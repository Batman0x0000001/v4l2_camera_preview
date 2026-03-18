#ifndef APP_H
#define APP_H

#include<stdint.h>
#include<linux/videodev2.h>
#include<SDL2/SDL.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include"media_frame.h"
#include"frame_queue.h"

typedef struct Buffer{
    void *start;
    size_t length;
}Buffer;

typedef struct SharedFrame{
    unsigned char *rgb;
    int width;
    int height;
    size_t bytes;
    uint64_t frame_id;
    CaptureMeta meta;//采集元信息
    SDL_mutex *mutex;
}SharedFrame;

#define MAX_CONTROLS 128

typedef struct{
    uint32_t id;
    char name[64];
    int type;

    int min;
    int max;
    int step;
    int def;
}CameraControl;

typedef struct StreamState{
    char output_url[512];

    const AVCodec *encoder;
    AVCodecContext *enc_ctx;
    AVFormatContext *ofmt_ctx;
    AVStream *video_st;

    struct SwsContext *sws_ctx;

    AVFrame *yuv_frame;
    AVPacket *pkt;

    int fps;
    int64_t frame_index;
    uint64_t base_timestamp_us;
    int have_base_timestamp;
    int64_t last_input_pts;

    SDL_mutex *mutex;
    int enabled;
    int accepting_frames;
    int fatal_error;

    SDL_Thread *thread;
    FrameQueue queue;

    uint64_t frames_encoded;
}StreamState;

typedef struct RecordState{
    char output_path[512];

    const AVCodec *encoder;
    AVCodecContext *enc_ctx;

    AVFormatContext *ofmt_ctx;
    AVStream *video_st;

    /*
        不透明结构体  不知道这个结构体有多大，无法分配内存
        而指针大小固定是8字节，可以声明
    */
    struct SwsContext *sws_ctx;

    AVFrame *yuv_frame;
    AVPacket *pkt;

    int fps;
    int64_t frame_index;
    uint64_t base_timestamp_us;
    int have_base_timestamp;
    int64_t last_input_pts;

    SDL_mutex *mutex;
    int enabled;
    int accepting_frames;
    int fatal_error;

    SDL_Thread *thread;
    FrameQueue queue;

    uint64_t frames_encoded;
}RecordState;

typedef struct AppState{
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

    //采集线程私有RGB临时缓冲
    unsigned char *preview_rgb;
    size_t preview_rgb_bytes;

    unsigned char *capture_yuyv;
    size_t capture_yuyv_bytes;

    int stream_on;
    int record_on;

    uint32_t bytesperline;
    uint32_t sizeimage;
    uint64_t frames_captured;
    uint64_t frames_dropped;
    uint64_t last_stats_ms;

    int quit;
    int paused;
}AppState;

#endif