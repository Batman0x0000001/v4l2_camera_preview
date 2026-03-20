#include <stdio.h>
#include <string.h>
#include "app_ctrl.h"
#include "display.h"
#include "v4l2_core.h"
#include "log.h"

void app_print_help(void){
    LOG_INFO("keyboard help:======================================");
    LOG_INFO("  esc            quit");
    LOG_INFO("  space          pause/resume capture processing");
    LOG_INFO("  t              toggle RTSP streaming");
    LOG_INFO("  r              toggle MP4 recording");
    LOG_INFO("  s              save snapshot");
    LOG_INFO("  up/down        select V4L2 control");
    LOG_INFO("  left/right     adjust current control");
    LOG_INFO("  h              print help");
    LOG_INFO("  i              print runtime state");
}

void app_print_runtime_state(const AppState *app){
    int stream_audio_q_size = 0;
    int record_audio_q_size = 0;
    uint64_t stream_audio_dropped = 0;
    uint64_t record_audio_dropped = 0;

    if(!app){
        return;
    }

    if(app->stream.audio_queue.mutex){
        SDL_LockMutex(app->stream.audio_queue.mutex);
        stream_audio_q_size = app->stream.audio_queue.size;
        stream_audio_dropped = app->stream.audio_queue.dropped_chunks;
        SDL_UnlockMutex(app->stream.audio_queue.mutex);
    }

    if(app->record.audio_queue.mutex){
        SDL_LockMutex(app->record.audio_queue.mutex);
        record_audio_q_size = app->record.audio_queue.size;
        record_audio_dropped = app->record.audio_queue.dropped_chunks;
        SDL_UnlockMutex(app->record.audio_queue.mutex);
    }

    LOG_INFO("runtime state:=========================================");
    LOG_INFO("  paused=%d", app->paused);
    LOG_INFO("  stream_on=%d enabled=%d accepting=%d fatal=%d",
        app->stream_on,
        app->stream.enabled,
        app->stream.accepting_frames,
        app->stream.fatal_error);
    LOG_INFO("  record_on=%d enabled=%d accepting=%d fatal=%d",
        app->record_on,
        app->record.enabled,
        app->record.accepting_frames,
        app->record.fatal_error);
    LOG_INFO("  frames_captured=%llu frames_dropped=%llu",
        (unsigned long long)app->frames_captured,
        (unsigned long long)app->frames_dropped);
    LOG_INFO("  latest_frame_id=%llu latest_seq=%u latest_bytes=%u",
        (unsigned long long)app->latest.frame_id,
        app->latest.meta.sequence,
        app->latest.meta.bytesused);

        
    LOG_INFO("  audio enabled=%d running=%d fatal=%d xruns=%llu",
        app->audio.enabled,
        app->audio.running,
        app->audio.fatal_error,
        (unsigned long long)app->audio.xruns);

    LOG_INFO("  audio chunks=%llu pcm_frames=%llu last_chunk_frames=%llu last_capture_us=%llu",
        (unsigned long long)app->audio.chunks_captured,
        (unsigned long long)app->audio.pcm_frames_captured,
        (unsigned long long)app->audio.last_chunk_frames,
        (unsigned long long)app->audio.last_capture_time_us);
             
    LOG_INFO("  stream_audio_queue size=%d dropped=%llu",
        stream_audio_q_size,
        (unsigned long long)stream_audio_dropped);
    LOG_INFO("  record_audio_queue size=%d dropped=%llu",
        record_audio_q_size,
        (unsigned long long)record_audio_dropped);
}

void app_print_current_control_status(AppState *app){
    CameraControl *c;
    int32_t value;

    if(!app){
        return;
    }

    if(app->control_count <= 0){
        LOG_WARN("no V4L2 controls available");
        return;
    }

    if(app->current_control < 0 || app->current_control >= app->control_count){
        LOG_WARN("current_control out of range: %d", app->current_control);
        return;
    }

    c = &app->controls[app->current_control];
    get_control_value(app, c->id,&value);
    if(value < c->min || value > c->max){
        LOG_WARN("failed to get control value: %s", c->name);
        return;
    }

    LOG_INFO("control[%d/%d]: name=%s id=0x%x value=%d range=[%d,%d] step=%d default=%d",
             app->current_control + 1,
             app->control_count,
             c->name,
             c->id,
             value,
             c->min,
             c->max,
             c->step,
             c->def);
}

void app_toggle_pause(AppState *app){
    if(!app){
        return;
    }

    app->paused = !app->paused;
    LOG_INFO("pause toggled: paused=%d", app->paused);
}

