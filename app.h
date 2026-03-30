#ifndef APP_H
#define APP_H

#include <stddef.h>
#include <stdint.h>

#include <alsa/asoundlib.h>
#include <linux/videodev2.h>
#include <SDL2/SDL.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#include "audio_queue.h"
#include "frame_queue.h"
#include "media_frame.h"

/*
 * app.h 当前仍然是“全局状态聚合头文件”。
 *
 * 这一轮先做低风险整理：
 * 1. 按职责重新排列结构体，降低阅读成本；
 * 2. 给关键字段补语义注释，特别是运行开关 / 时间戳 / 线程资源；
 * 3. 删除已经确认无用的历史字段，减轻后续 WebRTC/C++ 接入负担。
 *
 * 后续更彻底的整理方向：
 * - AppConfig 从 app.h 中独立出去；
 * - StreamState / RecordState / AudioCaptureState 各自拆到独立头文件；
 * - 让 WebRTC 只依赖清晰的采集输出接口，而不是直接感知整个 AppState。
 */

//opaque handle
typedef struct WebRtcSender WebRtcSender;


typedef enum StreamBackendType{
    STREAM_BACKEND_RTSP = 0,
    STREAM_BACKEND_WEBRTC = 1
}StreamBackendType;

//启动配置
typedef struct AppConfig {
    char device_path[256];
    int width;
    int height;
    int fps;

    StreamBackendType stream_backend;
    char stream_url[512];

    char record_dir[256];
    char snapshot_dir[256];

    /*
     * 启动后默认的用户开关状态。
     * 注意：模块内部 enabled / accepting_frames 仍然由各模块自己维护。
     */
    int start_stream_on;
    int start_record_on;

    /*
     * 该字段与 start_record_on 存在一定语义重叠。
     * 这一轮先不改行为，只保留说明，避免引入联动问题。
     */
    int auto_record_on_start;

    char audio_device[128];
    unsigned int audio_sample_rate;
    int audio_channels;
    unsigned int audio_period_frames;
} AppConfig;


//V4L2 mmap 缓冲区描述
typedef struct Buffer {
    void *start;
    size_t length;
} Buffer;


//UI 线程共享的最新预览帧
typedef struct SharedFrame {
    unsigned char *rgb;
    int width;
    int height;
    size_t bytes;

    /*
     * 仅用于标识“最新帧是否更新过”，
     * 不是 V4L2 的 sequence，也不是编码时间戳。
     */
    uint64_t frame_id;

    /* 采集元信息，供显示和调试读取。 */
    CaptureMeta meta;

    SDL_mutex *mutex;
} SharedFrame;

#define MAX_CONTROLS 128


//V4L2 控件抽象
typedef struct CameraControl {
    uint32_t id;
    char name[64];
    int type;

    int min;
    int max;
    int step;
    int def;
} CameraControl;

//推流状态
typedef struct StreamState {
    WebRtcSender *webrtc_sender;
    StreamBackendType backend_type;
    char output_url[512];

    const AVCodec *encoder;
    AVCodecContext *enc_ctx;
    AVFormatContext *ofmt_ctx;
    AVStream *video_st;

    struct SwsContext *sws_ctx;

    AVFrame *yuv_frame;
    AVPacket *pkt;

    int fps;

    /* 已送入编码链路的视频帧计数，便于统计。 */
    int64_t frame_index;

    /* 推流模块自己的相对时间基起点。 */
    uint64_t base_timestamp_us;
    int have_base_timestamp;

    /* 输入视频 PTS 单调性保护。 */
    int64_t last_input_pts;

    SDL_mutex *mutex;

    /*
     * enabled:
     *     模块资源是否初始化成功。
     * accepting_frames:
     *     工作线程当前是否继续接收新数据。
     * fatal_error:
     *     不可恢复错误，外层应停止继续喂数据。
     */
    int enabled;
    int accepting_frames;
    int fatal_error;

    SDL_Thread *thread;
    FrameQueue queue;
    AudioQueue audio_queue;

    uint64_t frames_encoded;
} StreamState;


