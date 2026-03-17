#ifndef RECORD_H
#define RECORD_H

#include"app.h"

void record_state_init(AppState *app,const char *path,int fps);
int record_init(AppState *app);
void record_close(AppState *app);

#endif