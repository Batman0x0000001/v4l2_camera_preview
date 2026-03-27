#ifndef AUDIO_QUEUE_H
#define AUDIO_QUEUE_H

#include <stdint.h>

#include <SDL2/SDL.h>

#include "media_frame.h"

typedef enum AudioQueuePopResult {
    AUDIO_QUEUE_POP_OK = 0,
    AUDIO_QUEUE_POP_TIMEOUT = 1,
    AUDIO_QUEUE_POP_STOPPED = 2,
    AUDIO_QUEUE_POP_ERROR = -1,
} AudioQueuePopResult;

/*
 * 固定容量环形音频块队列：
 * - 满时丢弃最旧 chunk，优先保留最新音频
 * - stop_request 置位后，push 直接忽略
 * - pop 在 stop 且队列清空后返回 STOPPED
 */
typedef struct AudioQueue {
    AudioPacket *slots;

    int capacity;
    int size;
    int read_index;
    int write_index;

    uint64_t dropped_chunks;
    int stop_request;

    SDL_mutex *mutex;
    SDL_cond *not_empty;
} AudioQueue;

int audio_packet_init(AudioPacket *pkt, size_t capacity);
void audio_packet_free(AudioPacket *pkt);

int audio_queue_init(AudioQueue *q, int capacity, size_t chunk_capacity);
void audio_queue_stop(AudioQueue *q);
void audio_queue_destroy(AudioQueue *q);

int audio_queue_push(AudioQueue *q,
                     const uint8_t *data,
                     size_t bytes,
                     unsigned int sample_rate,
                     int channels,
                     size_t bytes_per_sample,
                     size_t bytes_per_frame,
                     uint64_t chunk_id,
                     const AudioMeta *meta);

AudioQueuePopResult audio_queue_pop(AudioQueue *q,
                                    AudioPacket *out,
                                    int timeout_ms);

void audio_queue_flush(AudioQueue *q);

#endif