//本地录像状态
typedef struct RecordState {
    /* 默认输出目录，例如 recordings/ */
    char output_dir[512];

    /* 当前录制会话最终落盘的完整文件路径。 */
    char active_output_path[512];

    int session_active;
    int stopping_session;

    uint64_t session_count;
    uint64_t session_start_media_us;
    uint64_t session_last_media_us;

    /* ---------- video ---------- */
    const AVCodec *encoder;
    AVCodecContext *enc_ctx;
    AVStream *video_st;
    struct SwsContext *sws_ctx;
    AVFrame *yuv_frame;
    AVPacket *pkt;

    /* ---------- audio ---------- */
    const AVCodec *audio_encoder;
    AVCodecContext *audio_enc_ctx;
    AVStream *audio_st;
    SwrContext *swr_ctx;
    AVFrame *audio_frame;
    AVPacket *audio_pkt;
    AVAudioFifo *audio_fifo;

    AVFormatContext *ofmt_ctx;
    int fps;
    int64_t frame_index;

    /*
     * 录像内部统一使用同一个媒体时间起点。
     * 视频和音频都挂到这条时间轴上，避免各算各的相对时间。
     */
    uint64_t media_base_timestamp_us;
    int have_media_base_timestamp;

    /* 视频输入 PTS 单调性保护。 */
    int64_t last_input_pts;

    /*
     * 第一块音频到达时记录锚点：
     * - capture_time_us: 这块音频对应的媒体时刻
     * - first_frame_index: 这块音频在整条 PCM 流中的起始 frame 序号
     */
    uint64_t audio_anchor_capture_time_us;
    uint64_t audio_anchor_first_frame_index;
    int have_audio_anchor;

    /* 下一个待编码音频帧的 pts，单位是 audio time_base。 */
    int64_t audio_next_pts;

    SDL_mutex *mutex;

    int enabled;
    int accepting_frames;
    int fatal_error;

    SDL_Thread *thread;
    FrameQueue queue;
    AudioQueue audio_queue;

    uint64_t frames_encoded;
    uint64_t audio_frames_encoded;
    uint64_t audio_chunks_consumed;
} RecordState;


//ALSA音频采集状态
typedef struct AudioCaptureState {
    char device_name[128];

    snd_pcm_t *pcm;
    snd_pcm_format_t sample_format;

    unsigned int sample_rate;
    int channels;

    snd_pcm_uframes_t period_frames;
    snd_pcm_uframes_t buffer_frames;

    size_t bytes_per_sample;
    size_t bytes_per_frame;

    unsigned char *period_buffer;
    size_t period_buffer_bytes;

    SDL_mutex *mutex;
    SDL_Thread *thread;

    int enabled;
    int running;
    int fatal_error;

    uint64_t chunks_captured;
    uint64_t pcm_frames_captured;
    uint64_t xruns;

    uint64_t last_capture_time_us;
    uint64_t last_chunk_frames;
} AudioCaptureState;

//应用总状态
typedef struct AppState {
    char device_path[256];
    int fd;

    int width;
    int height;
    uint32_t pixfmt;

    Buffer *buffers;
    unsigned int n_buffers;

    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;

    SDL_Thread *capture_tid;
    SharedFrame latest;

    CameraControl controls[MAX_CONTROLS];
    int control_count;
    int current_control;

    StreamState stream;
    RecordState record;
    AudioCaptureState audio;

    /* 采集线程私有临时缓冲，不直接暴露给显示线程。 */
    unsigned char *preview_rgb;
    size_t preview_rgb_bytes;

    unsigned char *capture_yuyv;
    size_t capture_yuyv_bytes;

    /*
     * 这是用户层面的运行开关。
     * 例如模块可能已经 init 成功(enabled=1)，
     * 但用户当前不希望推流(stream_on=0)。
     */
    int stream_on;
    int record_on;

    uint32_t bytesperline;
    uint32_t sizeimage;
    uint64_t frames_captured;
    uint64_t frames_dropped;
    uint64_t last_stats_ms;

    int quit;
    int paused;

    /*
     * 应用统一媒体时钟。
     * 暂停期间的时间会被扣除，音频/视频都依赖它落到同一时间域。
     */
    SDL_mutex *clock_mutex;
    uint64_t pause_begin_us;
    uint64_t total_paused_us;

    uint64_t paused_video_frames_discarded;
    uint64_t paused_audio_chunks_discarded;
    uint64_t paused_audio_frames_discarded;
} AppState;

#endif