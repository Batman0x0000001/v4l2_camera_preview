#include"webrtc_publisher.hpp"
#include"webrtc_signaling.hpp"
#include"rtc/rtc.hpp"

#include<exception>
#include<sstream>
#include<string>
#include<utility>

// 这里的东西只在当前 .cpp 文件内可见,在匿名命名空间里再加 static 是多余的,加不加效果一样
namespace{

//回调参数都是 const char *，来自 C 那边，随时可能是 nullptr，用这个函数转换就很安全。
std::string CopyCString(const char* text){
    return text ? std::string(text) : std::string();
}

WebRtcPeerState ToBridgeState(rtc::PeerConnection::State state){
    switch (state)
    {
    case rtc::PeerConnection::State::New:
        return WEBRTC_PEER_STATE_NEW;
    case rtc::PeerConnection::State::Connecting:
        return WEBRTC_PEER_STATE_CONNECTING;
    case rtc::PeerConnection::State::Connected:
        return WEBRTC_PEER_STATE_CONNECTED;
    case rtc::PeerConnection::State::Disconnected:
        return WEBRTC_PEER_STATE_DISCONNECTED;
    case rtc::PeerConnection::State::Failed:
        return WEBRTC_PEER_STATE_FAILED;
    case rtc::PeerConnection::State::Closed:
    default:
        return WEBRTC_PEER_STATE_CLOSED;
    }
}

rtc::Description::Type ParseDescriptionType(const std::string& type){
    if(type == "offer")
        return rtc::Description::Type::Offer;
    if(type == "answer")
        return rtc::Description::Type::Answer;
    if(type == "pranswer")
        return rtc::Description::Type::Pranswer;
    if(type == "rollback")
        return rtc::Description::Type::Rollback;

    return rtc::Description::Type::Unspec;
}

rtc::NalUnit::Separator DetectNalSeparator(const uint8_t* data,size_t size){
    if(data && size >=4 && 
        data[0] == 0x00 && data[1] == 0x00 && 
        data[2] == 0x00 && data[3] == 0x01){
        return rtc::NalUnit::Separator::LongStartSequence;
    }

    if(data && size >=3 && 
        data[0] == 0x00 && data[1] == 0x00 && 
        data[2] == 0x01){
        return rtc::NalUnit::Separator::ShortStartSequence;
    }

    return rtc::NalUnit::Separator::Length;
}

const char* NalSeparatorName(rtc::NalUnit::Separator separator){
    switch(separator){
        case rtc::NalUnit::Separator::Length:
            return "Length";
        case rtc::NalUnit::Separator::LongStartSequence:
            return "LongStartSequence";
        case rtc::NalUnit::Separator::ShortStartSequence:
            return "ShortStartSequence";
        case rtc::NalUnit::Separator::StartSequence:
            return "StartSequence";
        default:
            return "unknown";
    }
}
}//结束匿名空间

WebRtcPublisher::WebRtcPublisher(const WebRtcSenderConfig& config,const WebRtcSenderCallbacks& callbacks)
    :m_Config(config),m_Callbacks(callbacks){}

WebRtcPublisher::~WebRtcPublisher(){
    Stop();
}

WebRtcPeerState WebRtcPublisher::GetState()const{
    return m_State;
}

int WebRtcPublisher::IsReady()const{
    return m_State == WEBRTC_PEER_STATE_CONNECTED ? 1 : 0;
}

void WebRtcPublisher::ChangeState(WebRtcPeerState newState){
    if(m_State == newState){
        return;
    }
    m_State = newState;

    if(m_Callbacks.on_state){
        m_Callbacks.on_state(m_State,m_Callbacks.userdata);
    }
}

void WebRtcPublisher::EmitLog(WebRtcBridgeLogLevel level,const std::string& message)const{
    if(!m_Callbacks.on_log){
        return;
    }

    m_Callbacks.on_log(level,message.c_str(),m_Callbacks.userdata);
}

