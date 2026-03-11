#include<stdio.h>
#include"app.h"
#include"v4l2_core.h"
#include"display.h"

int main(int argc, char const *argv[])
{
    AppState app;
    const char *device = "/dev/video0";
    SDL_Event event;

    if(argc > 1){
        device = argv[1];
    }

    app_state_init(&app,device);

    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0){
        fprintf(stderr, "SDL_Init failed:%s\n",SDL_GetError());
        return -1;
    }

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

    int32_t value =0;

    if(get_control_value(&app,V4L2_CID_BRIGHTNESS,&value) == 0){
        printf("当前的亮度为:%d\n",value);
    }else{
        printf("当前设备不支持读取亮度\n");
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

    if (start_capturing(&app) < 0) {
        cleanup(&app);
        SDL_Quit();
        return 1;
    }

    if(init_shared_frame(&app) < 0){
        cleanup(&app);
        SDL_Quit();
        return 1;
    }

    if(display_init(&app) < 0){
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

    printf("运行中:Space 暂停/继续,[/] 调整亮度,ESC 退出\n");

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
                                printf("Power Line Frequency mode -> %d\n",
                                        value);
                            }
                        }
                        break;
                    }
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

        SDL_Delay(5);
    }

    printf("开始 display_destroy\n");
    display_destroy(&app);
    printf("完成 display_destroy\n");

    printf("开始 cleanup\n");
    cleanup(&app);
    printf("完成 cleanup\n");

    printf("开始 SDL_Quit\n");
    SDL_Quit();
    printf("完成 SDL_Quit\n");

    return 0;
}