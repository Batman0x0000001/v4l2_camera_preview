#ifndef DISPLAY_H
#define DISPLAY_H

#include "app.h"

/*
 * 显示模块职责：
 * 1. 初始化 SDL 窗口、renderer、texture；
 * 2. 把 latest.rgb 显示到窗口；
 * 3. 提供一个仅基于 latest.rgb 的轻量级 PPM 保存接口；
 * 4. 释放 SDL 显示资源。
 *
 * 注意：
 * - 这里只处理“显示与 latest 读取”；
 * - 不负责采集线程，也不负责截图目录与文件名生成。
 */

int display_init(AppState *app);
void display_destroy(AppState *app);

int display_present_latest(AppState *app);
int display_save_latest_ppm(AppState *app, const char *filename);

#endif