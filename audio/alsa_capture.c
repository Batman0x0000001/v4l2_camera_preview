#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alsa_capture.h"
#include "app_clock.h"
#include "log.h"
#include "time_utils.h"

static size_t audio_bytes_per_sample(snd_pcm_format_t fmt)
{
    int bits = snd_pcm_format_width(fmt);

    if (bits <= 0 || (bits % 8) != 0) {
        return 0;
    }

    return (size_t)bits / 8;
}

static int audio_get_running_flag(AppState *app)
{
    int running = 0;

    if (!app || !app->audio.mutex) {
        return 0;
    }

    SDL_LockMutex(app->audio.mutex);
    running = app->audio.running;
    SDL_UnlockMutex(app->audio.mutex);

    return running;
}

static void audio_set_runtime_flags(AppState *app,
                                    int enabled,
                                    int running,
                                    int fatal_error)
{
    if (!app || !app->audio.mutex) {
        return;
    }

    SDL_LockMutex(app->audio.mutex);
    app->audio.enabled = enabled;
    app->audio.running = running;
    app->audio.fatal_error = fatal_error;
    SDL_UnlockMutex(app->audio.mutex);
}

static void audio_mark_stopped(AppState *app)
{
    if (!app || !app->audio.mutex) {
        return;
    }

    SDL_LockMutex(app->audio.mutex);
    app->audio.running = 0;
    SDL_UnlockMutex(app->audio.mutex);
}

static void audio_increment_xruns(AppState *app)
{
    if (!app || !app->audio.mutex) {
        return;
    }

    SDL_LockMutex(app->audio.mutex);
    app->audio.xruns++;
    SDL_UnlockMutex(app->audio.mutex);
}

static void audio_update_capture_stats(AppState *app,
                                       snd_pcm_sframes_t frames,
                                       uint64_t capture_time_us,
                                       uint64_t *chunk_id,
                                       uint64_t *first_frame_index)
{
    if (!app || !app->audio.mutex || !chunk_id || !first_frame_index) {
        return;
    }

    SDL_LockMutex(app->audio.mutex);

    *chunk_id = app->audio.chunks_captured + 1;
    *first_frame_index = app->audio.pcm_frames_captured;

    app->audio.chunks_captured++;
    app->audio.pcm_frames_captured += (uint64_t)frames;
    app->audio.last_chunk_frames = (uint64_t)frames;
    app->audio.last_capture_time_us = capture_time_us;

    SDL_UnlockMutex(app->audio.mutex);
}

static int audio_init_output_queues(AppState *app)
{
    if (!app) {
        return -1;
    }

    if (audio_queue_init(&app->stream.audio_queue,
                         32,
                         app->audio.period_buffer_bytes) < 0) {
        LOG_ERROR("audio_queue_init (stream) failed");
        return -1;
    }

    if (audio_queue_init(&app->record.audio_queue,
                         256,
                         app->audio.period_buffer_bytes) < 0) {
        LOG_ERROR("audio_queue_init (record) failed");
        audio_queue_destroy(&app->stream.audio_queue);
        return -1;
    }

    return 0;
}

static void audio_distribute_chunk(AppState *app,
                                   const uint8_t *data,
                                   snd_pcm_uframes_t frames,
                                   uint64_t chunk_id,
                                   const AudioMeta *meta)
{
    size_t bytes;

    if (!app || !data || !meta) {
        return;
    }

    bytes = (size_t)frames * app->audio.bytes_per_frame;

    if (app->stream.enabled &&
        app->stream_on &&
        app->stream.accepting_frames &&
        !app->stream.fatal_error) {
        if (audio_queue_push(&app->stream.audio_queue,
                             data,
                             bytes,
                             app->audio.sample_rate,
                             app->audio.channels,
                             app->audio.bytes_per_sample,
                             app->audio.bytes_per_frame,
                             chunk_id,
                             meta) < 0) {
            LOG_WARN("audio_queue_push (stream) failed");
        }
    }

    if (app->record.enabled &&
        app->record_on &&
        app->record.accepting_frames &&
        !app->record.fatal_error) {
        if (audio_queue_push(&app->record.audio_queue,
                             data,
                             bytes,
                             app->audio.sample_rate,
                             app->audio.channels,
                             app->audio.bytes_per_sample,
                             app->audio.bytes_per_frame,
                             chunk_id,
                             meta) < 0) {
            LOG_WARN("audio_queue_push (record) failed");
        }
    }
}

