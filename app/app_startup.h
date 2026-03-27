#ifndef APP_STARTUP_H
#define APP_STARTUP_H

#include "app.h"

/*
 * 启动模块职责：
 * 1. 负责把各子模块按顺序拉起；
 * 2. 负责在失败时统一回滚；
 * 3. 负责应用退出时统一关闭。
 *
 * 注意：
 * - 这里只管理“应用生命周期”；
 * - 不负责具体的 V4L2 / ALSA / 推流 / 录像业务细节。
 */

int app_startup(AppState *app);
void app_shutdown(AppState *app);

/*
 * 兼容旧拼写，避免当前 main.c 立刻失效。
 * 等后续把调用点全部改成 app_shutdown() 后，再删除这个兼容层。
 */
static inline void app_shutdowm(AppState *app)
{
    app_shutdown(app);
}

#endif