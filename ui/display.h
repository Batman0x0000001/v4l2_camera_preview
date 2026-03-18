#ifndef DISPLAY_H
#define DISPLAY_H

#include"app.h"

int display_init(AppState *app);
void display_destroy(AppState *app);
int display_present_latest(AppState *app);
int display_save_latest_ppm(AppState *app,const char* filename);
#endif