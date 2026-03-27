#include "display.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

static void display_get_target_rect(AppState *app, SDL_Rect *rect)
{
    int win_w = 0;
    int win_h = 0;
    double aspect;
    int dst_w;
    int dst_h;

    if (!app || !rect || !app->window || app->latest.width <= 0 || app->latest.height <= 0) {
        return;
    }

    SDL_GetWindowSize(app->window, &win_w, &win_h);

    if (win_w <= 0 || win_h <= 0) {
        rect->x = 0;
        rect->y = 0;
        rect->w = app->latest.width;
        rect->h = app->latest.height;
        return;
    }

    aspect = (double)app->latest.width / (double)app->latest.height;

    dst_h = win_h;
    dst_w = (int)(dst_h * aspect) & ~1;

    if (dst_w > win_w) {
        dst_w = win_w;
        dst_h = (int)(dst_w / aspect) & ~1;
    }

    rect->x = (win_w - dst_w) / 2;
    rect->y = (win_h - dst_h) / 2;
    rect->w = dst_w;
    rect->h = dst_h;
}

static int display_copy_latest_to_texture(AppState *app)
{
    void *pixels = NULL;
    int pitch = 0;
    int y;

    if (!app || !app->texture || !app->latest.rgb) {
        return -1;
    }

    if (SDL_LockTexture(app->texture, NULL, &pixels, &pitch) != 0) {
        LOG_ERROR("SDL_LockTexture failed: %s", SDL_GetError());
        return -1;
    }

    for (y = 0; y < app->latest.height; y++) {
        memcpy((unsigned char *)pixels + y * pitch,
               app->latest.rgb + (size_t)y * (size_t)app->latest.width * 3,
               (size_t)app->latest.width * 3);
    }

    SDL_UnlockTexture(app->texture);
    return 0;
}

static int display_snapshot_latest_rgb(AppState *app,
                                       unsigned char **rgb_copy,
                                       int *width,
                                       int *height,
                                       size_t *bytes,
                                       uint64_t *frame_id)
{
    if (rgb_copy) {
        *rgb_copy = NULL;
    }
    if (width) {
        *width = 0;
    }
    if (height) {
        *height = 0;
    }
    if (bytes) {
        *bytes = 0;
    }
    if (frame_id) {
        *frame_id = 0;
    }

    if (!app || !app->latest.mutex || !app->latest.rgb || !rgb_copy) {
        return -1;
    }

    SDL_LockMutex(app->latest.mutex);

    if (app->latest.frame_id == 0 ||
        app->latest.width <= 0 ||
        app->latest.height <= 0 ||
        app->latest.bytes == 0) {
        SDL_UnlockMutex(app->latest.mutex);
        return -1;
    }

    *rgb_copy = (unsigned char *)malloc(app->latest.bytes);
    if (!*rgb_copy) {
        SDL_UnlockMutex(app->latest.mutex);
        perror("malloc display latest snapshot");
        return -1;
    }

    memcpy(*rgb_copy, app->latest.rgb, app->latest.bytes);

    if (width) {
        *width = app->latest.width;
    }
    if (height) {
        *height = app->latest.height;
    }
    if (bytes) {
        *bytes = app->latest.bytes;
    }
    if (frame_id) {
        *frame_id = app->latest.frame_id;
    }

    SDL_UnlockMutex(app->latest.mutex);
    return 0;
}

static int display_write_ppm_file(const char *filename,
                                  const unsigned char *rgb,
                                  int width,
                                  int height)
{
    FILE *fp;
    int y;

    if (!filename || !rgb || width <= 0 || height <= 0) {
        return -1;
    }

    fp = fopen(filename, "wb");
    if (!fp) {
        perror("fopen");
        return -1;
    }

    fprintf(fp, "P6\n%d %d\n255\n", width, height);

    for (y = 0; y < height; y++) {
        const unsigned char *line = rgb + (size_t)y * (size_t)width * 3;

        if (fwrite(line, 1, (size_t)width * 3, fp) != (size_t)width * 3) {
            perror("fwrite");
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);
    return 0;
}

int display_init(AppState *app)
{
    if (!app) {
        return -1;
    }

    app->window = SDL_CreateWindow("v4l2_camera_preview",
                                   SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED,
                                   app->width,
                                   app->height,
                                   SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!app->window) {
        LOG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
        return -1;
    }

    app->renderer = SDL_CreateRenderer(app->window,
                                       -1,
                                       SDL_RENDERER_ACCELERATED |
                                           SDL_RENDERER_PRESENTVSYNC);
    if (!app->renderer) {
        LOG_ERROR("SDL_CreateRenderer failed: %s", SDL_GetError());
        display_destroy(app);
        return -1;
    }

    app->texture = SDL_CreateTexture(app->renderer,
                                     SDL_PIXELFORMAT_RGB24,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     app->width,
                                     app->height);
    if (!app->texture) {
        LOG_ERROR("SDL_CreateTexture failed: %s", SDL_GetError());
        display_destroy(app);
        return -1;
    }

    return 0;
}

void display_destroy(AppState *app)
{
    if (!app) {
        return;
    }

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

int display_present_latest(AppState *app)
{
    SDL_Rect rect;

    if (!app) {
        return -1;
    }

    if (!app->window || !app->renderer || !app->texture) {
        return -1;
    }

    if (!app->latest.rgb || !app->latest.mutex) {
        return 0;
    }

    SDL_LockMutex(app->latest.mutex);

    if (app->latest.frame_id == 0 ||
        app->latest.width <= 0 ||
        app->latest.height <= 0) {
        SDL_UnlockMutex(app->latest.mutex);
        return 0;
    }

    if (display_copy_latest_to_texture(app) < 0) {
        SDL_UnlockMutex(app->latest.mutex);
        return -1;
    }

    SDL_UnlockMutex(app->latest.mutex);

    display_get_target_rect(app, &rect);

    SDL_RenderClear(app->renderer);
    SDL_RenderCopy(app->renderer, app->texture, NULL, &rect);
    SDL_RenderPresent(app->renderer);

    return 0;
}

int display_save_latest_ppm(AppState *app, const char *filename)
{
    unsigned char *rgb_copy = NULL;
    int width = 0;
    int height = 0;
    size_t bytes = 0;
    uint64_t frame_id = 0;
    int ret;

    if (!app || !filename) {
        return -1;
    }

    ret = display_snapshot_latest_rgb(app,
                                      &rgb_copy,
                                      &width,
                                      &height,
                                      &bytes,
                                      &frame_id);
    if (ret < 0) {
        return -1;
    }

    (void)bytes;
    (void)frame_id;

    ret = display_write_ppm_file(filename, rgb_copy, width, height);
    free(rgb_copy);

    if (ret < 0) {
        return -1;
    }

    LOG_INFO("display latest frame saved: %s", filename);
    return 0;
}