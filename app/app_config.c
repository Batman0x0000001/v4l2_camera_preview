#include <stdio.h>
#include <string.h>

#include "app_apply.h"
#include "app_config.h"
#include "log.h"

/*
 * 统一的“安全字符串复制”辅助函数。
 * 相比到处散写 strncpy(...) - 1，更不容易写错目标字段。
 */
static void app_copy_text(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    snprintf(dst, dst_size, "%s", src);
}

static const char *app_bool_text(int value)
{
    return value ? "on" : "off";
}

static const char *app_stream_backend_text(StreamBackendType type)
{
    switch (type) {
    case STREAM_BACKEND_WEBRTC:
        return "webrtc";
    case STREAM_BACKEND_RTSP:
    default:
        return "rtsp";
    }
}

void app_config_init_default(AppConfig *cfg)
{
    if (!cfg) {
        return;
    }

    memset(cfg, 0, sizeof(*cfg));

    /*
     * 视频采集默认配置。
     * 这里给出的只是“启动配置默认值”，
     * 真正协商后的格式仍以 V4L2 驱动返回结果为准。
     */
    app_copy_text(cfg->device_path,
                  sizeof(cfg->device_path),
                  "/dev/video0");
    cfg->width = 640;
    cfg->height = 480;
    cfg->fps = 30;

    /*
     * 推流 / 录像 / 截图的默认输出位置。
     */
    app_copy_text(cfg->stream_url,
                  sizeof(cfg->stream_url),
                  "rtsp://127.0.0.1:8554/cam");
    app_copy_text(cfg->record_dir,
                  sizeof(cfg->record_dir),
                  "recordings");
    app_copy_text(cfg->snapshot_dir,
                  sizeof(cfg->snapshot_dir),
                  "snapshots");

    /*
     * 启动时默认不自动推流、不自动录像。
     * 是否自动录像会话启动，由：
     *   start_record_on || auto_record_on_start
     * 共同决定，语义收敛在 app_apply.c 中。
     */
    cfg->start_stream_on = 0;
    cfg->start_record_on = 0;
    cfg->auto_record_on_start = 0;

    /*
     * ALSA 采集默认配置。
     */
    app_copy_text(cfg->audio_device,
                  sizeof(cfg->audio_device),
                  "default");
    cfg->audio_sample_rate = 48000;
    cfg->audio_channels = 2;
    cfg->audio_period_frames = 2048;


    cfg->stream_backend = STREAM_BACKEND_RTSP;
    app_copy_text(cfg->stream_url,sizeof(cfg->stream_url),"restp://127.0.0.1:8554/cam");
}

void app_print_banner(void)
{
    LOG_INFO("========================================");
    LOG_INFO(" Linux Camera Media Pipeline Demo");
    LOG_INFO(" V4L2 + ALSA + SDL + FFmpeg + MP4");
    LOG_INFO(" version: 1.0");
    LOG_INFO("========================================");
}

void app_print_config(const AppConfig *cfg)
{
    int stream_on_start;
    int record_autostart;

    if (!cfg) {
        return;
    }

    stream_on_start = app_cfg_stream_enabled_on_start(cfg);
    record_autostart = app_cfg_record_session_should_autostart(cfg);

    LOG_INFO("startup config:");
    LOG_INFO("  video.device_path=%s", cfg->device_path);
    LOG_INFO("  video.width=%d video.height=%d video.fps=%d",
             cfg->width,
             cfg->height,
             cfg->fps);

    LOG_INFO("  record.dir=%s", cfg->record_dir);
    LOG_INFO("  record.start_record_on=%d", cfg->start_record_on);
    LOG_INFO("  record.auto_record_on_start=%d", cfg->auto_record_on_start);
    LOG_INFO("  record.session_autostart=%s",
             app_bool_text(record_autostart));

    LOG_INFO("  snapshot.dir=%s", cfg->snapshot_dir);

    LOG_INFO("  audio.device=%s", cfg->audio_device);
    LOG_INFO("  audio.sample_rate=%u audio.channels=%d audio.period_frames=%u",
             cfg->audio_sample_rate,
             cfg->audio_channels,
             cfg->audio_period_frames);

    LOG_INFO("  stream.backend=%s", app_stream_backend_text(cfg->stream_backend));
    LOG_INFO("  stream.target=%s", cfg->stream_url);
    LOG_INFO("  stream.start_on=%d (%s)",
            cfg->start_stream_on,
            app_bool_text(stream_on_start));
}