void WebRtcPublisher::EmitLocalDescription(const rtc::Description& description){
    if(m_Callbacks.on_local_description){
        //调用 libdatachannel 的方法，返回 SDP 类型字符串，值为 "offer" 或 "answer"。
        const std::string type = description.typeString();

        //rtc::Description 重载了 std::string 的类型转换运算符，所以可以直接用 std::string(description) 提取完整的 SDP 文本内容。
        const std::string sdp = std::string(description);

        //.c_str() 把 std::string 转成 const char *，因为 C 接口只认裸指针。
        m_Callbacks.on_local_description(type.c_str(),sdp.c_str(),m_Callbacks.userdata);
        
        if(m_SignalingServer && type == "offer"){
            m_SignalingServer->SendOffer(sdp);
        }
        return;
    }

    if(m_SignalingServer && description.typeString() == "offer"){
        m_SignalingServer->SendOffer(std::string(description));
    }
}


void WebRtcPublisher::EmitLocalCandidate(const rtc::Candidate& candidate){
    //提取 ICE 候选字符串，内容类似：
    //candidate:1234567890 1 udp 2122260223 192.168.1.100 54321 typ host
    const std::string candidateText = candidate.candidate();

    //提取媒体流标识符，标识这个候选属于哪条媒体流，通常是 "0" 或 "video"。
    const std::string mid = candidate.mid();
    
    if(m_Callbacks.on_local_candidate){
        m_Callbacks.on_local_candidate(candidateText.c_str(),mid.c_str(),m_Callbacks.userdata);
    }

    if(m_SignalingServer){
        m_SignalingServer->SendCandidate(mid,candidateText);
    }
    

}

void WebRtcPublisher::BindPeerCallbacks(){
    if(!m_PeerConnection){
        return;
    }
/*
    [this]      // 捕获当前对象指针，可以访问 this->m_Callbacks 等成员
    []          // 不捕获任何东西
    [=]         // 按值捕获所有外部变量
    [&]         // 按引用捕获所有外部变量
    [this, &x]  // 捕获 this 和某个局部变量 x 的引用

    // lambda 写法
    [this](rtc::Description description) {
        // 函数体
    }

    // 等价的普通函数写法
    void OnLocalDescription(rtc::Description description) {
        // 函数体
    }
*/
    m_PeerConnection->onLocalDescription([this](rtc::Description description){
        EmitLog(WEBRTC_LOG_INFO,"local description generated, send it through signaling");
        EmitLocalDescription(description);
    });

    m_PeerConnection->onLocalCandidate([this](rtc::Candidate candidate){
        EmitLocalCandidate(candidate);
    });

    m_PeerConnection->onStateChange([this](rtc::PeerConnection::State state){
        ChangeState(ToBridgeState(state));
    });

    /*
        std::ostringstream
        字符串流，用于拼接字符串，类似于 C 的 snprintf：

        std::ostringstream oss;
        oss << "gathering state changed:" << state;  // 把文字和 state 拼在一起
        oss.str();  // 取出拼接结果，返回 std::string
    */

    m_PeerConnection->onIceStateChange([this](rtc::PeerConnection::IceState state){
        std::ostringstream oss;
        oss << "ice state changed:" << state;
        EmitLog(WEBRTC_LOG_INFO,oss.str());
    });

    m_PeerConnection->onSignalingStateChange([this](rtc::PeerConnection::SignalingState state){
        std::ostringstream oss;
        oss << "signaling state changed:" << state;
        EmitLog(WEBRTC_LOG_INFO,oss.str());
    });

    m_PeerConnection->onGatheringStateChange([this](rtc::PeerConnection::GatheringState state){
        std::ostringstream oss;
        oss << "gathering state changed:" << state;
        EmitLog(WEBRTC_LOG_INFO,oss.str());
    });
}

int WebRtcPublisher::CreatePeerConnection(){
    try{
        rtc::Configuration config;
        config.disableAutoNegotiation = true;

        if(m_Config.stun_url[0] != '\0'){
            config.iceServers.emplace_back(CopyCString(m_Config.stun_url));
        }

        if(m_Config.turn_url[0] != '\0'){
            EmitLog(WEBRTC_LOG_WARN,"TURN configuration is reserved but not wired in Part 4 yet.");
        }
        //std::move(config):把 config 从拷贝变成移动
        m_PeerConnection = std::make_shared<rtc::PeerConnection>(std::move(config));
        EmitLog(WEBRTC_LOG_INFO,"peer connection created");
    }catch(const std::exception& e){
        EmitLog(WEBRTC_LOG_ERROR,std::string("failed to create peer connection:")+e.what());
        return -1;
    }catch (...) {//catch (...) 是捕获所有异常的语法，... 是固定写法（不是省略号的意思）
        EmitLog(WEBRTC_LOG_ERROR, "failed to create peer connection");
        return -1;
    }

    BindPeerCallbacks();
    ChangeState(WEBRTC_PEER_STATE_NEW);
    return 0;
}

