#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_clock.h"
#include "app_ctrl.h"
#include "log.h"
#include "record.h"
#include "v4l2_core.h"

static void app_get_audio_queue_stats(const AudioQueue *q,
                                      int *size,
                                      uint64_t *dropped_chunks)
{
    if (size) {
        *size = 0;
    }
    if (dropped_chunks) {
        *dropped_chunks = 0;
    }

    if (!q || !q->mutex) {
        return;
    }

    SDL_LockMutex(q->mutex);

    if (size) {
        *size = q->size;
    }
    if (dropped_chunks) {
        *dropped_chunks = q->dropped_chunks;
    }

    SDL_UnlockMutex(q->mutex);
}

static void app_get_frame_queue_stats(const FrameQueue *q,
                                      int *size,
                                      uint64_t *dropped_frames)
{
    if (size) {
        *size = 0;
    }
    if (dropped_frames) {
        *dropped_frames = 0;
    }

    if (!q || !q->mutex) {
        return;
    }

    SDL_LockMutex(q->mutex);

    if (size) {
        *size = q->size;
    }
    if (dropped_frames) {
        *dropped_frames = q->dropped_frames;
    }

    SDL_UnlockMutex(q->mutex);
}

static void app_get_latest_frame_snapshot(const AppState *app,
                                          uint64_t *frame_id,
                                          CaptureMeta *meta)
{
    if (frame_id) {
        *frame_id = 0;
    }
    if (meta) {
        memset(meta, 0, sizeof(*meta));
    }

    if (!app) {
        return;
    }

    if (app->latest.mutex) {
        SDL_LockMutex(app->latest.mutex);

        if (frame_id) {
            *frame_id = app->latest.frame_id;
        }
        if (meta) {
            *meta = app->latest.meta;
        }

        SDL_UnlockMutex(app->latest.mutex);
        return;
    }

    if (frame_id) {
        *frame_id = app->latest.frame_id;
    }
    if (meta) {
        *meta = app->latest.meta;
    }
}

static CameraControl *app_get_current_control(AppState *app)
{
    if (!app) {
        return NULL;
    }

    if (app->control_count <= 0) {
        return NULL;
    }

    if (app->current_control < 0 || app->current_control >= app->control_count) {
        return NULL;
    }

    return &app->controls[app->current_control];
}

static int app_control_is_adjustable(const CameraControl *ctrl)
{
    if (!ctrl) {
        return 0;
    }

    switch (ctrl->type) {
    case V4L2_CTRL_TYPE_INTEGER:
    case V4L2_CTRL_TYPE_BOOLEAN:
    case V4L2_CTRL_TYPE_MENU:
    case V4L2_CTRL_TYPE_INTEGER_MENU:
        return 1;

    default:
        return 0;
    }
}

void app_print_help(void)
{
    LOG_INFO("keyboard help:======================================");
    LOG_INFO("  esc            quit");
    LOG_INFO("  space          pause/resume capture processing");
    LOG_INFO("  t              toggle streaming");
    LOG_INFO("  r              toggle MP4 recording");
    LOG_INFO("  s              save snapshot");
    LOG_INFO("  up/down        select V4L2 control");
    LOG_INFO("  left/right     adjust current control");
    LOG_INFO("  h              print help");
    LOG_INFO("  i              print runtime state");
    LOG_INFO("  y              load WebRTC answer file");
    LOG_INFO("  u              load next WebRTC candidate file");
}

