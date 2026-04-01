#include"webrtc_signaling.hpp"

#include<rtc/rtc.hpp>
#include<sstream>
#include<utility>
#include<variant>

WebRtcSignalingServer::WebRtcSignalingServer(int port,AnswerCallback onAnswer,CandidateCallback onCandidate,LogCallback onLog)
    :m_Port(port > 0 ? port : 9001),m_OnAnswer(std::move(onAnswer)),m_OnCandidate(std::move(onCandidate)),m_OnLog(std::move(onLog)){

}

WebRtcSignalingServer::~WebRtcSignalingServer (){
    Stop();
}

int WebRtcSignalingServer::Port()const{
    return m_Port;
}

void WebRtcSignalingServer::EmitLog(WebRtcBridgeLogLevel level,const std::string& message)const{
    if(m_OnLog){
        m_OnLog(level,message);
    }
}

std::string WebRtcSignalingServer::BuildDescriptionMessage(const std::string& type,
                                                           const std::string& sdp) {
    return type + "\n" + sdp;
}

std::string WebRtcSignalingServer::BuildCandidateMessage(const std::string& mid,
                                                         const std::string& candidate) {
    return std::string("candidate\nmid=") + mid + "\n" + candidate;
}

bool WebRtcSignalingServer::ParseCandidateMessage(const std::string& message,
                                                  std::string& outMid,
                                                  std::string& outCandidate) {
    const std::string prefix = "candidate\n";
    const std::string midPrefix = "mid=";

    outMid.clear();
    outCandidate.clear();

    /*
    rfind 是从后往前搜索，返回最后一次出现的位置。但这里用法比较特殊：
    第二个参数 0 是搜索起始位置，告诉 rfind 从位置 0 开始往前搜索，也就是只检查位置 0。
        所以 rfind(prefix, 0) == 0 等价于：字符串在位置 0 处能找到这个前缀，即以该前缀开头。

    用 find 检查开头
        message.find(prefix) == 0  但会搜索整个字符串，效率低
    */
    if (message.rfind(prefix, 0) != 0) {
        return false;
    }

    //去掉第一行"candidate\n"，剩下的内容
    const std::string body = message.substr(prefix.size());
    const size_t lineEnd = body.find('\n');
    if (lineEnd == std::string::npos) {
        return false;// 没有换行符，格式错误
    }

    /*
    substr:    
        第一种：只传起始位置，取到末尾
        第二种：传起始位置和长度
    */
    const std::string firstLine = body.substr(0, lineEnd);
    if (firstLine.rfind(midPrefix, 0) != 0) {
        return false;
    }

    outMid = firstLine.substr(midPrefix.size());
    outCandidate = body.substr(lineEnd + 1);

    while (!outCandidate.empty() &&
           (outCandidate.back() == '\n' || outCandidate.back() == '\r')) {
        outCandidate.pop_back();
    }

    return !outCandidate.empty();
}

int WebRtcSignalingServer::Start(){
    if(m_Server){
        return 0;
    }

    try
    {
        rtc::WebSocketServer::Configuration config;
        config.port = static_cast<uint16_t>(m_Port);
        config.enableTls = false;

        m_Server = std::make_shared<rtc::WebSocketServer>(config);

        m_Server->onClient([this](std::shared_ptr<rtc::WebSocket> client){
            AttachClient(client);
        });

        std::ostringstream oss;
        oss << "websocket signaling server listening on ws://127.0.0.1:" << m_Server->port();
        EmitLog(WEBRTC_LOG_INFO,oss.str());

        return 0;
    }
    catch(const std::exception& e)
    {
        EmitLog(WEBRTC_LOG_ERROR,std::string("failed to start signaling server:")+e.what());
        return -1;
    }
    catch(...)
    {
        EmitLog(WEBRTC_LOG_ERROR,"failed to start signaling server:");
        return -1;
    }
}

