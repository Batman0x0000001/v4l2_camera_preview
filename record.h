#ifndef RECORD_H
#define RECORD_H

#include"app.h"

void record_state_init(AppState *app,const char *path,int fps);
int record_init(AppState *app);
int record_push_rgb_frame(AppState *app,const unsigned char *rgb);
void record_close(AppState *app);

#endif