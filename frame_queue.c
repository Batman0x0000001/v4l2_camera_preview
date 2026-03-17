#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include"frame_queue.h"

int frame_packet_init(FramePacket *pkt,size_t capacity,int width,int height,uint32_t pixfmt){
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
    pkt->width = width;
    pkt->height = height;
    pkt->pixfmt = pixfmt;

    return 0;
}

void frame_packet_free(FramePacket *pkt){
    if(!pkt){
        return;
    }

    free(pkt->data);
    pkt->data = NULL;
    pkt->capacity = 0;
    pkt->bytes = 0;
    pkt->width = 0;
    pkt->height = 0;
    pkt->pixfmt = 0;
    pkt->frame_id = 0;
    pkt->meta.sequence = 0;
    pkt->meta.bytesused = 0;
    pkt->meta.timestamp_us = 0;
}

int frame_queue_init(FrameQueue *q,int capacity,size_t frame_bytes,int width,int height,uint32_t pixfmt){
    if(!q || capacity <= 0 || frame_bytes == 0){
        return -1;
    }

    memset(q,0,sizeof(*q));
    q->slots = (FramePacket *)calloc((size_t)capacity,sizeof(FramePacket));
    if(!q->slots){
        perror("calloc");
        return -1;
    }

    q->capacity = capacity;
    q->size = 0;
    q->read_index = 0;
    q->write_index = 0;
    q->dropped_frames = 0;
    q->stop_request = 0;

    for (int i = 0; i < capacity; i++)
    {
        if(frame_packet_init(&q->slots[i],frame_bytes,width,height,pixfmt) < 0){
            for (int j = 0; j < i; j++)
            {
                frame_packet_free(&q->slots[j]);
                free(q->slots);
                q->slots = NULL;
                return -1;
            }
            
        }
    }

    q->mutex = SDL_CreateMutex();
    if(!q->mutex){
        fprintf(stderr, "SDL_CreateMutex failed:%s\n",SDL_GetError() );
        return -1;
    }

    q->not_empty = SDL_CreateCond();
    if(!q->not_empty){
        fprintf(stderr, "SDL_CreateCond failed\n:%s", SDL_GetError());
        return -1;
    }

    return 0;
}

void frame_queue_stop(FrameQueue *q){
    if(!q || !q->mutex){
        return;
    }

    SDL_LockMutex(q->mutex);
    q->stop_request = 1;
    SDL_CondBroadcast(q->not_empty);
    SDL_UnlockMutex(q->mutex);
}

void frame_queue_destroy(FrameQueue *q){
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
            frame_packet_free(&q->slots[i]);
        }
        free(q->slots);
        q->slots = NULL;
    }

    q->capacity = 0;
    q->size = 0;
    q->read_index = 0;
    q->write_index = 0;
    q->dropped_frames = 0;
    q->stop_request = 0;
}

int frame_queue_push(FrameQueue *q,const uint8_t *data,size_t bytes,int width,int height,uint32_t pixfmt,uint64_t frame_id,const CaptureMeta *meta){
    FramePacket *slot;

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
        q->dropped_frames++;
    }

    slot = &q->slots[q->write_index];

    if(bytes > slot->capacity){
        SDL_UnlockMutex(q->mutex);
        return -1;
    }

    memcpy(slot->data,data,bytes);
    slot->bytes = bytes;
    slot->width = width;
    slot->height = height;
    slot->pixfmt = pixfmt;
    slot->frame_id = frame_id;
    slot->meta = *meta;

    q->write_index = (q->write_index + 1) % q->capacity;
    q->size++;

    SDL_CondSignal(q->not_empty);
    SDL_UnlockMutex(q->mutex);

    return 0;
}

FrameQueuePopResult frame_queue_pop(FrameQueue *q,FramePacket *out,int timeout_ms){
    int wait_ret;
    FramePacket *slot;

    if(!q || !out || !out->data || !q->mutex){
        return FRAME_QUEUE_POP_ERROR;
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
            return FRAME_QUEUE_POP_TIMEOUT;
        }

        if(wait_ret != 0){
            SDL_UnlockMutex(q->mutex);
            return FRAME_QUEUE_POP_ERROR;
        }
    }

    if(q->size == 0 && q->stop_request){
        SDL_UnlockMutex(q->mutex);
        return FRAME_QUEUE_POP_STOPPED;
    }

    slot = &q->slots[q->read_index];

    if(slot->bytes > out->capacity){
        SDL_UnlockMutex(q->mutex);
        return FRAME_QUEUE_POP_ERROR;
    }

    memcpy(out->data,slot->data,slot->bytes);
    out->bytes = slot->bytes;
    out->frame_id = slot->frame_id;
    out->meta = slot->meta;
    out->width = slot->width;
    out->height = slot->height;
    out->pixfmt = slot->pixfmt;

    q->read_index = (q->read_index + 1) % q->capacity;
    q->size--;

    SDL_UnlockMutex(q->mutex);
    return FRAME_QUEUE_POP_OK;
}