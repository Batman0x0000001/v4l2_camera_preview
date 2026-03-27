#ifndef APP_CTRL_H
#define APP_CTRL_H

#include "app.h"

/*
 * 控制层职责：
 * 1. 对外提供“用户动作”入口，例如暂停、推流开关、录像开关；
 * 2. 打印运行状态和控制项状态；
 * 3. 提供截图这种轻量级应用级操作。
 *
 * 注意：
 * - 这里只做“控制与展示”；
 * - 不负责底层 V4L2 / ALSA / 编码器资源管理。
 */

void app_print_help(void);
void app_print_runtime_state(const AppState *app);
void app_print_current_control_status(AppState *app);
void app_print_module_overview(const AppState *app);

void app_toggle_pause(AppState *app);
void app_toggle_stream(AppState *app);
void app_toggle_record(AppState *app);

void app_select_next_control(AppState *app);
void app_select_prev_control(AppState *app);
void app_adjust_current_control(AppState *app, int delta);

int app_save_snapshot(AppState *app, const char *path);

#endif