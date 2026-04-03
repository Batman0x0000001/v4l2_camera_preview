#ifndef WEBRTC_BRIDGE_H
#define WEBRTC_BRIDGE_H

#include<stddef.h>
#include<stdint.h>

/*
场景：你的 C++ 原生推流端 -> 浏览器
第一步：原生端创建 PeerConnection
    并配置 ICE server，也就是你在代码里看到的 stun_url / turn_url 这些参数。W3C 的 RTCConfiguration 本来就包含 iceServers。

第二步：原生端生成 offer
    这是“我打算怎么和你建立媒体连接”的提案。JSEP 把它放在 createOffer() 这一步。

第三步：原生端 setLocalDescription(offer)
    意思是“我先把自己的提案应用到自己这里”。

第四步：通过信令通道把 offer 发给浏览器
    信令通道不是 WebRTC 内建的，要由应用自己提供。

第五步：浏览器 setRemoteDescription(offer)
    意思是“浏览器把你这边的提案安装到它的 PeerConnection 里”。

第六步：浏览器生成 answer，并 setLocalDescription(answer)
    表示“我接受/调整后的协商结果是这个”。

第七步：浏览器把 answer 发回原生端
    还是走信令通道。

第八步：原生端 setRemoteDescription(answer)
    至此，媒体和传输配置层面的协商才算装完。

第九步：双方不断产生 ICE candidate，并互相发送
    如果是 Trickle ICE，这个交换是增量进行的，不用等全收集完。

第十步：收到一个 candidate，就 addIceCandidate / add_remote_candidate
    把这条“可能可达的网络路径”交给本地 ICE agent。

第十一步：ICE 检查并选出 candidate pair
    选出真正可用的一对地址后，媒体才能稳定发起来。
    RFC 8445 明确说，ICE 会创建 candidate pairs、执行 connectivity checks，
    并最终选出用于收发数据的 pair。
*/

/*
1、set_remote_description()
    = 把对方的协商结果装到本地 PeerConnection 里

2、add_remote_candidate()
    = 把对方新发现的一条网络路径交给本地 ICE

3、stun_url
    = 帮助我发现 NAT 映射地址、做连通性检查的工具服务器

4、turn_url + username + password
    = 当直连失败时，去申请中继转发所需的服务器与凭据
*/

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
    
    // video参数
    int width;
    int height;
    int fps;
    char video_codec[32];
    int video_payload_type;
    int video_clock_rate;


    //audio
    int enable_audio;
    char audio_codec[32];
    int audio_payload_type;
    int audio_clock_rate;
    int audio_sample_rate;
    int audio_channels;

    // ICE/中继参数
    //STUN 的价值就是帮助你知道：“我在外网视角下，被映射成了哪个 IP:port。”
    char stun_url[256];
    //TURN：直连不行时，给你兜底中继
    char turn_url[256];
    char turn_username[128];
    char turn_password[128];
    int signaling_port;
}WebRtcSenderConfig;

typedef struct WebRtcEncodedVideoFrame{
    const uint8_t *data;
    size_t size;

    int64_t pts_us;
    int64_t dts_us;

    int is_keyframe;
}WebRtcEncodedVideoFrame;

typedef struct WebRtcEncodedAudioFrame{
    const uint8_t *data;
    size_t size;

    int64_t pts_us;
    
    uint32_t sample_rate;
    uint16_t channels;
    uint32_t samples_per_channel;
}WebRtcEncodedAudioFrame;


//description 解决的是“协议和媒体怎么谈”
//candidate 解决的是“数据包到底从哪儿走”
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

int webrtc_sender_send_audio(WebRtcSender *sender,const WebRtcEncodedAudioFrame *frame);

WebRtcPeerState webrtc_sender_get_state(const WebRtcSender *sender);
int webrtc_sender_is_ready(const WebRtcSender *sender);

void webrtc_sender_stop(WebRtcSender *sender);
void webrtc_sender_destroy(WebRtcSender **sender);

#ifdef __cplusplus
}
#endif

#endif