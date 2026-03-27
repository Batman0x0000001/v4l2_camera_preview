#include "app_startup.h"
#include "display.h"
#include "stream.h"
#include "record.h"
#include "v4l2_core.h"
#include "log.h"
#include<unistd.h>
#include"alsa_capture.h"
#include"app_clock.h"

int app_startup(AppState *app){
    if(!app){
        return -1;
    }

    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0){
        LOG_ERROR("SDL_Init failed");
        return -1;
    }

    if(app_clock_init(app) < 0){
        LOG_ERROR("app_clock_init failed");
        return -1;
    }

    if(open_device(app) < 0){
        LOG_ERROR("open_device failed");
        return -1;
    }

    if(query_capability(app) < 0){
        LOG_ERROR("query_capability failed");
        return -1;
    }

    if(enum_formats(app) < 0){
        LOG_ERROR("enum_formats failed");
        return -1;
    }

    if(enum_frame_sizes(app, app->pixfmt) < 0){
        LOG_ERROR("enum_frame_sizes failed");
        return -1;
    }

    if(enum_frame_intervals(app, app->pixfmt, app->width, app->height) < 0){
        LOG_ERROR("enum_frame_intervals failed");
        return -1;
    }

    if(enum_controls(app) < 0){
        LOG_ERROR("enum_controls failed");
        return -1;
    }

    if(set_format(app) < 0){
        LOG_ERROR("set_format failed");
        return -1;
    }

    if(init_mmap(app) < 0){
        LOG_ERROR("init_mmap failed");
        return -1;
    }

    if(init_shared_frame(app) < 0){
        LOG_ERROR("init_shared_frame failed");
        return -1;
    }

    if(alloc_preview_rgb_buffer(app) < 0){
        LOG_ERROR("alloc_preview_rgb_buffer failed");
        return -1;
    }

    if(alloc_capture_yuyv_buffer(app) < 0){
        LOG_ERROR("alloc_capture_yuyv_buffer failed");
        return -1;
    }

    if(display_init(app) < 0){
        LOG_ERROR("display_init failed");
        return -1;
    }

    if(audio_init(app) < 0){
        LOG_ERROR("audio_init failed");
        return -1;
    }

    if(stream_init(app) < 0){
        LOG_ERROR("stream_init failed");
        return -1;
    }

    if(record_init(app) < 0){
        LOG_ERROR("record_init failed");
        return -1;
    }

    if(start_capturing(app) < 0){
        LOG_ERROR("start_capturing failed");
        return -1;
    }

    if(capture_start_thread(app) < 0){
        LOG_ERROR("capture_start_thread failed");
        return -1;
    }

    LOG_INFO("app_startup succeeded");
    return 0;
}

void app_shutdowm(AppState *app){
    if(!app){
        return;
    }
    cleanup(app);
}

void cleanup(AppState *app){
    app->quit = 1;

    if(app->record.session_active){
        (void)record_session_stop(app);
        app->record_on = 0;
    }

    if(app->capture_tid){
        SDL_WaitThread(app->capture_tid,NULL);
        app->capture_tid = NULL;
    }

    audio_close(app);
    stream_close(app);
    record_close(app);

    stop_capturing(app);
    uninit_mmap(app);

    if(app->latest.mutex){
        SDL_DestroyMutex(app->latest.mutex);
        app->latest.mutex = NULL;
    }

    free(app->latest.rgb);
    app->latest.rgb = NULL;
    app->latest.bytes = 0;
    app->latest.frame_id = 0;

    free(app->preview_rgb);
    app->preview_rgb = NULL;
    app->preview_rgb_bytes = 0;

    free(app->capture_yuyv);
    app->capture_yuyv = NULL;
    app->capture_yuyv_bytes = 0;

    if(app->fd >= 0){
        close(app->fd);
        app->fd = -1;
    }

    app_clock_destroy(app);
}