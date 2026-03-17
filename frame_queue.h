#ifndef FRAME_QUEUE_H
#define FRAME_QUEUE_H

#include<stdint.h>
#include<SDL2/SDL.h>
#include"media_frame.h"

typedef enum FrameQueuePopResult{
    FRAME_QUEUE_POP_OK = 0,
    FRAME_QUEUE_POP_TIMEOUT = 1,
    FRAME_QUEUE_POP_STOPPED = 2,
    FRAME_QUEUE_POP_ERROR = -1, 
}FrameQueuePopResult;

typedef struct FrameQueue{
    FramePacket *slots;
    int capacity;
    int size;
    int read_index;
    int write_index;
    uint64_t dropped_frames;
    int stop_request;
    SDL_mutex *mutex;
    SDL_cond *not_empty;
}FrameQueue;

int frame_packet_init(FramePacket *pkt,size_t capacity,int width,int height);
void frame_packet_free(FramePacket *pkt);

int frame_queue_init(FrameQueue *q,int capacity,size_t frame_bytes,int width,int height);
void frame_queue_stop(FrameQueue *q);
void frame_queue_destroy(FrameQueue *q);
int frame_queue_push(FrameQueue *q,const uint8_t *rgb,size_t bytes,uint64_t frame_id,const CaptureMeta *meta);
FrameQueuePopResult frame_queue_pop(FrameQueue *q,FramePacket *out,int timeout_ms);

#endif