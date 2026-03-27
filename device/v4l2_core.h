#ifndef V4L2_CORE_H
#define V4L2_CORE_H

#include "app.h"

/*
 * v4l2_core 模块职责：
 * 1. 管理视频设备的打开/配置/关闭；
 * 2. 管理 mmap 缓冲区；
 * 3. 启动采集线程并把采集帧送往显示/推流/录像模块；
 * 4. 提供 V4L2 control 的查询与设置接口。
 *
 * 注意：
 * - 这里只处理“视频采集核心”；
 * - 不负责应用级生命周期清理，统一由 app_startup/app_shutdown 管理。
 */

void app_state_init(AppState *app, const char *device_path);

int open_device(AppState *app);
int query_capability(AppState *app);

int enum_formats(AppState *app);
int enum_frame_sizes(AppState *app, uint32_t pixfmt);
int enum_frame_intervals(AppState *app,
                         uint32_t pixfmt,
                         unsigned int width,
                         unsigned int height);

int set_format(AppState *app);
void explain_selected_format(uint32_t pixfmt);

int init_mmap(AppState *app);
int start_capturing(AppState *app);
void stop_capturing(AppState *app);
void uninit_mmap(AppState *app);

int capture_one_frame(AppState *app, const char *output_path);
int capture_one_frame_as_ppm(AppState *app, const char *output_path);

int init_shared_frame(AppState *app);
int capture_start_thread(AppState *app);

int enum_controls(AppState *app);
int get_control_value(AppState *app, uint32_t id, int32_t *value);
int set_control_value(AppState *app, uint32_t id, int32_t value);

int query_control_info(AppState *app, uint32_t id, struct v4l2_queryctrl *out);
int enum_control_menu(AppState *app, struct v4l2_queryctrl *qctrl);

int scan_controls(AppState *app);
void print_controls(AppState *app);
int get_control_by_index(AppState *app, int index, int *value);
int set_control_by_index(AppState *app, int index, int value);

int alloc_preview_rgb_buffer(AppState *app);
int alloc_capture_yuyv_buffer(AppState *app);

#endif