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

    printf("运行中:Space 暂停/继续,ESC 退出\n");

    while(!app.quit){
        while(SDL_PollEvent(&event)){
            if(event.type == SDL_QUIT){
                app.quit = 1;
            }else if (event.type == SDL_KEYDOWN)
            {
                switch (event.key.keysym.sym)
                {
                case SDLK_ESCAPE:
                    app.quit = 1;
                    break;
                case SDLK_SPACE:
                    app.paused = !app.paused;
                    break;
                default:
                    break;
                }
            }    
        }

        if(display_present_latest(&app) < 0){
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