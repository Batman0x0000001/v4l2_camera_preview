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

    SDL_mutex *mutex;
    int enabled;
}StreamState;

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

    int quit;
    int paused;
}AppState;

#endif