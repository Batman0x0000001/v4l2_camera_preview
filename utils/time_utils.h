#ifndef TIME_UTILS_H
#define TIME_UTILS_H

#include <stdint.h>

/*
 * 时间工具模块职责：
 * 1. 提供单调时钟（monotonic clock）的统一微秒接口；
 * 2. 避免各模块重复手写 clock_gettime()。
 *
 * 约定：
 * - 返回值单位统一为微秒（us）
 * - 使用 CLOCK_MONOTONIC，适合做媒体时间线和时长统计
 * - 不用于真实日历时间或文件名时间戳
 */

uint64_t app_now_monotonic_us(void);

#endif