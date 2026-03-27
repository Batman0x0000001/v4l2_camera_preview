#ifndef APP_CLOCK_H
#define APP_CLOCK_H

#include <stdint.h>

#include "app.h"

/*
 * 应用统一媒体时钟模块
 *
 * 职责：
 * 1. 维护 pause / resume 状态；
 * 2. 统计累计暂停时长；
 * 3. 提供“扣除暂停时间后的统一媒体时钟”；
 * 4. 给视频采集、音频采集、录像、推流提供同一时间域。
 *
 * 设计约定：
 * - app_media_clock_us() 返回的是：
 *       monotonic_now - total_paused_duration
 * - 暂停期间这条时钟不应继续推进到业务时间线上；
 * - 所有时间单位统一为微秒（us）。
 */

int app_clock_init(AppState *app);
void app_clock_destroy(AppState *app);

int app_is_paused(AppState *app);
void app_pause_begin(AppState *app);
void app_pause_end(AppState *app);

uint64_t app_total_paused_us(AppState *app);
uint64_t app_media_clock_us(AppState *app);

#endif