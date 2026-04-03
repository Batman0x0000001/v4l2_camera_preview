#pragma once

#include"webrtc_bridge.h"

#include<memory>
#include<string>
#include <rtc/common.hpp> 

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

    //connection
    WebRtcSenderConfig m_Config{};
    WebRtcSenderCallbacks m_Callbacks{};
    std::shared_ptr<rtc::PeerConnection> m_PeerConnection;
    std::unique_ptr<WebRtcSignalingServer> m_SignalingServer;
    WebRtcPeerState m_State = WEBRTC_PEER_STATE_NEW;


    std::shared_ptr<rtc::Track> m_VideoTrack;//表示“PeerConnection 上那条视频轨道”
    std::shared_ptr<rtc::Track> m_AudioTrack;

    //维护 RTP 发送所需的配置，比如 SSRC、payload type、clock rate
    std::shared_ptr<rtc::RtpPacketizationConfig> m_VideoRtpConfig;

    //打包器：把一帧样本拆成 WebRTC/UDP 真正要发的 RTP 包
    std::shared_ptr<rtc::H264RtpPacketizer> m_VideoPacketizer;

    //SR 报告器：定期发送 RTCP Sender Report，用于音视频同步
    std::shared_ptr<rtc::RtcpSrReporter> m_VideoSrReporter;

    // NACK 响应器：处理对端请求重传丢失的包
    std::shared_ptr<rtc::RtcpNackResponder> m_VideoNackResponder;

    // 同步源标识符，唯一标识这路流
    uint32_t m_VideoSsrc = 1;
    uint32_t m_AudioSsrc = 2;

    std::string m_VideoMid = "video";
    std::string m_AudioMid = "audio";

    // 流的名称
    std::string m_VideoCname = "pushstream-video";
    std::string m_VideoMsid = "pushstream-stream";
    std::string m_AudioCname = "pushstream-audio";
    std::string m_AudioMsid = "pushstream-stream";

    bool m_Started = false;
    bool m_VideoTrackOpen = false;
    bool m_AudioTrackOpen = false;

    uint16_t m_AudioSequence = 0;
    uint32_t m_AudioTimestamp = 0;
    bool m_HasAudioRtpState = false;    

    bool m_HasWarnedVideoTrackNotOpen = false;
    bool m_HasWarnedAudioTrackNotOpen = false;

    bool m_WaitingForFirstKeyframe = true;
    bool m_HasWarnedWaitingKeyframe = false;


    // rtc::binary m_CachedKeyframeSample;
    // bool m_HasCachedKeyframe = false;    
    // bool m_HasPrimedDecoder = false;

private://method
    int CreatePeerConnection();
    void BindPeerCallbacks();
    int AddVideoTrack();
    int AddAudioTrack();
    int StartLocalOffer();
    int ConfigureVideoSender(const WebRtcEncodedVideoFrame* frame);
    rtc::binary BuildOpusRtpPacket(const WebRtcEncodedAudioFrame* frame,uint8_t payloadType,uint16_t sequence,uint32_t timestamp)const;
    void ChangeState(WebRtcPeerState newState);
    void EmitLog(WebRtcBridgeLogLevel level,const std::string& message) const;
    void EmitLocalDescription(const rtc::Description& description);
    void EmitLocalCandidate(const rtc::Candidate& candidate);

    int StartSignalingServer();

    // void CacheKeyframeSample(const WebRtcEncodedVideoFrame* frame);
    // int PrimeDecoderWithCachedKeyframe();
public:
    WebRtcPublisher(const WebRtcSenderConfig& config,const WebRtcSenderCallbacks& callbacks);
    ~WebRtcPublisher();
    
    int Start();
    void Stop();

    int SetRemoteDescription(const char* type,const char* sdp);
    int AddRemoteCandidate(const char* candidate,const char* mid);
    int SendVideo(const WebRtcEncodedVideoFrame* frame);
    int SendAudio(const WebRtcEncodedAudioFrame* frame);

    WebRtcPeerState GetState()const;
    int IsReady()const;
};
