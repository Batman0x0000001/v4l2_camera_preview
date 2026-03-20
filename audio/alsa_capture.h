#ifndef ALSA_CAPTURE_H
#define ALSA_CAPTURE_H

#include"app.h"

void audio_state_init(AppState *app,const char *device_name,unsigned int sample_rate,int channels,unsigned int period_frames);
int audio_init(AppState *app);
void audio_close(AppState *app);

#endif