void app_print_runtime_state(const AppState *app)
{
    int stream_frame_q_size = 0;
    int record_frame_q_size = 0;
    int stream_audio_q_size = 0;
    int record_audio_q_size = 0;

    uint64_t stream_frame_dropped = 0;
    uint64_t record_frame_dropped = 0;
    uint64_t stream_audio_dropped = 0;
    uint64_t record_audio_dropped = 0;

    uint64_t latest_frame_id = 0;
    CaptureMeta latest_meta;

    if (!app) {
        return;
    }

    memset(&latest_meta, 0, sizeof(latest_meta));

    app_get_frame_queue_stats(&app->stream.queue,
                              &stream_frame_q_size,
                              &stream_frame_dropped);
    app_get_frame_queue_stats(&app->record.queue,
                              &record_frame_q_size,
                              &record_frame_dropped);

    app_get_audio_queue_stats(&app->stream.audio_queue,
                              &stream_audio_q_size,
                              &stream_audio_dropped);
    app_get_audio_queue_stats(&app->record.audio_queue,
                              &record_audio_q_size,
                              &record_audio_dropped);

    app_get_latest_frame_snapshot(app, &latest_frame_id, &latest_meta);

    LOG_INFO("runtime state:=========================================");
    LOG_INFO("  paused=%d", app->paused);

    LOG_INFO("  stream_on=%d enabled=%d accepting=%d fatal=%d",
             app->stream_on,
             app->stream.enabled,
             app->stream.accepting_frames,
             app->stream.fatal_error);

    LOG_INFO("  record_on=%d enabled=%d accepting=%d fatal=%d",
             app->record_on,
             app->record.enabled,
             app->record.accepting_frames,
             app->record.fatal_error);

    LOG_INFO("  frames_captured=%llu frames_dropped=%llu",
             (unsigned long long)app->frames_captured,
             (unsigned long long)app->frames_dropped);

    LOG_INFO("  latest_frame_id=%llu latest_seq=%u latest_bytes=%u capture_us=%llu device_us=%llu",
             (unsigned long long)latest_frame_id,
             latest_meta.sequence,
             latest_meta.bytesused,
             (unsigned long long)latest_meta.capture_time_us,
             (unsigned long long)latest_meta.device_time_us);

    LOG_INFO("  stream_frame_queue size=%d dropped=%llu",
             stream_frame_q_size,
             (unsigned long long)stream_frame_dropped);

    LOG_INFO("  record_frame_queue size=%d dropped=%llu",
             record_frame_q_size,
             (unsigned long long)record_frame_dropped);

    LOG_INFO("  audio enabled=%d running=%d fatal=%d xruns=%llu",
             app->audio.enabled,
             app->audio.running,
             app->audio.fatal_error,
             (unsigned long long)app->audio.xruns);

    LOG_INFO("  audio chunks=%llu pcm_frames=%llu last_chunk_frames=%llu last_capture_us=%llu",
             (unsigned long long)app->audio.chunks_captured,
             (unsigned long long)app->audio.pcm_frames_captured,
             (unsigned long long)app->audio.last_chunk_frames,
             (unsigned long long)app->audio.last_capture_time_us);

    LOG_INFO("  stream_audio_queue size=%d dropped=%llu",
             stream_audio_q_size,
             (unsigned long long)stream_audio_dropped);

    LOG_INFO("  record_audio_queue size=%d dropped=%llu",
             record_audio_q_size,
             (unsigned long long)record_audio_dropped);

    LOG_INFO("  total_paused_us=%llu (%.3f s)",
             (unsigned long long)app->total_paused_us,
             (double)app->total_paused_us / 1000000.0);

    LOG_INFO("  paused_video_frames_discarded=%llu",
             (unsigned long long)app->paused_video_frames_discarded);

    LOG_INFO("  paused_audio_chunks_discarded=%llu paused_audio_frames_discarded=%llu",
             (unsigned long long)app->paused_audio_chunks_discarded,
             (unsigned long long)app->paused_audio_frames_discarded);

    LOG_INFO("  record session_active=%d stopping=%d session_count=%llu",
             app->record.session_active,
             app->record.stopping_session,
             (unsigned long long)app->record.session_count);

    LOG_INFO("  active_output_path=%s",
             app->record.session_active ? app->record.active_output_path : "(none)");

    LOG_INFO("  record video_frames_encoded=%llu audio_frames_encoded=%llu audio_chunks_consumed=%llu",
             (unsigned long long)app->record.frames_encoded,
             (unsigned long long)app->record.audio_frames_encoded,
             (unsigned long long)app->record.audio_chunks_consumed);
}