int WebRtcPublisher::AddVideoTrack(){
    if(!m_PeerConnection){
        EmitLog(WEBRTC_LOG_ERROR,"peer connection is null");
        return -1;
    }

    const std::string codec = CopyCString(m_Config.video_codec);
    if(!codec.empty() && codec != "H264"){
        EmitLog(WEBRTC_LOG_ERROR,"Part 4 only supports H264 because the current C pipeline encodes H264");
        return -1;
    }

    //payload type 是 RTP 协议里标识编码格式的数字，H264 的动态 payload type 范围是 96-127，没有配置就默认用 96。
    const int payloadType = m_Config.payload_type > 0 ? m_Config.payload_type : 96;

    try{
        rtc::Description::Video video(m_VideoMid,rtc::Description::Direction::SendOnly);
        video.addH264Codec(payloadType);
        video.addSSRC(m_VideoSsrc,m_VideoCname,m_VideoMsid,m_VideoMid);

        m_VideoTrack = m_PeerConnection->addTrack(video);
        if(!m_VideoTrack){
            EmitLog(WEBRTC_LOG_ERROR,"addTrack returned null");
            return -1;
        }

        m_VideoTrack->onOpen([this](){
            m_VideoTrackOpen = true;
            EmitLog(WEBRTC_LOG_INFO,"video track opened");
        });

        EmitLog(WEBRTC_LOG_INFO,"video track added");
        return 0;
    /*
    凡是调用第三方 C++ 库的函数，都应该套 try/catch，因为你无法保证它内部不抛异常。自己写的函数则视情况而定。

    不加 try/catch :

        异常向上传播，穿越extern "C" 边界：

        addTrack 抛出异常
            ↓
        AddVideoTrack 没有捕获，继续向上
            ↓
        webrtc_sender_start (extern "C") 没有捕获，穿越 C ABI 边界
            ↓
        未定义行为，程序直接崩溃
    */
    }catch(const std::exception& e){
        EmitLog(WEBRTC_LOG_ERROR,std::string("failed to add video track:")+e.what());
        return -1;
    }catch(...){
        EmitLog(WEBRTC_LOG_ERROR,"failed to add video track");
        return -1;
    }
}

int WebRtcPublisher::StartLocalOffer(){
    if(!m_PeerConnection){
        EmitLog(WEBRTC_LOG_ERROR,"peer connection is null");
        return -1;
    }

    try{
        m_PeerConnection->setLocalDescription(rtc::Description::Type::Offer);
        EmitLog(WEBRTC_LOG_INFO,"local offer requested, waiting for onLocalDescription callback");
        return 0;
    }catch(const std::exception& e){
        EmitLog(WEBRTC_LOG_ERROR,std::string("failed to start local offer:")+e.what());
        return -1;
    }catch(...){
        EmitLog(WEBRTC_LOG_ERROR,"failed to start local offer");
        return -1;
    }
}

