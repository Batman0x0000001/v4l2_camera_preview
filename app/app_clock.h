#ifndef APP_CLOCK_H
#define APP_CLOCK_H

#include"app.h"
#include<stdint.h>

int app_clock_init(AppState *app);
void app_clock_destroy(AppState *app);

int app_is_paused(AppState *app);
void app_pause_begin(AppState *app);
void app_pause_end(AppState *app);

uint64_t app_total_paused_us(AppState *app);
uint64_t app_media_clock_us(AppState *app);
#endif