void app_print_current_control_status(AppState *app)
{
    CameraControl *ctrl;
    int32_t value = 0;

    if (!app) {
        return;
    }

    if (app->control_count <= 0) {
        LOG_WARN("no V4L2 controls available");
        return;
    }

    ctrl = app_get_current_control(app);
    if (!ctrl) {
        LOG_WARN("current_control out of range: %d", app->current_control);
        return;
    }

    if (get_control_value(app, ctrl->id, &value) < 0) {
        LOG_WARN("failed to get control value: %s", ctrl->name);
        return;
    }

    LOG_INFO("control[%d/%d]: name=%s id=0x%x type=%d value=%d range=[%d,%d] step=%d default=%d",
             app->current_control + 1,
             app->control_count,
             ctrl->name,
             ctrl->id,
             ctrl->type,
             value,
             ctrl->min,
             ctrl->max,
             ctrl->step,
             ctrl->def);
}

void app_toggle_pause(AppState *app)
{
    uint64_t total_paused_us;

    if (!app) {
        return;
    }

    if (app_is_paused(app)) {
        app_pause_end(app);
        total_paused_us = app_total_paused_us(app);

        LOG_INFO("resume: total_paused_us=%llu (%.3f s)",
                 (unsigned long long)total_paused_us,
                 (double)total_paused_us / 1000000.0);
    } else {
        app_pause_begin(app);

        /*
         * 立即通知录像模块处理暂停，
         * 避免按下暂停后仍继续写入一小段拖尾数据。
         */
        record_notify_pause(app);

        LOG_INFO("pause begin");
    }
}

void app_toggle_stream(AppState *app)
{
    if (!app) {
        return;
    }

    if (!app->stream.enabled) {
        LOG_WARN("stream module is not enabled");
        return;
    }

    if (app->stream.fatal_error) {
        LOG_WARN("stream module is in fatal_error state");
        return;
    }

    app->stream_on = !app->stream_on;
    LOG_INFO("stream toggled: stream_on=%d", app->stream_on);
}

void app_toggle_record(AppState *app)
{
    if (!app) {
        return;
    }

    if (!app->record.enabled) {
        LOG_WARN("record module is not enabled");
        return;
    }

    if (app->record.fatal_error) {
        LOG_WARN("record module is in fatal_error state");
        return;
    }

    if (app->record.session_active) {
        if (record_session_stop(app) < 0) {
            LOG_WARN("record_session_stop failed");
            return;
        }

        app->record_on = 0;
        LOG_INFO("record stopped");
        return;
    }

    if (record_session_start(app) < 0) {
        LOG_WARN("record_session_start failed");
        return;
    }

    app->record_on = 1;
    LOG_INFO("record started: %s", app->record.active_output_path);
}

void app_select_next_control(AppState *app)
{
    if (!app) {
        return;
    }

    if (app->control_count <= 0) {
        LOG_WARN("no V4L2 controls available");
        return;
    }

    app->current_control++;
    if (app->current_control >= app->control_count) {
        app->current_control = 0;
    }

    app_print_current_control_status(app);
}

void app_select_prev_control(AppState *app)
{
    if (!app) {
        return;
    }

    if (app->control_count <= 0) {
        LOG_WARN("no V4L2 controls available");
        return;
    }

    app->current_control--;
    if (app->current_control < 0) {
        app->current_control = app->control_count - 1;
    }

    app_print_current_control_status(app);
}

