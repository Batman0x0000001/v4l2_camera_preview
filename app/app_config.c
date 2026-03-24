#include <stdio.h>
#include <string.h>
#include "app_config.h"
#include "log.h"

void app_config_init_default(AppConfig *cfg){
    if(!cfg){
        return;
    }

    memset(cfg, 0, sizeof(*cfg));

    strncpy(cfg->device_path, "/dev/video0", sizeof(cfg->device_path) - 1);
    cfg->width = 640;
    cfg->height = 480;
    cfg->fps = 30;

    strncpy(cfg->stream_url, "rtsp://127.0.0.1:8554/cam", sizeof(cfg->stream_url) - 1);
    strncpy(cfg->record_path, "record.mp4", sizeof(cfg->record_path) - 1);
    strncpy(cfg->snapshot_path, "snapshot.ppm", sizeof(cfg->snapshot_path) - 1);

    cfg->start_stream_on = 1;
    cfg->start_record_on = 1;

    strncpy(cfg->audio_device,"default",sizeof(cfg->audio_device) - 1);
    cfg->audio_sample_rate = 48000;
    cfg->audio_channels = 2;
    cfg->audio_period_frames = 2048;
}

void app_print_banner(void){
    LOG_INFO("========================================");
    LOG_INFO(" Linux Camera Media Pipeline Demo");
    LOG_INFO(" V4L2 + SDL + FFmpeg + RTSP + MP4");
    LOG_INFO(" version: 1.0");
    LOG_INFO("========================================");
}

void app_print_config(const AppConfig *cfg){
    if(!cfg){
        return;
    }

    LOG_INFO("startup config:");
    LOG_INFO("  device_path=%s", cfg->device_path);
    LOG_INFO("  width=%d height=%d fps=%d", cfg->width, cfg->height, cfg->fps);
    LOG_INFO("  stream_url=%s", cfg->stream_url);
    LOG_INFO("  record_path=%s", cfg->record_path);
    LOG_INFO("  snapshot_path=%s", cfg->snapshot_path);
    LOG_INFO("  start_stream_on=%d start_record_on=%d",
             cfg->start_stream_on,
             cfg->start_record_on);
    LOG_INFO("  audio_device=%s", cfg->audio_device);
    LOG_INFO("  audio_sample_rate=%u audio_channels=%d audio_period_frames=%u",
         cfg->audio_sample_rate,
         cfg->audio_channels,
         cfg->audio_period_frames);
}