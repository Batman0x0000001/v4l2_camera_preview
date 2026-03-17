#ifndef MEDIA_FRAME_H
#define MEDIA_FRAME_H

#include<stddef.h>
#include<stdint.h>

typedef struct CaptureMeata{
    uint32_t sequence;
    uint32_t bytesused;
    uint64_t timestamp_us;
}CaptureMeta;

typedef struct FramePacket{
    uint8_t *data;
    uint32_t pixfmt;
    size_t capacity;
    size_t bytes;
    int width;
    int height;
    uint64_t frame_id;
    CaptureMeta meta;
}FramePacket;

#endif