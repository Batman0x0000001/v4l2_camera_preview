#include"webrtc_publisher.hpp"

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

void WebRtcPublisher::EmitLocalDescription(const rtc::Description& desciption){
    if(!m_Callbacks.on_local_description){
        return;
    }

    //调用 libdatachannel 的方法，返回 SDP 类型字符串，值为 "offer" 或 "answer"。
    const std::string type = desciption.typeString();

    //rtc::Description 重载了 std::string 的类型转换运算符，所以可以直接用 std::string(description) 提取完整的 SDP 文本内容。
    const std::string sdp = std::string(desciption);

    //.c_str() 把 std::string 转成 const char *，因为 C 接口只认裸指针。
    m_Callbacks.on_local_description(type.c_str(),sdp.c_str(),m_Callbacks.userdata);
}

void WebRtcPublisher::EmitLocalCandidate(const rtc::Candidate& candidate){
    if(!m_Callbacks.on_local_candidate){
        return;
    }

    //提取 ICE 候选字符串，内容类似：
    //candidate:1234567890 1 udp 2122260223 192.168.1.100 54321 typ host
    const std::string candidateText = candidate.candidate();

    //提取媒体流标识符，标识这个候选属于哪条媒体流，通常是 "0" 或 "video"。
    const std::string mid = candidate.mid();

    m_Callbacks.on_local_candidate(candidateText.c_str(),mid.c_str(),m_Callbacks.userdata);
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
        enum class GatheringState {
            New,
            InProgress,
            Complete
        };

        std::ostringstream
        字符串流，用于拼接字符串，类似于 C 的 snprintf：

        std::ostringstream oss;
        oss << "gathering state changed:" << state;  // 把文字和 state 拼在一起
        oss.str();  // 取出拼接结果，返回 std::string
    */
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
        const std::string mid = "video";// 媒体流标识符
        const std::string cname = "pushstream-video";// RTP 同步源名称
        const std::string msid = "pushstream-stream";// 媒体流 ID
        const uint32_t ssrc = 1;// 同步源标识符，唯一标识一路 RTP 流

        rtc::Description::Video video(mid,rtc::Description::Direction::SendOnly);
        video.addH264Codec(payloadType);
        video.addSSRC(ssrc,cname,msid,mid);

        m_VideoTrack = m_PeerConnection->addTrack(video);
        if(!m_VideoTrack){
            EmitLog(WEBRTC_LOG_ERROR,"addTrack returned null");
            return -1;
        }

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

int WebRtcPublisher::Start(){
    if(m_Started){
        return 0;
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
    //转换成 void 类型后值直接丢弃，不产生任何代码
    (void)frame;

    if(!m_HasWarnedAboutVideoStub){
        EmitLog(WEBRTC_LOG_WARN,"video send path is not implemented in Part 4 yet, dropping encoded frames");
        m_HasWarnedAboutVideoStub = true;
    }

    return 0;
}


int WebRtcPublisher::SetRemoteDescription(const char* type,const char* sdp){
    (void)type;
    (void)sdp;

    EmitLog(WEBRTC_LOG_WARN,"SetRemoteDescription is not implemented in Part 4 yet");
    return -1;
}
int WebRtcPublisher::AddRemoteCandidate(const char* candidate,const char* mid){
    (void)candidate;
    (void)mid;

    EmitLog(WEBRTC_LOG_WARN,"SetRemoteDescription is not implemented in Part 4 yet");
    return -1;
}

void WebRtcPublisher::Stop(){
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

    m_Started = false;
    ChangeState(WEBRTC_PEER_STATE_CLOSED);
}