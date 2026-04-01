#pragma once

#include"webrtc_bridge.h"

#include<memory>
#include<string>

//前向声明:只告诉编译器"这个类存在"，但不给出完整定义：
namespace rtc{
    class PeerConnection;
    class Track;
    class Description;
    class Candidate;
    class RtpPacketizationConfig;
    class H264RtpPacketizer;
    class RtcpSrReporter;
    class RtcpNackResponder;
}

class WebRtcSignalingServer;

class WebRtcPublisher
{
private://data
    WebRtcSenderConfig m_Config{};
    WebRtcSenderCallbacks m_Callbacks{};

    std::shared_ptr<rtc::PeerConnection> m_PeerConnection;
    std::unique_ptr<WebRtcSignalingServer> m_SignalingServer;
    std::shared_ptr<rtc::Track> m_VideoTrack;//表示“PeerConnection 上那条视频轨道”

    //维护 RTP 发送所需的配置，比如 SSRC、payload type、clock rate
    std::shared_ptr<rtc::RtpPacketizationConfig> m_VideoRtpConfig;

    //H264 打包器：把一帧 H264 样本拆成 WebRTC/UDP 真正要发的 RTP 包
    std::shared_ptr<rtc::H264RtpPacketizer> m_VideoPacketizer;

    //SR 报告器：定期发送 RTCP Sender Report，用于音视频同步
    std::shared_ptr<rtc::RtcpSrReporter> m_VideoSrReporter;

    // NACK 响应器：处理对端请求重传丢失的包
    std::shared_ptr<rtc::RtcpNackResponder> m_VideoNackResponder;

    // 同步源标识符，唯一标识这路流
    uint32_t m_VideoSsrc = 1;

    std::string m_VideoMid = "video";

    // 流的名称
    std::string m_VideoCname = "pushstream-video";
    std::string m_VideoMsid = "pushstream-stream";


    WebRtcPeerState m_State = WEBRTC_PEER_STATE_NEW;
    bool m_Started = false;
    bool m_VideoTrackOpen = false;
    bool m_HasWarnedTrackNotOpen = false;

private://method
    int CreatePeerConnection();
    void BindPeerCallbacks();
    int AddVideoTrack();
    int StartLocalOffer();
    int ConfigureVideoSender(const WebRtcEncodedVideoFrame* frame);

    void ChangeState(WebRtcPeerState newState);
    void EmitLog(WebRtcBridgeLogLevel level,const std::string& message) const;
    void EmitLocalDescription(const rtc::Description& description);
    void EmitLocalCandidate(const rtc::Candidate& candidate);

    int StartSignalingServer();
public:
    WebRtcPublisher(const WebRtcSenderConfig& config,const WebRtcSenderCallbacks& callbacks);
    ~WebRtcPublisher();
    
    int Start();
    void Stop();

    int SetRemoteDescription(const char* type,const char* sdp);
    int AddRemoteCandidate(const char* candidate,const char* mid);
    int SendVideo(const WebRtcEncodedVideoFrame* frame);

    WebRtcPeerState GetState()const;
    int IsReady()const;
};
