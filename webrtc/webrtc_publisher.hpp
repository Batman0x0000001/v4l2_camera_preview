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
}

class WebRtcPublisher
{
private://data
    WebRtcSenderConfig m_Config{};
    WebRtcSenderCallbacks m_Callbacks{};

    std::shared_ptr<rtc::PeerConnection> m_PeerConnection;
    std::shared_ptr<rtc::Track> m_VideoTrack;

    WebRtcPeerState m_State = WEBRTC_PEER_STATE_NEW;
    bool m_Started = false;
    bool m_HasWarnedAboutVideoStub = false;

private://method
    int CreatePeerConnection();
    void BindPeerCallbacks();
    int AddVideoTrack();
    int StartLocalOffer();

    void ChangeState(WebRtcPeerState newState);
    void EmitLog(WebRtcBridgeLogLevel level,const std::string& message) const;
    void EmitLocalDescription(const rtc::Description& description);
    void EmitLocalCandidate(const rtc::Candidate& candidate);
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