void WebRtcSignalingServer::AttachClient(const std::shared_ptr<rtc::WebSocket>& client){
    if(!client){
        return;
    }

    {
        //lock_guard 在作用域结束时自动释放锁，不需要手动 unlock
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Client = client;
    }

    client->onOpen([this](){
        EmitLog(WEBRTC_LOG_INFO,"browser signaling websocket opened");
        FlushPendingMessages();
    });

    client->onClosed([this](){
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Client.reset();
        EmitLog(WEBRTC_LOG_WARN,"browswer signaling websocket closed");
    });

    //std::variant 表示消息可能是二进制或文本两种类型
    client->onMessage([this](std::variant<rtc::binary,rtc::string> message){
        // 检查是否是文本类型
        if(!std::holds_alternative<rtc::string>(message)){
            EmitLog(WEBRTC_LOG_WARN,"ignored non-text signaling message");
            return;
        }
        //取出文本内容
        HandleMessage(std::get<rtc::string>(message));
    });

    if(client->isOpen()){
        FlushPendingMessages();
    }
}

void WebRtcSignalingServer::QueueOrSend(const std::string& message){
    std::lock_guard<std::mutex> lock(m_Mutex);

    if(m_Client && m_Client->isOpen()){
        if(!m_Client->send(message)){
            EmitLog(WEBRTC_LOG_WARN,"signaling send failed, queue for retry");
            m_PendingMessages.emplace_back(message);
        }
        return;
    }

    m_PendingMessages.emplace_back(message);
}

void WebRtcSignalingServer::FlushPendingMessages(){
    std::lock_guard<std::mutex> lock(m_Mutex);

    if(!m_Client || !m_Client->isOpen()){
        return;
    }

    std::vector<std::string> remaining;
    //尝试预先分配足够的内存来容纳指定数量的元素。
    remaining.reserve(m_PendingMessages.size());

    for(const auto& message : m_PendingMessages){
        if(!m_Client->send(message)){
            remaining.emplace_back(message);
        }
    }

    //与另一个vector交换数据
    m_PendingMessages.swap(remaining);
}

void WebRtcSignalingServer::SendOffer(const std::string& sdp){
    QueueOrSend(BuildDescriptionMessage("offer",sdp));
}

void WebRtcSignalingServer::SendCandidate(const std::string& mid,const std::string& candidate){
    QueueOrSend(BuildCandidateMessage(mid,candidate));
}

void WebRtcSignalingServer::HandleMessage(const std::string& message){
    const std::string answerPrefix = "answer\n";
    const std::string candidatePrefix = "candidate\n";

    if(message.rfind(answerPrefix,0)  == 0){
        const std::string sdp = message.substr(answerPrefix.size());

        if(m_OnAnswer){
            m_OnAnswer(sdp);
        }
        return;
    }

    if(message.rfind(candidatePrefix,0) == 0){
        std::string mid;
        std::string candidate;

        if(!ParseCandidateMessage(message,mid,candidate)){
            EmitLog(WEBRTC_LOG_ERROR,"failed to parse candidate signaling message");
            return;
        }

        if(m_OnCandidate){
            m_OnCandidate(mid,candidate);
        }
        return;
    }
}

void WebRtcSignalingServer::Stop(){
    // {
    //     std::lock_guard<std::mutex> lock(m_Mutex);

    //     if(m_Client){
    //         try{
    //             m_Client->close();
    //         }catch(...){
    //         }
    //         m_Client.reset();
    //     }
    //     m_PendingMessages.clear();
    // }

    // if(m_Server){
    //     try{
    //         m_Server->stop();
    //     }catch(...){
    //     }
    //     m_Server.reset();
    // }
    std::shared_ptr<rtc::WebSocket> client;
    std::shared_ptr<rtc::WebSocketServer> server;

    {
        std::lock_guard<std::mutex> lock(m_Mutex);

        client = std::move(m_Client);
        server = std::move(m_Server);
        m_PendingMessages.clear();
    }

    if (client) {
        try {
            client->close();
        } catch (...) {
        }
    }

    if (server) {
        try {
            server->stop();
        } catch (...) {
        }
    }
}