#include<stdio.h>
#include<string.h>
#include"app.h"
#include"v4l2_core.h"
#include"display.h"
#include"stream.h"
#include"record.h"
#include"log.h"
#include"app_ctrl.h"
#include"app_config.h"
#include"app_apply.h"
#include"app_startup.h"

int main()
{
    AppState app;
    AppConfig cfg;
    SDL_Event event;

    app_config_init_default(&cfg);
    app_print_banner();
    app_print_config(&cfg);
    app_apply_config(&app,&cfg);
    app_state_init(&app,cfg.device_path);
    stream_state_init(&app,cfg.stream_url,cfg.fps);
    record_state_init(&app,cfg.record_path,cfg.fps);

    if(app_startup(&app) < 0){
        app_shutdowm(&app);
        return 1;
    }

    scan_controls(&app);
    print_controls(&app);

    app_print_help();
    app_print_runtime_state(&app);
    app_print_current_control_status(&app);
    app_print_module_overview(&app);
    while(!app.quit){
        while(SDL_PollEvent(&event)){
            if(event.type == SDL_QUIT){
                app.quit = 1;
            }else if (event.type == SDL_KEYDOWN)
            {
                switch (event.key.keysym.sym)
                {
                case SDLK_s:
                    app_save_snapshot(&app,cfg.snapshot_path);
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
    SDL_Quit();
    app_shutdowm(&app);

    return 0;
}