int WebRtcPublisher::ConfigureVideoSender(const WebRtcEncodedVideoFrame* frame){
    if(!m_VideoTrack){
        EmitLog(WEBRTC_LOG_ERROR,"video track is null");
        return -1;
    }

    if(m_VideoPacketizer){
        return 0;
    }

    if(!frame || !frame->data || frame->size == 0){
        EmitLog(WEBRTC_LOG_ERROR,"invalid encoded video frame for packetizer setup");
        return -1;
    }

    const int payloadType = m_Config.payload_type > 0 ? m_Config.payload_type : 96;
    
    //检测 NAL 分隔符，需要知道输入是哪种格式才能正确打包，所以用第一帧数据检测。
    const auto separator = DetectNalSeparator(frame->data,frame->size);

    try
    {
        m_VideoRtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
            m_VideoSsrc,
            m_VideoCname,
            static_cast<uint8_t>(payloadType),
            rtc::H264RtpPacketizer::ClockRate
        );

        /*
        H264 帧
            ↓
        H264RtpPacketizer  → 切割成 RTP 包发送
            ↓
        RtcpSrReporter     → 定期发送同步报告
            ↓
        RtcpNackResponder  → 响应重传请求
        */

        m_VideoPacketizer = std::make_shared<rtc::H264RtpPacketizer>(
            separator,
            m_VideoRtpConfig
        );

        m_VideoSrReporter = std::make_shared<rtc::RtcpSrReporter>(m_VideoRtpConfig);
        m_VideoPacketizer->addToChain(m_VideoSrReporter);

        m_VideoNackResponder = std::make_shared<rtc::RtcpNackResponder>();
        m_VideoPacketizer->addToChain(m_VideoNackResponder);

        //把整条处理链挂到视频轨道上
        m_VideoTrack->setMediaHandler(m_VideoPacketizer);

        EmitLog(WEBRTC_LOG_INFO,std::string("configured H264 packetizer:separator=")+NalSeparatorName(separator));

        return 0;

    }
    catch(const std::exception& e)
    {
        EmitLog(WEBRTC_LOG_ERROR,std::string("failed to configure video sender")+e.what());
        return -1;
    }
    catch(...)
    {
        EmitLog(WEBRTC_LOG_ERROR,std::string("failed to configure video sender"));
        return -1;
    }
    
}

int WebRtcPublisher::Start(){
    if(m_Started){
        return 0;
    }

    if(StartSignalingServer() < 0){
        return -1;
    }

    if(CreatePeerConnection() < 0){
        return -1;
    }

    if(AddVideoTrack() < 0){
        Stop();
        return -1;
    }

    if(StartLocalOffer() < 0){
        Stop();
        return -1;
    }

    m_Started = true;
    return 0;
}

int WebRtcPublisher::SendVideo(const WebRtcEncodedVideoFrame* frame){
    if(!m_VideoTrack){
        EmitLog(WEBRTC_LOG_ERROR,"video track is null");
        return -1;
    }

    if(!frame || !frame->data || frame->size == 0){
        EmitLog(WEBRTC_LOG_ERROR,"invalid encoded video frame");
        return -1;
    }

    if(!m_VideoTrackOpen || !m_VideoTrack->isOpen()){
        if(!m_HasWarnedTrackNotOpen){
            EmitLog(WEBRTC_LOG_WARN,"video track is not open yet, dropping encoded frames");
            m_HasWarnedTrackNotOpen = true;
        }
        return 0;
    }

    if(ConfigureVideoSender(frame) < 0){
        return -1;
    }

    const int64_t ptsUs = frame->pts_us >= 0 ? frame->pts_us : 0;

    try
    {
        m_VideoTrack->sendFrame(
            reinterpret_cast<const rtc::byte*>(frame->data),
            frame->size,
            std::chrono::duration<double,std::micro>(static_cast<double>(ptsUs))
        );

        return 0;
    }
    catch(const std::exception& e)
    {
        EmitLog(WEBRTC_LOG_ERROR,std::string("failed to send video frame: ")+ e.what());
        return -1;
    }
    catch(...){
        EmitLog(WEBRTC_LOG_ERROR,"failed to send video frame");
        return -1;
    }
    
}


int WebRtcPublisher::SetRemoteDescription(const char* type,const char* sdp){
    if(!m_PeerConnection){
        EmitLog(WEBRTC_LOG_ERROR,"peer connection is null");
        return -1;
    }

    const std::string sdpText = CopyCString(sdp);
    if(sdpText.empty()){
        EmitLog(WEBRTC_LOG_ERROR,"remote description is empty");
        return -1;
    }

    const std::string typeText = CopyCString(type);
    rtc::Description::Type desctiptionType = rtc::Description::Type::Unspec;

    if(!typeText.empty()){
        desctiptionType = ParseDescriptionType(typeText);
        if(desctiptionType == rtc::Description::Type::Unspec){
            EmitLog(WEBRTC_LOG_ERROR,std::string("unsupported remote description type:")+typeText);
            return -1;
        }
    }

    try{
        rtc::Description remoteDescription(sdpText,desctiptionType);
        m_PeerConnection->setRemoteDescription(remoteDescription);

        std::string effectiveType = typeText;
        if(effectiveType.empty()){
            effectiveType = remoteDescription.typeString();
        }

        EmitLog(WEBRTC_LOG_INFO,std::string("remote description applied: type=")+effectiveType);

        if(remoteDescription.type() == rtc::Description::Type::Offer){
            EmitLog(WEBRTC_LOG_WARN,"remote offer applied while auto negotiation is disabled;");
        }

        return 0;
    }catch(const std::exception& e){
        EmitLog(WEBRTC_LOG_ERROR,
                std::string("failed to apply remote description: ") + e.what());
        return -1;
    }catch(...){
        EmitLog(WEBRTC_LOG_ERROR,
                std::string("failed to apply remote description"));
        return -1;
    }
}