void app_toggle_stream(AppState *app){
    if(!app){
        return;
    }

    if(!app->stream.enabled){
        LOG_WARN("stream module is not enabled");
        return;
    }

    if(app->stream.fatal_error){
        LOG_WARN("stream module is in fatal_error state");
        return;
    }

    app->stream_on = !app->stream_on;
    LOG_INFO("stream toggled: stream_on=%d", app->stream_on);
}

void app_toggle_record(AppState *app){
    if(!app){
        return;
    }

    if(!app->record.enabled){
        LOG_WARN("record module is not enabled");
        return;
    }

    if(app->record.fatal_error){
        LOG_WARN("record module is in fatal_error state");
        return;
    }

    app->record_on = !app->record_on;
    LOG_INFO("record toggled: record_on=%d", app->record_on);
}

void app_select_next_control(AppState *app){
    if(!app){
        return;
    }

    if(app->control_count <= 0){
        LOG_WARN("no V4L2 controls available");
        return;
    }

    app->current_control++;
    if(app->current_control >= app->control_count){
        app->current_control = 0;
    }

    app_print_current_control_status(app);
}

void app_select_prev_control(AppState *app){
    if(!app){
        return;
    }

    if(app->control_count <= 0){
        LOG_WARN("no V4L2 controls available");
        return;
    }

    app->current_control--;
    if(app->current_control < 0){
        app->current_control = app->control_count - 1;
    }

    app_print_current_control_status(app);
}

void app_adjust_current_control(AppState *app, int delta){
    CameraControl *c;
    int32_t current_value;
    int new_value;
    int step;

    if(!app){
        return;
    }

    if(app->control_count <= 0){
        LOG_WARN("no V4L2 controls available");
        return;
    }

    if(app->current_control < 0 || app->current_control >= app->control_count){
        LOG_WARN("current_control out of range: %d", app->current_control);
        return;
    }

    c = &app->controls[app->current_control];

    get_control_value(app, c->id,&current_value);
    if(current_value < c->min || current_value > c->max){
        LOG_WARN("failed to get current control value: %s", c->name);
        return;
    }

    step = (c->step > 0) ? c->step : 1;
    new_value = current_value + delta * step;

    if(new_value < c->min){
        new_value = c->min;
    }
    if(new_value > c->max){
        new_value = c->max;
    }

    if(new_value == current_value){
        LOG_INFO("control unchanged: %s value=%d", c->name, current_value);
        return;
    }

    if(set_control_value(app, c->id, new_value) < 0){
        LOG_WARN("failed to set control: %s -> %d", c->name, new_value);
        return;
    }

    LOG_INFO("control updated: %s %d -> %d", c->name, current_value, new_value);
    app_print_current_control_status(app);
}

int app_save_snapshot(AppState *app,const char* filename){
    FILE *fp = NULL;
    
    if(!app || !app->latest.rgb || !filename){
        return -1;
    }

    SDL_LockMutex(app->latest.mutex);

    if(app->latest.frame_id == 0){
        SDL_UnlockMutex(app->latest.mutex);
        return -1;
    }

    fp = fopen(filename,"wb");
    if(!fp){
        SDL_UnlockMutex(app->latest.mutex);
        perror("fopen");
        return -1;
    }

    fprintf(fp,"P6\n%d %d\n255\n",app->latest.width,app->latest.height);

    for (int y = 0; y < app->latest.height; y++)
    {
        if(fwrite(
            app->latest.rgb + y*app->latest.width*3,
            1,
            app->latest.width*3,
            fp)!=(size_t)app->latest.width*3){
            perror("fwrite");
            fclose(fp);
            SDL_UnlockMutex(app->latest.mutex);
            return -1;
        }
    }

    fclose(fp);
    SDL_UnlockMutex(app->latest.mutex);

    LOG_INFO("snapshot saved: %s", filename);
    return 0;
}

void app_print_module_overview(AppState *app){
    if(!app){
        return;
    }

    LOG_INFO("module overview:=========================================");
    LOG_INFO("  capture: device=%s size=%dx%d pixfmt=0x%x bytesperline=%u sizeimage=%u",
             app->device_path,
             app->width,
             app->height,
             app->pixfmt,
             app->bytesperline,
             app->sizeimage);

    LOG_INFO("  display: latest_rgb_bytes=%zu", app->latest.bytes);

    LOG_INFO("  stream: url=%s enabled=%d accepting=%d fatal=%d",
             app->stream.output_url,
             app->stream.enabled,
             app->stream.accepting_frames,
             app->stream.fatal_error);

    LOG_INFO("  record: path=%s enabled=%d accepting=%d fatal=%d",
             app->record.output_path,
             app->record.enabled,
             app->record.accepting_frames,
             app->record.fatal_error);
}