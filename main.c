#include <stdio.h>
#include <string.h>

#include "app.h"
#include "alsa_capture.h"
#include "app_apply.h"
#include "app_config.h"
#include "app_ctrl.h"
#include "app_startup.h"
#include "display.h"
#include "log.h"
#include "path_utils.h"
#include "record.h"
#include "stream.h"
#include "v4l2_core.h"

/*
 * main.c 的职责应该尽量简单：
 * 1. 组装初始化顺序；
 * 2. 进入事件循环；
 * 3. 在退出时统一收尾。
 *
 * 它不应该承担具体业务细节，
 * 否则后面接 WebRTC / C++ 封装时会很难拆。
 */

static void app_print_startup_info(AppState *app, const AppConfig *cfg)
{
    (void)cfg;

    scan_controls(app);
    print_controls(app);

    app_print_help();
    app_print_runtime_state(app);
    app_print_current_control_status(app);
    app_print_module_overview(app);
}

static void app_handle_snapshot_request(AppState *app, const AppConfig *cfg)
{
    char snapshot_path[512];

    if (!app || !cfg) {
        return;
    }

    if (ensure_dir_exists(cfg->snapshot_dir) < 0) {
        LOG_WARN("ensure_dir_exists(snapshot_dir) failed");
        return;
    }

    if (make_snapshot_filename(snapshot_path,
                               sizeof(snapshot_path),
                               cfg->snapshot_dir) < 0) {
        LOG_WARN("make_snapshot_filename failed");
        return;
    }

    app_save_snapshot(app, snapshot_path);
}

static void app_handle_keydown(AppState *app, const AppConfig *cfg, SDL_Keycode key)
{
    if (!app || !cfg) {
        return;
    }

    switch (key) {
    case SDLK_s:
        app_handle_snapshot_request(app, cfg);
        break;

    case SDLK_t:
        app_toggle_stream(app);
        break;

    case SDLK_r:
        app_toggle_record(app);
        break;

    case SDLK_h:
        app_print_help();
        break;

    case SDLK_i:
        app_print_runtime_state(app);
        app_print_current_control_status(app);
        break;

    case SDLK_LEFT:
        app_adjust_current_control(app, -1);
        break;

    case SDLK_RIGHT:
        app_adjust_current_control(app, +1);
        break;

    case SDLK_UP:
        app_select_next_control(app);
        break;

    case SDLK_DOWN:
        app_select_prev_control(app);
        break;

    case SDLK_SPACE:
        app_toggle_pause(app);
        break;

    case SDLK_ESCAPE:
        app->quit = 1;
        break;

    default:
        break;
    }
}

static void app_pump_events(AppState *app, const AppConfig *cfg)
{
    SDL_Event event;

    if (!app || !cfg) {
        return;
    }

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            app->quit = 1;
            break;

        case SDL_KEYDOWN:
            app_handle_keydown(app, cfg, event.key.keysym.sym);
            break;

        default:
            break;
        }
    }
}

static void app_handle_runtime_failures(AppState *app)
{
    if (!app) {
        return;
    }

    if (app->stream_on && app->stream.fatal_error) {
        LOG_WARN("stream disabled due to fatal error");
        app->stream_on = 0;
    }

    if (app->record_on && app->record.fatal_error) {
        LOG_WARN("record disabled due to fatal error");
        app->record_on = 0;
    }

    if (app->audio.enabled && app->audio.fatal_error) {
        LOG_WARN("audio capture entered fatal error state");
    }
}

static int app_run_main_loop(AppState *app, const AppConfig *cfg)
{
    if (!app || !cfg) {
        return -1;
    }

    while (!app->quit) {
        app_pump_events(app, cfg);

        if (app->quit) {
            break;
        }

        if (display_present_latest(app) < 0) {
            LOG_ERROR("display_present_latest failed");
            return -1;
        }

        app_handle_runtime_failures(app);
    }
    return 0;
}

int main(void)
{
    AppState app;
    AppConfig cfg;
    int exit_code = 0;

    /*
     * 第一步：准备配置对象。
     */
    app_config_init_default(&cfg);

    app_print_banner();
    app_print_config(&cfg);

    /*
     * 第二步：先初始化 AppState 的基础默认值，
     * 再把配置覆盖进去。
     *
     * 原代码顺序是：
     *   app_apply_config(...)
     *   app_state_init(...)
     *
     * 那样会导致 app_state_init() 重新 memset 整个 AppState，
     * 从而把刚写进去的 width / height / stream_on / record_on 覆盖掉。
     *
     * 这一版先 app_state_init()，再 app_apply_config()，
     * 先在 main.c 层面把行为修正过来。
     */
    app_state_init(&app, cfg.device_path);
    app_apply_config(&app, &cfg);

    /*
     * 第三步：初始化各模块的“静态配置态”。
     * 这里只是填参数，不真正启动线程和设备。
     */
    stream_state_init(&app, cfg.stream_url, cfg.fps);

    record_state_init(&app, cfg.record_dir, cfg.fps);

    audio_state_init(&app,
                     cfg.audio_device,
                     cfg.audio_sample_rate,
                     cfg.audio_channels,
                     cfg.audio_period_frames);

    /*
     * 第四步：真正拉起应用运行时资源。
     */
    if (app_startup(&app) < 0) {
        exit_code = 1;
        goto shutdown;
    }

    app_print_startup_info(&app, &cfg);

    /*
     * 自动录制开关保留原有行为。
     * 注意它和 start_record_on 仍然有语义重叠，
     * 这个问题放到后续整理 app_config / app_apply 时再统一收敛。
     */
    if (app_cfg_record_session_should_autostart(&cfg)) {
        app_toggle_record(&app);
    }

    if (app_run_main_loop(&app, &cfg) < 0) {
        exit_code = 1;
    }

shutdown:
    app_shutdown(&app);
    SDL_Quit();
    return exit_code;
}