int WebRtcPublisher::AddRemoteCandidate(const char* candidate,const char* mid){
    if(!m_PeerConnection){
        EmitLog(WEBRTC_LOG_ERROR,"peer connection is null");
        return -1;
    }

    if(!m_PeerConnection->remoteDescription().has_value()){
        EmitLog(WEBRTC_LOG_ERROR,
                std::string("remote description must be set before adding remote candidates"));
        return -1;
    }

    const std::string candidateText = CopyCString(candidate);
    if(candidateText.empty()){
        EmitLog(WEBRTC_LOG_ERROR,"remote candidate is empty");
        return -1;
    }

    const std::string midText = CopyCString(mid);

    try{
        rtc::Candidate remoteCandidate = midText.empty() ? 
            rtc::Candidate(candidateText) : rtc::Candidate(candidateText,midText);
        
        m_PeerConnection->addRemoteCandidate(remoteCandidate);

        if(midText.empty()){
            EmitLog(WEBRTC_LOG_INFO, "remote candidate added");
        }else{
            EmitLog(WEBRTC_LOG_INFO,std::string("remote candidate added: mid=") + midText);
        }

        return 0;
    }catch(const std::exception& e){
        EmitLog(WEBRTC_LOG_ERROR,
                std::string("failed to add remote candidate: ") + e.what());
        return -1;
    }catch(...){
        EmitLog(WEBRTC_LOG_ERROR,
                std::string("failed to add remote candidate"));
        return -1;
    }
}

int WebRtcPublisher::StartSignalingServer(){
    if(m_SignalingServer){
        return 0;
    }

    m_SignalingServer = std::make_unique<WebRtcSignalingServer>(
        m_Config.signaling_port,
        [this](const std::string& sdp){
            if(SetRemoteDescription("answer",sdp.c_str()) < 0){
                EmitLog(WEBRTC_LOG_ERROR,"failed to apply browser answer from websocket signaling");
            }
        },
        [this](const std::string& mid,const std::string& candidate){
            if(AddRemoteCandidate(candidate.c_str(),mid.empty() ? nullptr : mid.c_str()) < 0){
                EmitLog(WEBRTC_LOG_ERROR,"failed to apply browser candidate from websocket signaling");
            }
        },
        [this](WebRtcBridgeLogLevel level,const std::string& message){
            EmitLog(level,message);
        }
    );

    return m_SignalingServer->Start();
}

void WebRtcPublisher::Stop(){
    // EmitLog(WEBRTC_LOG_INFO,"m_SignalingServer->Stop begin\n");
    if(m_SignalingServer){
        m_SignalingServer->Stop();
        // EmitLog(WEBRTC_LOG_INFO,"m_SignalingServer->Stop end\n");
        m_SignalingServer.reset();
    }
    

    m_VideoNackResponder.reset();
    m_VideoSrReporter.reset();
    m_VideoPacketizer.reset();
    m_VideoRtpConfig.reset();

    if(m_VideoTrack){
        //std::shared_ptr 虽然行为像指针，但它本身是一个对象，不是裸指针。
        /*
            .调用 shared_ptr 自身的成员函数
            ->通过 shared_ptr 调用内部 rtc::Track 对象的方法
        */
        m_VideoTrack.reset();
    }

    if(m_PeerConnection){
        try{
            m_PeerConnection->close();
            m_PeerConnection->resetCallbacks();
        }catch(...){

        }

        m_PeerConnection.reset();
    }

    m_VideoTrackOpen = false;
    m_HasWarnedTrackNotOpen = false;
    m_Started = false;
    ChangeState(WEBRTC_PEER_STATE_CLOSED);
}