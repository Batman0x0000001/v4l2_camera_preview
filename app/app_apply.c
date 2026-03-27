#include <stdio.h>

#include "app_apply.h"

/*
 * 把任意非 0 值收敛成标准布尔 1。
 * 这样外部配置即使写成 2 / -1，也不会污染运行态语义。
 */
static int app_normalize_flag(int value)
{
    return value ? 1 : 0;
}

/*
 * 如果配置值非法（<= 0），则保留当前默认值。
 * 这样 app_state_init() 先设置的安全默认值不会被错误配置冲掉。
 */
static int app_choose_positive_or_default(int value, int current_default)
{
    if (value > 0) {
        return value;
    }
    return current_default;
}

int app_cfg_stream_enabled_on_start(const AppConfig *cfg)
{
    if (!cfg) {
        return 0;
    }

    return app_normalize_flag(cfg->start_stream_on);
}

int app_cfg_record_session_should_autostart(const AppConfig *cfg)
{
    if (!cfg) {
        return 0;
    }

    /*
     * 这一层只做语义收敛：
     * - start_record_on
     * - auto_record_on_start
     *
     * 二者当前都可以理解为“启动成功后要显式开始录像会话”。
     */
    return app_normalize_flag(cfg->start_record_on ||
                              cfg->auto_record_on_start);
}

void app_apply_config(AppState *app, const AppConfig *cfg)
{
    if (!app || !cfg) {
        return;
    }

    /*
     * device_path 属于 AppState 本体，直接覆盖。
     * 但如果配置为空字符串，则保留 app_state_init() 已有默认值。
     */
    if (cfg->device_path[0] != '\0') {
        snprintf(app->device_path,
                 sizeof(app->device_path),
                 "%s",
                 cfg->device_path);
    }

    /*
     * width / height 只接受正数；
     * 非法值时保留 app_state_init() 设下的安全默认值。
     */
    app->width = app_choose_positive_or_default(cfg->width, app->width);
    app->height = app_choose_positive_or_default(cfg->height, app->height);

    /*
     * 推流开关可以直接映射为“用户启动意图”。
     * 真正能否工作，还要依赖 stream.enabled / fatal_error 等运行时状态。
     */
    app->stream_on = app_cfg_stream_enabled_on_start(cfg);

    /*
     * 录像不能在这里直接写成 record_on = 1。
     *
     * 原因：
     * 1. record_on 不是单纯的配置位，它和录像会话生命周期耦合；
     * 2. 真正的录像开始动作应该通过 record_session_start()
     *    或 app_toggle_record() 建立完整会话；
     * 3. 如果在启动前先把 record_on 置 1，
     *    采集线程可能开始向录像队列投递数据，
     *    但录像文件/编码器/时间基会话还没真正建立，语义会很乱。
     *
     * 所以这里统一清零。
     * “是否开机自动录像”由 main.c 在 app_startup() 成功后显式触发。
     */
    app->record_on = 0;
}