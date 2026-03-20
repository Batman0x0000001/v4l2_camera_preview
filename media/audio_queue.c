#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio_queue.h"

int audio_packet_init(AudioPacket *pkt,size_t capacity){
    if(!pkt || capacity == 0){
        return -1;
    }

    memset(pkt,0,sizeof(*pkt));

    pkt->data = (uint8_t *)malloc(capacity);
    if(!pkt->data){
        perror("malloc");
        return -1;
    }

    pkt->capacity = capacity;
    return 0;
}

void audio_packet_free(AudioPacket *pkt){
    if(!pkt){
        return;
    }

    free(pkt->data);
    pkt->data = NULL;

    pkt->capacity = 0;
    pkt->bytes = 0;
    pkt->sample_rate = 0;
    pkt->channels = 0;
    pkt->bytes_per_sample = 0;
    pkt->bytes_per_frame = 0;
    pkt->chunk_id = 0;

    pkt->meta.capture_time_us = 0;
    pkt->meta.first_frame_index = 0;
    pkt->meta.frames = 0;
}

int audio_queue_init(AudioQueue *q,int capacity,size_t packet_bytes){
    if(!q || capacity <= 0 || packet_bytes == 0){
        return -1;
    }

    memset(q,0,sizeof(*q));

    q->slots = (AudioPacket *)calloc((size_t)capacity,sizeof(AudioPacket));
    if(!q->slots){
        perror("calloc");
        return -1;
    }

    q->capacity = capacity;
    q->size = 0;
    q->read_index = 0;
    q->write_index = 0;
    q->dropped_chunks = 0;
    q->stop_request = 0;

    for(int i = 0; i < capacity; ++i){
        if(audio_packet_init(&q->slots[i],packet_bytes) < 0){
            for(int j = 0; j < i; ++j){
                audio_packet_free(&q->slots[j]);
            }
            free(q->slots);
            q->slots = NULL;
            return -1;
        }
    }

    q->mutex = SDL_CreateMutex();
    if(!q->mutex){
        fprintf(stderr,"SDL_CreateMutex (audio_queue) failed: %s\n",SDL_GetError());
        audio_queue_destroy(q);
        return -1;
    }

    q->not_empty = SDL_CreateCond();
    if(!q->not_empty){
        fprintf(stderr,"SDL_CreateCond (audio_queue) failed: %s\n",SDL_GetError());
        audio_queue_destroy(q);
        return -1;
    }

    return 0;
}

void audio_queue_stop(AudioQueue *q){
    if(!q || !q->mutex){
        return;
    }

    SDL_LockMutex(q->mutex);
    q->stop_request = 1;
    SDL_CondBroadcast(q->not_empty);
    SDL_UnlockMutex(q->mutex);
}

void audio_queue_destroy(AudioQueue *q){
    if(!q){
        return;
    }

    if(q->not_empty){
        SDL_DestroyCond(q->not_empty);
        q->not_empty = NULL;
    }

    if(q->mutex){
        SDL_DestroyMutex(q->mutex);
        q->mutex = NULL;
    }

    if(q->slots){
        for(int i = 0; i < q->capacity; ++i){
            audio_packet_free(&q->slots[i]);
        }
        free(q->slots);
        q->slots = NULL;
    }

    q->capacity = 0;
    q->size = 0;
    q->read_index = 0;
    q->write_index = 0;
    q->dropped_chunks = 0;
    q->stop_request = 0;
}

int audio_queue_push(AudioQueue *q,
                     const uint8_t *data,
                     size_t bytes,
                     unsigned int sample_rate,
                     int channels,
                     size_t bytes_per_sample,
                     size_t bytes_per_frame,
                     uint64_t chunk_id,
                     const AudioMeta *meta){
    AudioPacket *slot;

    if(!q || !data || !meta || !q->mutex){
        return -1;
    }

    SDL_LockMutex(q->mutex);

    if(q->stop_request){
        SDL_UnlockMutex(q->mutex);
        return 0;
    }

    if(q->size == q->capacity){
        q->read_index = (q->read_index + 1) % q->capacity;
        q->size--;
        q->dropped_chunks++;
    }

    slot = &q->slots[q->write_index];

    if(bytes > slot->capacity){
        SDL_UnlockMutex(q->mutex);
        return -1;
    }

    memcpy(slot->data,data,bytes);
    slot->bytes = bytes;
    slot->sample_rate = sample_rate;
    slot->channels = channels;
    slot->bytes_per_sample = bytes_per_sample;
    slot->bytes_per_frame = bytes_per_frame;
    slot->chunk_id = chunk_id;
    slot->meta = *meta;

    q->write_index = (q->write_index + 1) % q->capacity;
    q->size++;

    SDL_CondSignal(q->not_empty);
    SDL_UnlockMutex(q->mutex);

    return 0;
}

AudioQueuePopResult audio_queue_pop(AudioQueue *q,AudioPacket *out,int timeout_ms){
    int wait_ret;
    AudioPacket *slot;

    if(!q || !out || !out->data || !q->mutex){
        return AUDIO_QUEUE_POP_ERROR;
    }

    SDL_LockMutex(q->mutex);

    while(q->size == 0 && !q->stop_request){
        if(timeout_ms < 0){
            wait_ret = SDL_CondWait(q->not_empty,q->mutex);
        }else{
            wait_ret = SDL_CondWaitTimeout(q->not_empty,q->mutex,timeout_ms);
        }

        if(wait_ret == SDL_MUTEX_TIMEDOUT){
            SDL_UnlockMutex(q->mutex);
            return AUDIO_QUEUE_POP_TIMEOUT;
        }

        if(wait_ret != 0){
            SDL_UnlockMutex(q->mutex);
            return AUDIO_QUEUE_POP_ERROR;
        }
    }

    if(q->size == 0 && q->stop_request){
        SDL_UnlockMutex(q->mutex);
        return AUDIO_QUEUE_POP_STOPPED;
    }

    slot = &q->slots[q->read_index];

    if(slot->bytes > out->capacity){
        SDL_UnlockMutex(q->mutex);
        return AUDIO_QUEUE_POP_ERROR;
    }

    memcpy(out->data,slot->data,slot->bytes);
    out->bytes = slot->bytes;
    out->sample_rate = slot->sample_rate;
    out->channels = slot->channels;
    out->bytes_per_sample = slot->bytes_per_sample;
    out->bytes_per_frame = slot->bytes_per_frame;
    out->chunk_id = slot->chunk_id;
    out->meta = slot->meta;

    q->read_index = (q->read_index + 1) % q->capacity;
    q->size--;

    SDL_UnlockMutex(q->mutex);
    return AUDIO_QUEUE_POP_OK;
}