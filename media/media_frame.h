#ifndef MEDIA_FRAME_H
#define MEDIA_FRAME_H

#include <stddef.h>
#include <stdint.h>

/*
 * 视频采集元信息
 *
 * 语义：
 * - sequence:
 *     来自 V4L2 buffer.sequence，用于观察驱动层帧序号和丢帧情况。
 *
 * - bytesused:
 *     当前这帧实际有效负载大小，通常来自 V4L2 buffer.bytesused。
 *
 * - capture_time_us:
 *     统一媒体时钟下，这帧“进入应用层”的时刻。
 *     后续推流和录像都以它为视频 PTS 推导基础。
 *
 * - device_time_us:
 *     设备原始时间戳，当前主要用于调试与对照。
 *     对 V4L2 来说通常来自 struct v4l2_buffer.timestamp。
 */
typedef struct CaptureMeta {
    uint32_t sequence;
    uint32_t bytesused;

    uint64_t capture_time_us;
    uint64_t device_time_us;
} CaptureMeta;

/*
 * 视频帧包
 *
 * 用途：
 * - 作为 frame_queue 中的槽位对象
 * - 在采集线程 -> 推流线程 / 录像线程之间传递
 *
 * 说明：
 * - data/capacity 管理的是原始视频数据缓存
 * - bytes 表示本次有效数据大小
 * - width/height/stride/pixfmt 描述当前这帧的像素布局
 * - frame_id 是应用层“最新帧序号”，不是设备 sequence
 */
typedef struct FramePacket {
    uint8_t *data;

    uint32_t pixfmt;

    size_t capacity;
    size_t bytes;

    int width;
    int height;
    int stride;

    uint64_t frame_id;
    CaptureMeta meta;
} FramePacket;

/*
 * 音频采集元信息
 *
 * 语义：
 * - capture_time_us:
 *     统一媒体时钟下，这块 PCM 数据进入应用层的时刻。
 *
 * - first_frame_index:
 *     这块音频在整条采集 PCM 流中的起始 frame 序号。
 *     例如：
 *         第 0 块从 0 开始，
 *         下一块从上一块累计 frame 数后继续递增。
 *     录像模块会结合它和 capture_time_us 推导音频时间线。
 *
 * - frames:
 *     当前 chunk 内包含多少个 PCM frame。
 *     注意不是字节数。
 */
typedef struct AudioMeta {
    uint64_t capture_time_us;
    uint64_t first_frame_index;
    uint32_t frames;
} AudioMeta;

/*
 * 音频块包
 *
 * 用途：
 * - 作为 audio_queue 中的槽位对象
 * - 在音频采集线程 -> 推流线程 / 录像线程之间传递
 *
 * 说明：
 * - data/capacity 管理一块 PCM 缓冲
 * - bytes 是当前有效字节数
 * - bytes_per_sample / bytes_per_frame 用于下游计算样本布局
 * - chunk_id 是应用层递增音频块编号，便于日志和调试
 */
typedef struct AudioPacket {
    uint8_t *data;

    size_t capacity;
    size_t bytes;

    unsigned int sample_rate;
    int channels;

    size_t bytes_per_sample;
    size_t bytes_per_frame;

    uint64_t chunk_id;
    AudioMeta meta;
} AudioPacket;

#endif