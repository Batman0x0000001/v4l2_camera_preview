#ifndef FRAME_QUEUE_H
#define FRAME_QUEUE_H

#include <stdint.h>

#include <SDL2/SDL.h>

#include "media_frame.h"

typedef enum FrameQueuePopResult {
    FRAME_QUEUE_POP_OK = 0,
    FRAME_QUEUE_POP_TIMEOUT = 1,
    FRAME_QUEUE_POP_STOPPED = 2,
    FRAME_QUEUE_POP_ERROR = -1,
} FrameQueuePopResult;

/*
 * 固定容量环形视频帧队列：
 * - 满时丢弃最旧帧，保留最新帧
 * - stop_request 置位后，push 直接忽略，pop 在队列清空后返回 STOPPED
 */
typedef struct FrameQueue {
    FramePacket *slots;

    int capacity;
    int size;
    int read_index;
    int write_index;

    uint64_t dropped_frames;
    int stop_request;

    SDL_mutex *mutex;
    SDL_cond *not_empty;
} FrameQueue;

int frame_packet_init(FramePacket *pkt,
                      size_t capacity,
                      int width,
                      int height,
                      int stride,
                      uint32_t pixfmt);
void frame_packet_free(FramePacket *pkt);

int frame_queue_init(FrameQueue *q,
                     int capacity,
                     size_t frame_bytes,
                     int width,
                     int height,
                     int stride,
                     uint32_t pixfmt);
void frame_queue_stop(FrameQueue *q);
void frame_queue_destroy(FrameQueue *q);

int frame_queue_push(FrameQueue *q,
                     const uint8_t *data,
                     size_t bytes,
                     int width,
                     int height,
                     int stride,
                     uint32_t pixfmt,
                     uint64_t frame_id,
                     const CaptureMeta *meta);

FrameQueuePopResult frame_queue_pop(FrameQueue *q,
                                    FramePacket *out,
                                    int timeout_ms);

void frame_queue_flush(FrameQueue *q);

#endif