static void audio_enter_fatal_error(AppState *app, const char *reason)
{
    if (!app) {
        return;
    }

    if (app->audio.mutex) {
        SDL_LockMutex(app->audio.mutex);

        if (!app->audio.fatal_error) {
            app->audio.fatal_error = 1;
            app->audio.running = 0;
        }

        SDL_UnlockMutex(app->audio.mutex);
    }

    LOG_ERROR("audio fatal error: %s", reason ? reason : "unknown");
}

void audio_state_init(AppState *app,
                      const char *device_name,
                      unsigned int sample_rate,
                      int channels,
                      unsigned int period_frames)
{
    if (!app) {
        return;
    }

    memset(&app->audio, 0, sizeof(app->audio));

    snprintf(app->audio.device_name,
             sizeof(app->audio.device_name),
             "%s",
             device_name ? device_name : "default");

    app->audio.sample_format = SND_PCM_FORMAT_S16_LE;
    app->audio.sample_rate = (sample_rate > 0) ? sample_rate : 48000;
    app->audio.channels = (channels > 0) ? channels : 2;
    app->audio.period_frames = (period_frames > 0) ? period_frames : 1024;
    app->audio.buffer_frames = app->audio.period_frames * 4;

    app->audio.enabled = 0;
    app->audio.running = 0;
    app->audio.fatal_error = 0;
}

static int audio_open_device(AppState *app)
{
    /*
     * 打开 PCM 设备
     *   -> 配置 access / format / channels / rate / period / buffer
     *   -> 回读实际协商结果
     *   -> 计算字节参数并分配一个 period 的缓存
     *   -> prepare 后进入可读状态
     */
    snd_pcm_hw_params_t *hw_params = NULL;
    unsigned int negotiated_rate;
    snd_pcm_uframes_t negotiated_period_frames;
    snd_pcm_uframes_t negotiated_buffer_frames;
    int ret;

    if (!app) {
        return -1;
    }

    ret = snd_pcm_open(&app->audio.pcm,
                       app->audio.device_name,
                       SND_PCM_STREAM_CAPTURE,
                       SND_PCM_NONBLOCK);
    if (ret < 0) {
        LOG_ERROR("snd_pcm_open failed: %s", snd_strerror(ret));
        return -1;
    }

    ret = snd_pcm_hw_params_malloc(&hw_params);
    if (ret < 0) {
        LOG_ERROR("snd_pcm_hw_params_malloc failed: %s", snd_strerror(ret));
        goto fail;
    }

    ret = snd_pcm_hw_params_any(app->audio.pcm, hw_params);
    if (ret < 0) {
        LOG_ERROR("snd_pcm_hw_params_any failed: %s", snd_strerror(ret));
        goto fail;
    }

    ret = snd_pcm_hw_params_set_access(app->audio.pcm,
                                       hw_params,
                                       SND_PCM_ACCESS_RW_INTERLEAVED);
    if (ret < 0) {
        LOG_ERROR("snd_pcm_hw_params_set_access failed: %s", snd_strerror(ret));
        goto fail;
    }

    ret = snd_pcm_hw_params_set_format(app->audio.pcm,
                                       hw_params,
                                       app->audio.sample_format);
    if (ret < 0) {
        LOG_ERROR("snd_pcm_hw_params_set_format failed: %s", snd_strerror(ret));
        goto fail;
    }

    ret = snd_pcm_hw_params_set_channels(app->audio.pcm,
                                         hw_params,
                                         (unsigned int)app->audio.channels);
    if (ret < 0) {
        LOG_ERROR("snd_pcm_hw_params_set_channels failed: %s", snd_strerror(ret));
        goto fail;
    }

    negotiated_rate = app->audio.sample_rate;
    ret = snd_pcm_hw_params_set_rate_near(app->audio.pcm,
                                          hw_params,
                                          &negotiated_rate,
                                          NULL);
    if (ret < 0) {
        LOG_ERROR("snd_pcm_hw_params_set_rate_near failed: %s", snd_strerror(ret));
        goto fail;
    }

    negotiated_period_frames = app->audio.period_frames;
    ret = snd_pcm_hw_params_set_period_size_near(app->audio.pcm,
                                                 hw_params,
                                                 &negotiated_period_frames,
                                                 NULL);
    if (ret < 0) {
        LOG_ERROR("snd_pcm_hw_params_set_period_size_near failed: %s", snd_strerror(ret));
        goto fail;
    }

    negotiated_buffer_frames = app->audio.buffer_frames;
    ret = snd_pcm_hw_params_set_buffer_size_near(app->audio.pcm,
                                                 hw_params,
                                                 &negotiated_buffer_frames);
    if (ret < 0) {
        LOG_ERROR("snd_pcm_hw_params_set_buffer_size_near failed: %s", snd_strerror(ret));
        goto fail;
    }

    ret = snd_pcm_hw_params(app->audio.pcm, hw_params);
    if (ret < 0) {
        LOG_ERROR("snd_pcm_hw_params failed: %s", snd_strerror(ret));
        goto fail;
    }

    ret = snd_pcm_hw_params_get_rate(hw_params, &app->audio.sample_rate, NULL);
    if (ret < 0) {
        LOG_ERROR("snd_pcm_hw_params_get_rate failed: %s", snd_strerror(ret));
        goto fail;
    }

    ret = snd_pcm_hw_params_get_period_size(hw_params,
                                            &app->audio.period_frames,
                                            NULL);
    if (ret < 0) {
        LOG_ERROR("snd_pcm_hw_params_get_period_size failed: %s", snd_strerror(ret));
        goto fail;
    }

    ret = snd_pcm_hw_params_get_buffer_size(hw_params,
                                            &app->audio.buffer_frames);
    if (ret < 0) {
        LOG_ERROR("snd_pcm_hw_params_get_buffer_size failed: %s", snd_strerror(ret));
        goto fail;
    }

    app->audio.bytes_per_sample = audio_bytes_per_sample(app->audio.sample_format);
    if (app->audio.bytes_per_sample == 0) {
        LOG_ERROR("unsupported sample format width");
        goto fail;
    }

    app->audio.bytes_per_frame =
        app->audio.bytes_per_sample * (size_t)app->audio.channels;

    app->audio.period_buffer_bytes =
        (size_t)app->audio.period_frames * app->audio.bytes_per_frame;

    app->audio.period_buffer = (unsigned char *)malloc(app->audio.period_buffer_bytes);
    if (!app->audio.period_buffer) {
        LOG_ERROR("malloc audio.period_buffer failed");
        goto fail;
    }

    ret = snd_pcm_prepare(app->audio.pcm);
    if (ret < 0) {
        LOG_ERROR("snd_pcm_prepare failed: %s", snd_strerror(ret));
        goto fail;
    }

    snd_pcm_hw_params_free(hw_params);
    hw_params = NULL;

    LOG_INFO("audio opened: device=%s rate=%u channels=%d period=%lu buffer=%lu bytes_per_frame=%zu",
             app->audio.device_name,
             app->audio.sample_rate,
             app->audio.channels,
             (unsigned long)app->audio.period_frames,
             (unsigned long)app->audio.buffer_frames,
             app->audio.bytes_per_frame);

    return 0;

fail:
    if (hw_params) {
        snd_pcm_hw_params_free(hw_params);
    }
    return -1;
}

