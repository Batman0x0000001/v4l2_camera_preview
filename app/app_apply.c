#include<string.h>
#include"app_apply.h"

void app_apply_config(AppState *app,const AppConfig *cfg){
    if(!app || !cfg){
        return;
    }

    snprintf(app->device_path,sizeof(app->device_path),"%s",cfg->device_path);
    app->width = cfg->width;
    app->height = cfg->height;
    app->stream_on = cfg->start_stream_on;
    app->record_on = cfg->start_record_on;
}