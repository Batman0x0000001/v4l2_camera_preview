#ifndef STREAM_H
#define STREAM_H

#include"app.h"

void stream_state_init(AppState *app,const char *url,int fps);
int stream_init(AppState *app);
void stream_close(AppState *app);

#endif