static int audio_recover_from_read_error(AppState *app, int err)
{
    int ret;

    if (!app || !app->audio.pcm) {
        return -1;
    }

    /*
     * overrun:
     * 采集线程读取不及时，内核缓冲区被新数据覆盖。
     * prepare 后回到 PREPARED 状态，允许继续读。
     */
    if (err == -EPIPE) {
        audio_increment_xruns(app);

        LOG_WARN("audio overrun detected, prepare pcm again");

        ret = snd_pcm_prepare(app->audio.pcm);
        if (ret < 0) {
            LOG_ERROR("snd_pcm_prepare after overrun failed: %s",
                      snd_strerror(ret));
            return -1;
        }

        return 0;
    }

    /*
     * suspend:
     * 先尝试 resume，硬件暂未恢复时会反复返回 -EAGAIN。
     * 若 resume 不可行，则退回 prepare。
     */
    if (err == -ESTRPIPE) {
        while ((ret = snd_pcm_resume(app->audio.pcm)) == -EAGAIN) {
            SDL_Delay(10);
        }

        if (ret < 0) {
            ret = snd_pcm_prepare(app->audio.pcm);
            if (ret < 0) {
                LOG_ERROR("recover from suspend failed: %s", snd_strerror(ret));
                return -1;
            }
        }

        return 0;
    }

    /*
     * 其他错误走 ALSA 通用恢复路径。
     */
    ret = snd_pcm_recover(app->audio.pcm, err, 1);
    if (ret < 0) {
        LOG_ERROR("snd_pcm_recover failed: %s", snd_strerror(ret));
        return -1;
    }

    return 0;
}

