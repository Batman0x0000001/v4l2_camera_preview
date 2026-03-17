#include<stdio.h>
#include<string.h>
#include"app.h"
#include"v4l2_core.h"
#include"display.h"
#include"stream.h"
#include"record.h"
#include"log.h"

static void print_control_status(const char *name, int value) {
    static char last_name[32] = "";
    static int  last_value    = 0;

    // 内容没变化，直接返回，不打印
    if (last_value == value && strcmp(last_name, name) == 0) {
        return;
    }

    // 内容变了，更新缓存并打印
    strncpy(last_name, name, sizeof(last_name) - 1);
    last_value = value;

    printf("\r%-60s", "");
    printf("\rControl: %-20s = %-6d", name, value);
    fflush(stdout);//每次 fflush 都要从用户态切换到内核态。
}

int main(int argc, char const *argv[])
{
    AppState app;
    const char *device = "/dev/video0";
    SDL_Event event;

    if(argc > 1){
        device = argv[1];
    }

    app_state_init(&app,device);
    stream_state_init(&app,"rtsp://127.0.0.1:8554/cam",25);
    record_state_init(&app,"record.mp4",25);

    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0){
        fprintf(stderr, "SDL_Init failed:%s\n",SDL_GetError());
        return -1;
    }

    app.last_stats_ms = SDL_GetTicks64();

    if(open_device(&app) < 0){
        cleanup(&app);
        SDL_Quit();
        return -1;
    }

    printf("V4L2 设备打开成功，能力检查通过。\n");

    if(query_capability(&app) < 0){
        cleanup(&app);
        SDL_Quit();
        return -1;
    }

    if (enum_formats(&app) < 0) {
        cleanup(&app);
        SDL_Quit();
        return 1;
    }

    if (enum_frame_sizes(&app, app.pixfmt) < 0) {
        cleanup(&app);
        SDL_Quit();
        return 1;
    }

    if (enum_frame_intervals(&app, app.pixfmt, app.width, app.height) < 0) {
        cleanup(&app);
        SDL_Quit();
        return 1;
    }

    if(enum_controls(&app) < 0){
        cleanup(&app);
        SDL_Quit();
        return 1;     
    }

    if (set_format(&app) < 0) {
        cleanup(&app);
        SDL_Quit();
        return 1;
    }

    explain_selected_format(app.pixfmt);

    if (init_mmap(&app) < 0) {
        cleanup(&app);
        SDL_Quit();
        return 1;
    }

    if(init_shared_frame(&app) < 0){
        cleanup(&app);
        SDL_Quit();
        return 1;
    }

    if(alloc_preview_rgb_buffer(&app) < 0){
        cleanup(&app);
        SDL_Quit();
        return 1;
    }

    if(alloc_capture_yuyv_buffer(&app) < 0){
        cleanup(&app);
        SDL_Quit();
        return 1;
    }

    if(display_init(&app) < 0){
        cleanup(&app);
        SDL_Quit();
        return 1;
    }

    if (stream_init(&app) < 0) {
        fprintf(stderr, "RTSP 推流初始化失败\n");
        display_destroy(&app);
        cleanup(&app);
        SDL_Quit();
        return 1;
    }

    if (record_init(&app) < 0) {
        fprintf(stderr, "MP4 录像初始化失败\n");
        display_destroy(&app);
        cleanup(&app);
        SDL_Quit();
        return 1;
    }

    if (start_capturing(&app) < 0) {
        cleanup(&app);
        SDL_Quit();
        return 1;
    }

    if(capture_start_thread(&app) < 0){
        display_destroy(&app);
        cleanup(&app);
        SDL_Quit();
        return 1;
    }

    scan_controls(&app);
    print_controls(&app);

    printf("运行中:Space 暂停/继续,[/] 调整亮度,T推流,R录像,ESC 退出\n");
    printf("推流地址:%s\n",app.stream.output_url);
    printf("本地录像:%s\n",app.record.output_path);

    while(!app.quit){
        while(SDL_PollEvent(&event)){
            if(event.type == SDL_QUIT){
                app.quit = 1;
            }else if (event.type == SDL_KEYDOWN)
            {
                switch (event.key.keysym.sym)
                {
                case SDLK_s:
                    if(display_save_latest_ppm(&app,"screenshot.ppm") < 0){
                        fprintf(stderr,"截屏失败\n");
                    }
                    break;
                case SDLK_t:
                    app.stream_on = !app.stream_on;
                    printf("RTSP推流%s\n",app.stream_on?"开启":"暂停");
                    break;
                case SDLK_r:
                    app.record_on = !app.record_on;
                    printf("录像%s\n",app.record_on?"开启":"暂停");
                    break;
                case SDLK_LEFTBRACKET:
                    {
                        struct v4l2_queryctrl qctrl;
                        int32_t value;

                        if(query_control_info(&app,V4L2_CID_BRIGHTNESS,&qctrl) == 0 &&
                            get_control_value(&app,V4L2_CID_BRIGHTNESS,&value) == 0){
                            value -=qctrl.step ? qctrl.step : 1;
                            if(value < qctrl.minimum){
                                value = qctrl.minimum;
                            }
                            if(set_control_value(&app,V4L2_CID_BRIGHTNESS,value) == 0){
                                printf("亮度调整为:%d\n",value);
                            }
                        }
                        break;
                    }
                case SDLK_RIGHTBRACKET:
                    {
                        struct v4l2_queryctrl qctrl;
                        int32_t value;

                        if(query_control_info(&app,V4L2_CID_BRIGHTNESS,&qctrl) == 0 &&
                            get_control_value(&app,V4L2_CID_BRIGHTNESS,&value) == 0){
                            value +=qctrl.step ? qctrl.step : 1;
                            if(value > qctrl.maximum){
                                value = qctrl.maximum;
                            }
                            if(set_control_value(&app,V4L2_CID_BRIGHTNESS,value) == 0){
                                printf("亮度调整为:%d\n",value);
                            }
                        }
                        break;
                    }
                case SDLK_e:
                    {

                        int32_t value = 0;

                        if (get_control_value(&app,V4L2_CID_POWER_LINE_FREQUENCY,&value) == 0) {
                            value++;
                            if (value > 2)
                                value = 0;

                            if (set_control_value(&app,V4L2_CID_POWER_LINE_FREQUENCY,value) == 0) {
                                printf("Power Line Frequency mode -> %d\n",value);
                            }
                        }
                        break;
                    }
                case SDLK_LEFT:
                    {
                        CameraControl *c = &app.controls[app.current_control];
                        int value;

                        if(get_control_by_index(&app,app.current_control,&value) == 0){
                            value -= c->step ? c->step : 1;

                            if(value < c->min){
                                value = c->min;
                            }

                            set_control_by_index(&app,app.current_control,value);
                        }
                        break;
                    }
                case SDLK_RIGHT:
                    {
                        CameraControl *c = &app.controls[app.current_control];
                        int value;

                        if(get_control_by_index(&app,app.current_control,&value) == 0){
                            value += c->step ? c->step : 1;

                            if(value > c->max){
                                value = c->max;
                            }

                            set_control_by_index(&app,app.current_control,value);
                        }
                        break;
                    }
                case SDLK_UP:
                    app.current_control++;
                    if(app.current_control >= app.control_count){
                        app.current_control = 0;
                    }
                    break;
                case SDLK_DOWN:
                    app.current_control--;
                    if(app.current_control < 0){
                        app.current_control = app.control_count - 1;
                    }
                    break;
                case SDLK_ESCAPE:
                    app.quit = 1;
                    break;
                case SDLK_SPACE:
                    app.paused = !app.paused;
                    printf("预览%s\n",app.paused?"暂停":"继续");
                    break;
                default:
                    break;
                }
            }    
        }

        if(display_present_latest(&app) < 0){
            fprintf(stderr,"显示失败\n");
            break;
        }
        {
            uint64_t now_ms = SDL_GetTicks64();
            if(now_ms - app.last_stats_ms >= 1000){
                LOG_INFO("capture=%llu dropped=%llu stream_q_drop=%llu record_q_drop=%llu\n stream_encoded=%llu record_encoded=%llu latest_seq=%u latest_bytes=%u\n",
                        (unsigned long long)app.frames_captured,
                        (unsigned long long)app.frames_dropped,
                        (unsigned long long)app.stream.queue.dropped_frames,
                        (unsigned long long)app.record.queue.dropped_frames,
                        (unsigned long long)app.stream.frames_encoded,
                        (unsigned long long)app.record.frames_encoded,
                        app.latest.meta.sequence,
                        app.latest.meta.bytesused);
                app.last_stats_ms = now_ms;
    }
        }
        CameraControl *c = &app.controls[app.current_control];
        int value;
        /*
            或者直接调用get_control_value(&app, app->controls[index].id, &value) or
            get_control_value(&app, c->id, &value)
        */
        if(get_control_by_index(&app,app.current_control,&value) == 0){
            print_control_status(c->name,value);
        }

        SDL_Delay(5);
    }

    display_destroy(&app);
    cleanup(&app);
    SDL_Quit();

    return 0;
}