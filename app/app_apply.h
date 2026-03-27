#ifndef APP_APPLY_H
#define APP_APPLY_H

#include "app.h"

/*
 * app_apply 模块职责：
 * 1. 把 AppConfig 中属于 AppState 的配置安全地应用到运行态；
 * 2. 对启动开关做统一语义收敛；
 * 3. 不直接负责 stream/record/audio 子模块的独立配置初始化。
 *
 * 这里要特别注意：
 * - stream_on 可以视为“用户是否希望启动后立即推流”；
 * - record_on 不能简单等价于“启动后立即开始录像”，
 *   因为录像还依赖 record_session_start() 建立完整会话。
 */

void app_apply_config(AppState *app, const AppConfig *cfg);

/*
 * 是否在应用启动完成后自动进入“推流开启”状态。
 * 这是一个纯配置语义判断，不做任何副作用。
 */
int app_cfg_stream_enabled_on_start(const AppConfig *cfg);

/*
 * 是否在应用启动完成后自动启动“录像会话”。
 *
 * 这里统一收敛 start_record_on / auto_record_on_start 的语义：
 * 只要任意一个为真，都表示“启动成功后应显式启动录制会话”。
 */
int app_cfg_record_session_should_autostart(const AppConfig *cfg);

#endif