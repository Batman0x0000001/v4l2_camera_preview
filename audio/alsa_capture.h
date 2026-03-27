#ifndef ALSA_CAPTURE_H
#define ALSA_CAPTURE_H

#include "app.h"

/*
 * 音频采集模块职责：
 * 1. 初始化 ALSA 采集配置状态；
 * 2. 打开 PCM 设备并启动采集线程；
 * 3. 把采集到的 PCM 块分发给推流/录像模块；
 * 4. 在退出时安全关闭音频线程和 PCM 设备。
 */

void audio_state_init(AppState *app,
                      const char *device_name,
                      unsigned int sample_rate,
                      int channels,
                      unsigned int period_frames);

int audio_init(AppState *app);
void audio_close(AppState *app);

#endif