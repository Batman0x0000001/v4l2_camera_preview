#ifndef V4L2_CORE_H
#define V4L2_CORE_H

#include"app.h"

void app_state_init(AppState *app,const char *device_path);

int open_device(AppState *app);
int query_capability(AppState *app);

int enum_formats(AppState *app);
int enum_frame_sizes(AppState *app,uint32_t pixfmt);
int enum_frame_intervals(AppState *app,uint32_t pixfmt,unsigned int width,unsigned int height);

int set_format(AppState *app);
void explain_selected_format(uint32_t pixfmt);

int init_mmap(AppState *app);
int start_capturing(AppState *app);
void stop_capturing(AppState *app);
void uninit_mmap(AppState *app);

int capture_one_frame(AppState *app,const char *output_path);
int capture_one_frame_as_ppm(AppState *app,const char *output_path);

int init_shared_frame(AppState *app);
int capture_start_thread(AppState *app);

void cleanup(AppState *app);

#endif