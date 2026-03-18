#ifndef APP_CTRL_H
#define APP_CTRL_H

#include "app.h"

void app_print_help(void);
void app_print_runtime_state(const AppState *app);
void app_print_current_control_status(AppState *app);

void app_toggle_pause(AppState *app);
void app_toggle_stream(AppState *app);
void app_toggle_record(AppState *app);

void app_select_next_control(AppState *app);
void app_select_prev_control(AppState *app);
void app_adjust_current_control(AppState *app, int delta);

int app_save_snapshot(AppState *app, const char *path);

#endif