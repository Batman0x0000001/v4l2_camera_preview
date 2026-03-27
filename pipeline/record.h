#ifndef RECORD_H
#define RECORD_H

#include "app.h"

/*
 * 录像模块职责：
 * 1. 初始化录像线程与输入队列；
 * 2. 管理录像会话的启动 / 停止；
 * 3. 将视频帧与音频块编码并复用到 MP4；
 * 4. 在暂停、停止、关闭时维护时间线一致性。
 *
 * 设计说明：
 * - record.enabled:
 *     模块级资源（mutex / worker thread / queues）是否已就绪。
 *
 * - record.accepting_frames:
 *     当前录像会话是否接收新的音视频输入。
 *     只有在 session_active=1 且未进入 stopping/fatal 状态时才应为 1。
 *
 * - record.session_active:
 *     当前是否存在一个已经成功打开输出文件并写入 header 的录像会话。
 *
 * - record_on:
 *     这是 AppState 上的“用户意图开关”，不等价于 session_active。
 *     真正开始录像要通过 record_session_start() 建立完整会话。
 */

void record_state_init(AppState *app, const char *output_dir, int fps);

/*
 * 初始化录像模块本体：
 * - 创建 mutex
 * - 初始化视频输入队列
 * - 启动录像 worker 线程
 *
 * 注意：
 * 这里只初始化“模块级常驻资源”，
 * 不会立即打开 MP4 文件，也不会立即创建编码器会话。
 */
int record_init(AppState *app);

/*
 * 关闭整个录像模块：
 * - 如有活动会话，先停止会话并写尾
 * - 停止队列
 * - 等待 worker 线程退出
 * - 释放模块级资源
 */
void record_close(AppState *app);

/*
 * 显式启动一个新的录像会话：
 * - 生成新的输出文件名
 * - 创建音视频编码器
 * - 创建 muxer 并写入 header
 * - 清空旧队列残留
 * - 开始接收新的音视频数据
 *
 * 返回值：
 *   0  成功，或当前已经处于 active/stopping 状态时的幂等返回
 *  <0  失败
 */
int record_session_start(AppState *app);

/*
 * 显式停止当前录像会话：
 * - 停止继续接收新帧
 * - flush 队列与编码器
 * - 写 trailer
 * - 关闭输出文件并释放会话资源
 *
 * 返回值：
 *   0  成功，或当前本就没有活动会话时的幂等返回
 *  <0  失败
 */
int record_session_stop(AppState *app);

/*
 * 在应用进入 pause 时通知录像模块：
 * - 清空输入队列
 * - 清空音频 FIFO
 * - 重置音频锚点，避免暂停前后时间线串接
 */
void record_notify_pause(AppState *app);

#endif