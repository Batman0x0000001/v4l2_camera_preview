#include"display.h"
#include<string.h>

int display_init(AppState *app){
    app->window = SDL_CreateWindow(
        "v4l2_camera_preview",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        app->width,
        app->height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if(!app->window){
        return -1;
    }

    app->renderer = SDL_CreateRenderer(
        app->window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if(!app->renderer){
        return -1;
    }

    app->texture = SDL_CreateTexture(
        app->renderer,
        SDL_PIXELFORMAT_RGB24,
        SDL_TEXTUREACCESS_STREAMING,
        app->width,
        app->height);
    if(!app->texture){
        return -1;
    }

    return 0;
}

void display_destroy(AppState *app){
    if (app->texture) {
        SDL_DestroyTexture(app->texture);
        app->texture = NULL;
    }

    if (app->renderer) {
        SDL_DestroyRenderer(app->renderer);
        app->renderer = NULL;
    }

    if (app->window) {
        SDL_DestroyWindow(app->window);
        app->window = NULL;
    }
}

int display_present_latest(AppState *app){
    SDL_Rect rect;
    void *pixels = NULL;
    int pitch = 0;
    int y;
    
    if(!app->latest.rgb){
        return 0;
    }

    SDL_LockMutex(app->latest.mutex);

    if(app->latest.frame_id == 0){
        SDL_UnlockMutex(app->latest.mutex);
        return 0;
    }

    if(SDL_LockTexture(app->texture,NULL,&pixels,&pitch) != 0){
        SDL_UnlockMutex(app->latest.mutex);
        return -1;
    }

    for(y = 0;y < app->latest.height;y++){
        memcpy((unsigned char *)pixels+y*pitch,
            app->latest.rgb + y*app->latest.width*3,
            (size_t)app->latest.width*3);
    }

    SDL_UnlockTexture(app->texture);
    SDL_UnlockMutex(app->latest.mutex);

    rect.x = 0;
    rect.y = 0;
    rect.w = app->width;
    rect.h = app->height;

    SDL_RenderClear(app->renderer);
    SDL_RenderCopy(app->renderer,app->texture,NULL,&rect);
    SDL_RenderPresent(app->renderer);

    return 0;
}