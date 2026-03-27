#include"app_clock.h"
#include"time_utils.h"

int app_clock_init(AppState *app){
    if(!app){
        return -1;
    }

    app->clock_mutex = SDL_CreateMutex();
    if(!app->clock_mutex){
        return -1;
    }

    app->paused = 0;
    app->pause_begin_us = 0;
    app->total_paused_us = 0;
    app->paused_video_frames_discarded = 0;
    app->paused_audio_chunks_discarded = 0;
    app->paused_audio_frames_discarded = 0;

    return 0;
}

void app_clock_destroy(AppState *app){
    if(!app){
        return;
    }

    if(app->clock_mutex){
        SDL_DestroyMutex(app->clock_mutex);
        app->clock_mutex = NULL;
    }
}

int app_is_paused(AppState *app){
    int paused = 0;
    if(!app || !app->clock_mutex){
        return 0;
    }

    SDL_LockMutex(app->clock_mutex);
    paused = app->paused;
    SDL_UnlockMutex(app->clock_mutex);

    return paused;
}

void app_pause_begin(AppState *app){
    uint64_t now_us;

    if(!app || !app->clock_mutex){
        return;
    }

    now_us = app_now_monotonic_us();

    SDL_LockMutex(app->clock_mutex);
    if(!app->paused){
        app->paused = 1;
        app->pause_begin_us = now_us;
    }
    SDL_UnlockMutex(app->clock_mutex);
}

void app_pause_end(AppState *app){
    uint64_t now_us;

    if(!app || !app->clock_mutex){
        return;
    }

    now_us = app_now_monotonic_us();

    SDL_LockMutex(app->clock_mutex);
    if(app->paused){
       if(app->pause_begin_us > 0 && now_us > app->pause_begin_us){
        app->total_paused_us += now_us - app->pause_begin_us;
       }
       app->pause_begin_us = 0;
       app->paused = 0;
    }
    SDL_UnlockMutex(app->clock_mutex);
}

uint64_t app_total_paused_us(AppState *app){
    uint64_t total_paused_us = 0;
    uint64_t now_us = 0;

    if(!app || !app->clock_mutex){
        return 0;
    }

    now_us = app_now_monotonic_us();

    SDL_LockMutex(app->clock_mutex);
    total_paused_us = app->total_paused_us;

    if(app->paused && app->pause_begin_us > 0 && now_us > app->pause_begin_us){
        total_paused_us += now_us - app->pause_begin_us;
    }
    SDL_UnlockMutex(app->clock_mutex);

    return total_paused_us;
}

uint64_t app_media_clock_us(AppState *app){
    uint64_t now_us;
    uint64_t total_paused_us;

    now_us = app_now_monotonic_us();
    total_paused_us = app_total_paused_us(app);

    if(now_us <= total_paused_us){
        return 0;
    }

    return now_us - total_paused_us;
}