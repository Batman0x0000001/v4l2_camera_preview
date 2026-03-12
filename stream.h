#ifndef STREAM_H
#define STREAM_H

#include"app.h"

void stream_state_init(AppState *app,const char *url,int fps);
int stream_init(AppState *app);
int stream_push_rgb_frame(AppState *app,const unsigned char *rgb);
void stream_close(AppState *app);

#endif