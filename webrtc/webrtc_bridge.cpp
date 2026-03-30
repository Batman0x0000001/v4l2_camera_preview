#include"webrtc_bridge.h"

#include<new>
#include<cstdio>
#include<cstring>

class WebRtcSenderImpl
{
private:
    WebRtcSenderConfig config_;
    WebRtcSenderCallbacks callbacks_;
    WebRtcPeerState state_;
    bool started_;

private:
    void ChangeState(WebRtcPeerState new_state){
        if(state_ == new_state){
            return;
        }

        state_ = new_state;

        if(callbacks_.on_state){
            callbacks_.on_state(state_,callbacks_.userdata);
        }
    }

    void Log(WebRtcBridgeLogLevel level,const char *message){
        if(callbacks_.on_log){
            callbacks_.on_log(level,message,callbacks_.userdata);
        }
    }
public:
    WebRtcSenderImpl(const WebRtcSenderConfig& config,const WebRtcSenderCallbacks& callbacks)
        :config_(config),
        callbacks_(callbacks),
        state_(WEBRTC_PEER_STATE_NEW),
        started_(false)
    {}

    int Start(){
        Log(WEBRTC_LOG_INFO,"WebRTC bridge skeleton is ready, but publisher core is not implemented yet.");
        return -1;
    }

    int SetRemoteDescription(const char *type, const char *sdp) {
        (void)type;
        (void)sdp;
        Log(WEBRTC_LOG_WARN,
            "SetRemoteDescription is not implemented in Part 3.");
        return -1;
    }

    int AddRemoteCandidate(const char *candidate, const char *mid) {
        (void)candidate;
        (void)mid;
        Log(WEBRTC_LOG_WARN,
            "AddRemoteCandidate is not implemented in Part 3.");
        return -1;
    }

    int SendVideo(const WebRtcEncodedVideoFrame *frame) {
        (void)frame;
        Log(WEBRTC_LOG_WARN,
            "SendVideo is not implemented in Part 3.");
        return -1;
    }

    void Stop() {
        if (state_ != WEBRTC_PEER_STATE_CLOSED) {
            ChangeState(WEBRTC_PEER_STATE_CLOSED);
        }
        started_ = false;
    }

    WebRtcPeerState GetState() const {
        return state_;
    }

    int IsReady() const {
        return started_ && state_ == WEBRTC_PEER_STATE_CONNECTED;
    }
};

struct WebRtcSender
{
    WebRtcSenderImpl *impl;
};

static void webrtc_copy_text(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    std::snprintf(dst, dst_size, "%s", src);
}


//extern "C" 本质上是在 C++ 代码里开一个"C ABI 的窗口"，是跨语言互操作的基石。
extern "C" void webrtc_sender_config_init(WebRtcSenderConfig *cfg) {
    if (!cfg) {
        return;
    }

    std::memset(cfg, 0, sizeof(*cfg));

    webrtc_copy_text(cfg->stream_name, sizeof(cfg->stream_name), "camera");
    webrtc_copy_text(cfg->video_codec, sizeof(cfg->video_codec), "H264");

    cfg->width = 640;
    cfg->height = 480;
    cfg->fps = 30;

    cfg->payload_type = 96;
    cfg->video_clock_rate = 90000;
}

extern "C" void webrtc_sender_callbacks_init(WebRtcSenderCallbacks *callbacks) {
    if (!callbacks) {
        return;
    }

    std::memset(callbacks, 0, sizeof(*callbacks));
}

extern "C" const char *webrtc_peer_state_name(WebRtcPeerState state) {
    switch (state) {
    case WEBRTC_PEER_STATE_NEW:
        return "new";
    case WEBRTC_PEER_STATE_CONNECTING:
        return "connecting";
    case WEBRTC_PEER_STATE_CONNECTED:
        return "connected";
    case WEBRTC_PEER_STATE_DISCONNECTED:
        return "disconnected";
    case WEBRTC_PEER_STATE_FAILED:
        return "failed";
    case WEBRTC_PEER_STATE_CLOSED:
        return "closed";
    default:
        return "unknown";
    }
}

extern "C" int webrtc_sender_create(WebRtcSender **out_sender,
                                    const WebRtcSenderConfig *cfg,
                                    const WebRtcSenderCallbacks *callbacks) {
    WebRtcSender *sender = NULL;
    WebRtcSenderCallbacks local_callbacks;

    if (!out_sender || !cfg) {
        return -1;
    }

    *out_sender = NULL;

    webrtc_sender_callbacks_init(&local_callbacks);
    if (callbacks) {
        local_callbacks = *callbacks;
    }

    /*
        普通 new 的行为: 分配失败时抛出 std::bad_alloc 异常，如果没有捕获会直接终止程序。
        new (std::nothrow) 的行为: 分配失败时不抛异常，返回 nullptr，由调用方自己检查。
    */
    sender = new (std::nothrow) WebRtcSender;
    if (!sender) {
        return -1;
    }

    sender->impl = new (std::nothrow) WebRtcSenderImpl(*cfg, local_callbacks);
    if (!sender->impl) {
        delete sender;
        return -1;
    }

    *out_sender = sender;
    return 0;
}

extern "C" int webrtc_sender_start(WebRtcSender *sender) {
    if (!sender || !sender->impl) {
        return -1;
    }

    return sender->impl->Start();
}

extern "C" int webrtc_sender_set_remote_description(WebRtcSender *sender,
                                                    const char *type,
                                                    const char *sdp) {
    if (!sender || !sender->impl) {
        return -1;
    }

    return sender->impl->SetRemoteDescription(type, sdp);
}

extern "C" int webrtc_sender_add_remote_candidate(WebRtcSender *sender,
                                                  const char *candidate,
                                                  const char *mid) {
    if (!sender || !sender->impl) {
        return -1;
    }

    return sender->impl->AddRemoteCandidate(candidate, mid);
}

extern "C" int webrtc_sender_send_video(WebRtcSender *sender,
                                        const WebRtcEncodedVideoFrame *frame) {
    if (!sender || !sender->impl || !frame || !frame->data || frame->size == 0) {
        return -1;
    }

    return sender->impl->SendVideo(frame);
}

extern "C" WebRtcPeerState webrtc_sender_get_state(const WebRtcSender *sender) {
    if (!sender || !sender->impl) {
        return WEBRTC_PEER_STATE_CLOSED;
    }

    return sender->impl->GetState();
}

extern "C" int webrtc_sender_is_ready(const WebRtcSender *sender) {
    if (!sender || !sender->impl) {
        return 0;
    }

    return sender->impl->IsReady();
}

extern "C" void webrtc_sender_stop(WebRtcSender *sender) {
    if (!sender || !sender->impl) {
        return;
    }

    sender->impl->Stop();
}

extern "C" void webrtc_sender_destroy(WebRtcSender **sender) {
    if (!sender || !*sender) {
        return;
    }

    delete (*sender)->impl;
    (*sender)->impl = NULL;

    delete *sender;
    *sender = NULL;
}