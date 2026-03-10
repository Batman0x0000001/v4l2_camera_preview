#include<stdio.h>
#include"app.h"
#include"v4l2_core.h"

int main(int argc, char const *argv[])
{
    AppState app;
    const char *device = "/dev/video0";

    if(argc > 1){
        device = argv[1];
    }

    app_state_init(&app,device);

    if(open_device(&app) < 0){
        cleanup(&app);
        return -1;
    }
    if(query_capability(&app) < 0){
        cleanup(&app);
        return -1;
    }

    printf("V4L2 设备打开成功，能力检查通过。\n");

    if (enum_frame_sizes(&app, app.pixfmt) < 0) {
        cleanup(&app);
        return 1;
    }

    if (enum_frame_intervals(&app, app.pixfmt, app.width, app.height) < 0) {
        cleanup(&app);
        return 1;
    }

    if (set_format(&app) < 0) {
        cleanup(&app);
        return 1;
    }

    explain_selected_format(app.pixfmt);

    if (init_mmap(&app) < 0) {
        cleanup(&app);
        return 1;
    }

    if (start_capturing(&app) < 0) {
        cleanup(&app);
        return 1;
    }

    if (capture_one_frame(&app, "frame.raw") < 0) {
        cleanup(&app);
        return 1;
    }

    printf("已成功导出一帧到 frame.raw\n");

    cleanup(&app);
    return 0;
}