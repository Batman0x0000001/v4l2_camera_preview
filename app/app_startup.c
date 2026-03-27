#include "app_startup.h"

#include <unistd.h>

#include "alsa_capture.h"
#include "app_clock.h"
#include "display.h"
#include "log.h"
#include "record.h"
#include "stream.h"
#include "v4l2_core.h"

/*
 * 这个文件只做“生命周期编排”：
 * - 启动时按阶段初始化；
 * - 失败时统一清理；
 * - 退出时统一关闭。
 *
 * 这样做的好处：
 * 1. main.c 更干净；
 * 2. 后续接 WebRTC 时，可以更容易把“采集核心”和“输出后端”拆开；
 * 3. 清理逻辑不再散落到无关模块。
 */

static void app_release_shared_frame(AppState *app)
{
    if (app->latest.mutex) {
        SDL_DestroyMutex(app->latest.mutex);
        app->latest.mutex = NULL;
    }

    free(app->latest.rgb);
    app->latest.rgb = NULL;
    app->latest.bytes = 0;
    app->latest.width = 0;
    app->latest.height = 0;
    app->latest.frame_id = 0;
}

static void app_release_capture_buffers(AppState *app)
{
    free(app->preview_rgb);
    app->preview_rgb = NULL;
    app->preview_rgb_bytes = 0;

    free(app->capture_yuyv);
    app->capture_yuyv = NULL;
    app->capture_yuyv_bytes = 0;
}

static void app_close_video_device(AppState *app)
{
    if (app->fd >= 0) {
        close(app->fd);
        app->fd = -1;
    }
}

static int app_startup_prepare_clock_and_device(AppState *app)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        LOG_ERROR("SDL_Init failed");
        return -1;
    }

    if (app_clock_init(app) < 0) {
        LOG_ERROR("app_clock_init failed");
        return -1;
    }

    if (open_device(app) < 0) {
        LOG_ERROR("open_device failed");
        return -1;
    }

    if (query_capability(app) < 0) {
        LOG_ERROR("query_capability failed");
        return -1;
    }

    if (enum_formats(app) < 0) {
        LOG_ERROR("enum_formats failed");
        return -1;
    }

    if (enum_frame_sizes(app, app->pixfmt) < 0) {
        LOG_ERROR("enum_frame_sizes failed");
        return -1;
    }

    if (enum_frame_intervals(app, app->pixfmt, app->width, app->height) < 0) {
        LOG_ERROR("enum_frame_intervals failed");
        return -1;
    }

    if (enum_controls(app) < 0) {
        LOG_ERROR("enum_controls failed");
        return -1;
    }

    if (set_format(app) < 0) {
        LOG_ERROR("set_format failed");
        return -1;
    }

    return 0;
}

static int app_startup_prepare_video_buffers(AppState *app)
{
    if (init_mmap(app) < 0) {
        LOG_ERROR("init_mmap failed");
        return -1;
    }

    if (init_shared_frame(app) < 0) {
        LOG_ERROR("init_shared_frame failed");
        return -1;
    }

    if (alloc_preview_rgb_buffer(app) < 0) {
        LOG_ERROR("alloc_preview_rgb_buffer failed");
        return -1;
    }

    if (alloc_capture_yuyv_buffer(app) < 0) {
        LOG_ERROR("alloc_capture_yuyv_buffer failed");
        return -1;
    }

    return 0;
}

static int app_startup_prepare_runtime_modules(AppState *app)
{
    if (display_init(app) < 0) {
        LOG_ERROR("display_init failed");
        return -1;
    }

    if (audio_init(app) < 0) {
        LOG_ERROR("audio_init failed");
        return -1;
    }

    if (stream_init(app) < 0) {
        LOG_ERROR("stream_init failed");
        return -1;
    }

    if (record_init(app) < 0) {
        LOG_ERROR("record_init failed");
        return -1;
    }

    return 0;
}

static int app_startup_start_workers(AppState *app)
{
    if (start_capturing(app) < 0) {
        LOG_ERROR("start_capturing failed");
        return -1;
    }

    if (capture_start_thread(app) < 0) {
        LOG_ERROR("capture_start_thread failed");
        return -1;
    }

    return 0;
}

int app_startup(AppState *app)
{
    if (!app) {
        return -1;
    }

    if (app_startup_prepare_clock_and_device(app) < 0) {
        goto fail;
    }

    if (app_startup_prepare_video_buffers(app) < 0) {
        goto fail;
    }

    if (app_startup_prepare_runtime_modules(app) < 0) {
        goto fail;
    }

    if (app_startup_start_workers(app) < 0) {
        goto fail;
    }

    LOG_INFO("app_startup succeeded");
    return 0;

fail:
    /*
     * 启动失败时由本模块自己统一回滚，
     * 避免 main.c 到处补残局。
     */
    app_shutdown(app);
    return -1;
}

void app_shutdown(AppState *app)
{
    if (!app) {
        return;
    }

    app->quit = 1;

    /*
     * 如果录像会话仍在进行，先请求结束，
     * 让输出文件尽量正常写尾并关闭。
     */
    if (app->record.session_active) {
        (void)record_session_stop(app);
        app->record_on = 0;
    }

    /*
     * 先等采集线程退出，避免它继续访问后面将被释放的资源。
     */
    if (app->capture_tid) {
        SDL_WaitThread(app->capture_tid, NULL);
        app->capture_tid = NULL;
    }

    /*
     * 关闭依赖采集数据的各运行模块。
     * 顺序保持保守，不在这一轮大改行为。
     */
    audio_close(app);
    stream_close(app);
    record_close(app);

    stop_capturing(app);
    uninit_mmap(app);

    /*
     * display_init() 属于 app_startup() 拉起的资源，
     * 因此 display_destroy() 也应由本模块统一回收。
     *
     * 这样即使启动阶段在 display_init() 之后失败，
     * 也不会把 SDL 窗口资源遗留给 main.c。
     */
    display_destroy(app);

    app_release_shared_frame(app);
    app_release_capture_buffers(app);
    app_close_video_device(app);

    app_clock_destroy(app);
}