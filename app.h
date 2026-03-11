#ifndef APP_H
#define APP_H

#include<stdint.h>
#include<linux/videodev2.h>
#include<SDL2/SDL.h>

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

    int quit;
    int paused;
}AppState;

#endif