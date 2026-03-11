#include"display.h"
#include<string.h>
#include<errno.h>

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

static void calculate_display_rect(AppState *app,SDL_Rect *rect){
    int win_w = 0;
    int win_h = 0;
    double aspect;
    int dst_w,dst_h;
    int x,y;

    SDL_GetWindowSize(app->window,&win_w,&win_h);

    aspect = (double)app->latest.width / (double)app->latest.height;

    dst_h = win_h;
    dst_w = (int)(dst_h * aspect) & ~1;

    if(dst_w > win_w){
        dst_w = win_w;
        dst_h = (int)(dst_w / aspect) & ~1;
    }

    x = (win_w - dst_w) / 2;
    y = (win_h - dst_h) / 2;

    rect->w = dst_w;
    rect->h = dst_h;
    rect->x = x;
    rect->y = y;
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

    calculate_display_rect(app,&rect);

    SDL_RenderClear(app->renderer);
    SDL_RenderCopy(app->renderer,app->texture,NULL,&rect);
    SDL_RenderPresent(app->renderer);

    return 0;
}

int display_save_latest_ppm(AppState *app,const char* filename){
    FILE *fp = NULL;
    
    if(!app || !app->latest.rgb || !filename){
        return -1;
    }

    SDL_LockMutex(app->latest.mutex);

    if(app->latest.frame_id == 0){
        SDL_UnlockMutex(app->latest.mutex);
        return -1;
    }

    fp = fopen(filename,"wb");
    if(!fp){
        SDL_UnlockMutex(app->latest.mutex);
        perror("fopen");
        return -1;
    }

    fprintf(fp,"P6\n%d %d\n255\n",app->latest.width,app->latest.height);

    for (int y = 0; y < app->latest.height; y++)
    {
        if(fwrite(
            app->latest.rgb + y*app->latest.width*3,
            1,
            app->latest.width*3,
            fp)!=(size_t)app->latest.width*3){
            perror("fwrite");
            fclose(fp);
            SDL_UnlockMutex(app->latest.mutex);
            return -1;
        }
    }

    fclose(fp);
    SDL_UnlockMutex(app->latest.mutex);

    printf("已保存截图：%s\n",filename);

    return 0;
}