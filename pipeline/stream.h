#ifndef STREAM_H
#define STREAM_H

#include "app.h"

/*
 * 推流模块职责：
 * 1. 初始化视频编码器与 RTSP 输出；
 * 2. 启动工作线程，从 frame_queue 中取视频帧；
 * 3. 将采集时间戳映射到编码器 PTS；
 * 4. 在关闭时安全 flush 编码器并释放资源。
 *
 * 当前版本仍然只处理视频推流。
 * stream.audio_queue 暂时由音频采集侧分发数据，但这里尚未消费音频。
 */

const char *stream_backend_name(StreamBackendType type);
void stream_state_init(AppState *app, const char *url, int fps,StreamBackendType backend_type);
int stream_init(AppState *app);
void stream_close(AppState *app);

#endif