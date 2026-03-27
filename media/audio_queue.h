#ifndef AUDIO_QUEUE_H
#define AUDIO_QUEUE_H

#include <stdint.h>
#include <stddef.h>
#include <SDL2/SDL.h>

typedef struct AudioMeta{
    /*
        统一采集时间戳，和视频一样使用 CLOCK_MONOTONIC 对齐后的时间基。
    */
    uint64_t capture_time_us;

    /*
        这一块 PCM 在整条音频流中的起始 sample-frame 序号。
        例如双声道 48kHz 下，1 个 sample-frame = 1 个时刻的 LR 样本对。
    */
    uint64_t first_frame_index;

    /*
        当前这块 PCM 含有多少个 sample-frame。
    */
    uint32_t frames;
}AudioMeta;

typedef struct AudioPacket{
    uint8_t *data;
    size_t capacity;
    size_t bytes;

    unsigned int sample_rate;
    int channels;

    size_t bytes_per_sample;
    size_t bytes_per_frame;

    uint64_t chunk_id;
    AudioMeta meta;
}AudioPacket;

typedef enum AudioQueuePopResult{
    AUDIO_QUEUE_POP_OK = 0,
    AUDIO_QUEUE_POP_TIMEOUT = 1,
    AUDIO_QUEUE_POP_STOPPED = 2,
    AUDIO_QUEUE_POP_ERROR = -1,
}AudioQueuePopResult;

typedef struct AudioQueue{
    AudioPacket *slots;

    int capacity;
    int size;
    int read_index;
    int write_index;

    uint64_t dropped_chunks;
    int stop_request;

    SDL_mutex *mutex;
    SDL_cond *not_empty;
}AudioQueue;

int audio_packet_init(AudioPacket *pkt,size_t capacity);
void audio_packet_free(AudioPacket *pkt);

int audio_queue_init(AudioQueue *q,int capacity,size_t packet_bytes);
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

AudioQueuePopResult audio_queue_pop(AudioQueue *q,AudioPacket *out,int timeout_ms);

void audio_queue_flush(AudioQueue *q);
#endif