void app_adjust_current_control(AppState *app, int delta)
{
    CameraControl *ctrl;
    int32_t current_value = 0;
    int new_value;
    int step;

    if (!app) {
        return;
    }

    if (app->control_count <= 0) {
        LOG_WARN("no V4L2 controls available");
        return;
    }

    ctrl = app_get_current_control(app);
    if (!ctrl) {
        LOG_WARN("current_control out of range: %d", app->current_control);
        return;
    }

    if (!app_control_is_adjustable(ctrl)) {
        LOG_WARN("control is not adjustable with left/right: %s", ctrl->name);
        return;
    }

    if (get_control_value(app, ctrl->id, &current_value) < 0) {
        LOG_WARN("failed to get current control value: %s", ctrl->name);
        return;
    }

    step = (ctrl->step > 0) ? ctrl->step : 1;
    new_value = current_value + delta * step;

    if (new_value < ctrl->min) {
        new_value = ctrl->min;
    }
    if (new_value > ctrl->max) {
        new_value = ctrl->max;
    }

    if (new_value == current_value) {
        LOG_INFO("control unchanged: %s value=%d", ctrl->name, current_value);
        return;
    }

    if (set_control_value(app, ctrl->id, new_value) < 0) {
        LOG_WARN("failed to set control: %s -> %d", ctrl->name, new_value);
        return;
    }

    LOG_INFO("control updated: %s %d -> %d", ctrl->name, current_value, new_value);
    app_print_current_control_status(app);
}

int app_save_snapshot(AppState *app, const char *filename)
{
    FILE *fp = NULL;
    unsigned char *rgb_copy = NULL;
    size_t rgb_bytes = 0;
    int width = 0;
    int height = 0;
    uint64_t frame_id = 0;

    if (!app || !filename) {
        return -1;
    }

    if (!app->latest.rgb || !app->latest.mutex) {
        return -1;
    }

    SDL_LockMutex(app->latest.mutex);

    frame_id = app->latest.frame_id;
    width = app->latest.width;
    height = app->latest.height;
    rgb_bytes = app->latest.bytes;

    if (frame_id == 0 || width <= 0 || height <= 0 || rgb_bytes == 0) {
        SDL_UnlockMutex(app->latest.mutex);
        return -1;
    }

    rgb_copy = (unsigned char *)malloc(rgb_bytes);
    if (!rgb_copy) {
        SDL_UnlockMutex(app->latest.mutex);
        perror("malloc snapshot buffer");
        return -1;
    }

    memcpy(rgb_copy, app->latest.rgb, rgb_bytes);

    SDL_UnlockMutex(app->latest.mutex);

    fp = fopen(filename, "wb");
    if (!fp) {
        perror("fopen");
        free(rgb_copy);
        return -1;
    }

    fprintf(fp, "P6\n%d %d\n255\n", width, height);

    for (int y = 0; y < height; y++) {
        const unsigned char *line = rgb_copy + (size_t)y * (size_t)width * 3;

        if (fwrite(line, 1, (size_t)width * 3, fp) != (size_t)width * 3) {
            perror("fwrite");
            fclose(fp);
            free(rgb_copy);
            return -1;
        }
    }

    fclose(fp);
    free(rgb_copy);

    LOG_INFO("snapshot saved: %s", filename);
    return 0;
}

void app_print_module_overview(const AppState *app)
{
    if (!app) {
        return;
    }

    LOG_INFO("module overview:=========================================");
    LOG_INFO("  capture: device=%s size=%dx%d pixfmt=0x%x bytesperline=%u sizeimage=%u",
             app->device_path,
             app->width,
             app->height,
             app->pixfmt,
             app->bytesperline,
             app->sizeimage);

    LOG_INFO("  display: latest_rgb_bytes=%zu", app->latest.bytes);

    LOG_INFO("  stream: url=%s enabled=%d accepting=%d fatal=%d",
             app->stream.output_url,
             app->stream.enabled,
             app->stream.accepting_frames,
             app->stream.fatal_error);

    LOG_INFO("  record: dir=%s enabled=%d accepting=%d fatal=%d active=%s",
             app->record.output_dir,
             app->record.enabled,
             app->record.accepting_frames,
             app->record.fatal_error,
             app->record.session_active ? app->record.active_output_path : "(none)");
}