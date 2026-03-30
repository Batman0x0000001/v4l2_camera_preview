#ifndef WEBRTC_BRIDGE_H
#define WEBRTC_BRIDGE_H

#include<stddef.h>
#include<stdint.h>

/*
    头文件暴露的是纯 C 函数，真正干活的是内部 C++ 对象。
    这一层相当于“翻译层”：
        C 侧调用 webrtc_sender_send_video(...)
        桥接层把它翻译成 sender->impl->SendVideo(...)
*/


//宏含义    #ifdef __cplusplus      当前是 C++ 编译器时成立
//         #ifndef __cplusplus     当前是 C 编译器时成立
#ifdef __cplusplus
extern "C" {
#endif

typedef struct WebRtcSender WebRtcSender;

typedef enum WebRtcBridgeLogLevel{
    WEBRTC_LOG_INFO  = 0,
    WEBRTC_LOG_WARN  = 1,
    WEBRTC_LOG_ERROR = 2
}WebRtcBridgeLogLevel;

typedef enum WebRtcPeerState{
    WEBRTC_PEER_STATE_NEW = 0,
    WEBRTC_PEER_STATE_CONNECTING = 1,
    WEBRTC_PEER_STATE_CONNECTED = 2,
    WEBRTC_PEER_STATE_DISCONNECTED = 3,
    WEBRTC_PEER_STATE_FAILED = 4,
    WEBRTC_PEER_STATE_CLOSED = 5,
}WebRtcPeerState;

typedef struct WebRtcSenderConfig{
    char stream_name[128];
    
    // 媒体参数
    int width;
    int height;
    int fps;
    char video_codec[32];
    int payload_type;
    int video_clock_rate;

    // ICE/中继参数
    char stun_url[256];
    char turn_url[256];
    char turn_username[128];
    char turn_password[128];
}WebRtcSenderConfig;

typedef struct WebRtcEncodedVideoFrame{
    const uint8_t *data;
    size_t size;

    int64_t pts_us;
    int64_t dts_us;

    int is_keyframe;
}WebRtcEncodedVideoFrame;

typedef void (*webrtc_on_local_description_fn)(const char *type,const char *sdp,void *userdata);

typedef void (*webrtc_on_local_candidate_fn)(const char *candidate,const char *mid,void *userdata);

typedef void (*webrtc_on_state_fn)(WebRtcPeerState state,void *userdata);

typedef void (*webrtc_on_log_fn)(WebRtcBridgeLogLevel level,const char *message,void *userdata);

typedef struct WebRtcSenderCallbacks{
    webrtc_on_local_description_fn on_local_description;
    webrtc_on_local_candidate_fn on_local_candidate;
    webrtc_on_state_fn on_state;
    webrtc_on_log_fn on_log;
    void *userdata;
}WebRtcSenderCallbacks;

void webrtc_sender_config_init(WebRtcSenderConfig *cfg);
void webrtc_sender_callbacks_init(WebRtcSenderCallbacks *callbacks);

const char *webrtc_peer_state_name(WebRtcPeerState state);

int webrtc_sender_create(WebRtcSender **out_sender,const WebRtcSenderConfig *cfg,const WebRtcSenderCallbacks *callbacks);

int webrtc_sender_start(WebRtcSender *sender);

int webrtc_sender_set_remote_description(WebRtcSender *sender,const char *type,const char *sdp);

int webrtc_sender_add_remote_candidate(WebRtcSender *sender,const char *candidate,const char *mid);

int webrtc_sender_send_video(WebRtcSender *sender,const WebRtcEncodedVideoFrame *frame);

WebRtcPeerState webrtc_sender_get_state(const WebRtcSender *sender);
int webrtc_sender_is_ready(const WebRtcSender *sender);

void webrtc_sender_stop(WebRtcSender *sender);
void webrtc_sender_destroy(WebRtcSender **sender);

#ifdef __cplusplus
}
#endif

#endif