static int audio_thread_main(void *userdata)
{
    AppState *app = (AppState *)userdata;

    if (!app) {
        return -1;
    }

    while (!app->quit) {
        snd_pcm_sframes_t frames;
        uint64_t capture_time_us;
        uint64_t chunk_id = 0;
        uint64_t first_frame_index = 0;
        AudioMeta meta;

        if (!audio_get_running_flag(app)) {
            break;
        }

        /*
         * 每次读取一个 period 的 PCM 数据：
         * - -EAGAIN: 暂无数据，短暂等待后重试
         * - 负值: 尝试错误恢复
         * - 0: 没读到帧，直接继续
         * - 正值: 记录统一媒体时钟，并向下游分发
         */
        frames = snd_pcm_readi(app->audio.pcm,
                               app->audio.period_buffer,
                               app->audio.period_frames);

        if (frames == -EAGAIN) {
            SDL_Delay(1);
            continue;
        }

        if (frames < 0) {
            if (audio_recover_from_read_error(app, (int)frames) < 0) {
                audio_enter_fatal_error(app, "audio read recover failed");
                break;
            }
            continue;
        }

        if (frames == 0) {
            continue;
        }

        if (app_is_paused(app)) {
            if (app->clock_mutex) {
                SDL_LockMutex(app->clock_mutex);
                app->paused_audio_chunks_discarded++;
                app->paused_audio_frames_discarded += (uint64_t)frames;
                SDL_UnlockMutex(app->clock_mutex);
            }
            continue;
        }

        capture_time_us = app_media_clock_us(app);

        audio_update_capture_stats(app,
                                   frames,
                                   capture_time_us,
                                   &chunk_id,
                                   &first_frame_index);

        meta.capture_time_us = capture_time_us;
        meta.first_frame_index = first_frame_index;
        meta.frames = (uint32_t)frames;

        audio_distribute_chunk(app,
                               app->audio.period_buffer,
                               (snd_pcm_uframes_t)frames,
                               chunk_id,
                               &meta);
    }

    audio_mark_stopped(app);
    return 0;
}

int audio_init(AppState *app)
{
    if (!app) {
        return -1;
    }

    if (audio_open_device(app) < 0) {
        goto fail;
    }

    if (audio_init_output_queues(app) < 0) {
        goto fail;
    }

    app->audio.mutex = SDL_CreateMutex();
    if (!app->audio.mutex) {
        LOG_ERROR("SDL_CreateMutex (audio) failed: %s", SDL_GetError());
        goto fail;
    }

    audio_set_runtime_flags(app, 1, 1, 0);

    app->audio.thread = SDL_CreateThread(audio_thread_main, "audio_thread", app);
    if (!app->audio.thread) {
        LOG_ERROR("SDL_CreateThread (audio) failed: %s", SDL_GetError());
        goto fail;
    }

    return 0;

fail:
    audio_close(app);
    return -1;
}

void audio_close(AppState *app)
{
    if (!app) {
        return;
    }

    if (app->audio.mutex) {
        SDL_LockMutex(app->audio.mutex);
        app->audio.running = 0;
        SDL_UnlockMutex(app->audio.mutex);
    }

    /*
     * drop 会让阻塞中的 snd_pcm_readi 尽快退出，
     * 这样等待线程结束时不容易卡住。
     */
    if (app->audio.pcm) {
        snd_pcm_drop(app->audio.pcm);
    }

    if (app->audio.thread) {
        SDL_WaitThread(app->audio.thread, NULL);
        app->audio.thread = NULL;
    }

    if (app->audio.pcm) {
        snd_pcm_close(app->audio.pcm);
        app->audio.pcm = NULL;
    }

    free(app->audio.period_buffer);
    app->audio.period_buffer = NULL;
    app->audio.period_buffer_bytes = 0;

    if (app->audio.mutex) {
        SDL_DestroyMutex(app->audio.mutex);
        app->audio.mutex = NULL;
    }

    app->audio.enabled = 0;
    app->audio.running = 0;
    app->audio.fatal_error = 0;

    app->audio.bytes_per_sample = 0;
    app->audio.bytes_per_frame = 0;

    app->audio.chunks_captured = 0;
    app->audio.pcm_frames_captured = 0;
    app->audio.xruns = 0;
    app->audio.last_capture_time_us = 0;
    app->audio.last_chunk_frames = 0;
}