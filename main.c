#include<stdio.h>
#include<string.h>
#include"app.h"
#include"v4l2_core.h"
#include"display.h"
#include"stream.h"
#include"record.h"
#include"log.h"
#include"app_ctrl.h"

int main(int argc, char const *argv[])
{
    AppState app;
    const char *device = "/dev/video0";
    SDL_Event event;

    if(argc > 1){
        device = argv[1];
    }

    app_state_init(&app,device);
    stream_state_init(&app,"rtsp://127.0.0.1:8554/cam",30);
    record_state_init(&app,"record.mp4",30);

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

    app_print_help();
    app_print_runtime_state(&app);
    app_print_current_control_status(&app);

    while(!app.quit){
        while(SDL_PollEvent(&event)){
            if(event.type == SDL_QUIT){
                app.quit = 1;
            }else if (event.type == SDL_KEYDOWN)
            {
                switch (event.key.keysym.sym)
                {
                case SDLK_s:
                    app_save_snapshot(&app,"snapshot.ppm");
                    break;
                case SDLK_t:
                    app_toggle_stream(&app);
                    break;
                case SDLK_r:
                   app_toggle_record(&app);
                    break;
                case SDLK_h:
                    app_print_help();
                    break;
                case SDLK_i:
                    app_print_runtime_state(&app);
                    app_print_current_control_status(&app);
                    break;
                case SDLK_LEFT:
                    app_adjust_current_control(&app,-1);
                    break;
                case SDLK_RIGHT:
                    app_adjust_current_control(&app,+1);
                    break;
                case SDLK_UP:
                    app_select_next_control(&app);
                    break;
                case SDLK_DOWN:
                    app_select_prev_control(&app);
                    break;
                case SDLK_ESCAPE:
                    app.quit = 1;
                    break;
                case SDLK_SPACE:
                    app_toggle_pause(&app);
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
            if(app.stream_on && app.stream.fatal_error){
                LOG_WARN("stream disabled due to fatal error");
                app.stream_on = 0;
            }

            if(app.record_on && app.record.fatal_error){
                LOG_WARN("record disabled due to fatal error");
                app.record_on = 0;
            }
        }
    }

    display_destroy(&app);
    cleanup(&app);
    SDL_Quit();

    return 0;
}