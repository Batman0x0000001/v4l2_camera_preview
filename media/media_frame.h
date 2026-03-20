#ifndef MEDIA_FRAME_H
#define MEDIA_FRAME_H

#include<stddef.h>
#include<stdint.h>

typedef struct CaptureMeata{
    uint32_t sequence;
    uint32_t bytesused;

    /*
    统一采集时间戳：使用 CLOCK_MONOTONIC 记录“这份媒体数据进入应用层”的时刻。
    后续视频和音频都基于它做相对 PTS 计算，保证两者处于同一时钟域。
    */
    uint64_t capture_time_us;

    /*
    设备原始时间戳：当前主要给视频保留调试信息。
    对 V4L2 来说来自 struct v4l2_buffer.timestamp。
    以后做更深入的设备级同步时可以继续利用它。
    */
    uint64_t device_time_us;
}CaptureMeta;

typedef struct FramePacket{
    uint8_t *data;
    uint32_t pixfmt;
    size_t capacity;
    size_t bytes;
    int width;
    int height;
    int stride;
    uint64_t frame_id;
    CaptureMeta meta;
}FramePacket;

#endif