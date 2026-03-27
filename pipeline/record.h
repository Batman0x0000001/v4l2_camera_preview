#ifndef RECORD_H
#define RECORD_H

#include"app.h"

void record_state_init(AppState *app,const char *path,int fps);
int record_init(AppState *app);
void record_close(AppState *app);

void record_notify_pause(AppState *app);

int record_session_start(AppState *app);
int record_session_stop(AppState *app);
#endif