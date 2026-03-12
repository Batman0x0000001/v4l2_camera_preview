#include<stdio.h>
#include<string.h>
#include"record.h"

void record_state_init(AppState *app,const char *url,int fps){
    snprintf(app->record.output_path,sizeof(app->record.output_path),"%s",url);
    app->record.fps = fps > 0 ? fps : 25;
    app->record.frame_index = 0;
    app->record.enabled = 0;
    app->record.mutex = NULL;
}

static int record_init_encoder(AppState *app){
    //只要涉及容器（MP4/MKV/RTSP），就需要 codecpar；纯编解码不需要。
    int ret;

    app->record.encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    if(!app->record.encoder){
        fprintf(stderr, "avcodec_find_encoder (record) failed\n");
        return -1;
    }

    app->record.enc_ctx = avcodec_alloc_context3(app->record.encoder);
    if(!app->record.enc_ctx){
        fprintf(stderr, "avcodec_alloc_context3 (record) failed\n");
        return -1;
    }

    app->record.enc_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
    app->record.enc_ctx->width = app->width;
    app->record.enc_ctx->height = app->height;
    app->record.enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    app->record.enc_ctx->time_base = (AVRational){1, app->record.fps};
    app->record.enc_ctx->framerate = (AVRational){app->record.fps, 1};
    app->record.enc_ctx->gop_size = app->record.fps;
    app->record.enc_ctx->max_b_frames = 0;
    app->record.enc_ctx->bit_rate = 1000000;


    /*
    H264（libx264）常用参数
    速度与质量：
    preset：编码速度，越慢质量越好
        av_opt_set(enc_ctx->priv_data, "preset", "ultrafast", 0);
        av_opt_set(enc_ctx->priv_data, "preset", "superfast", 0);
        av_opt_set(enc_ctx->priv_data, "preset", "veryfast",  0);  // 推流常用
        av_opt_set(enc_ctx->priv_data, "preset", "faster",    0);
        av_opt_set(enc_ctx->priv_data, "preset", "fast",      0);
        av_opt_set(enc_ctx->priv_data, "preset", "medium",    0);  // 默认
        av_opt_set(enc_ctx->priv_data, "preset", "slow",      0);
        av_opt_set(enc_ctx->priv_data, "preset", "veryslow",  0);  // 录制存档用
    tune：针对特定场景优化：
        cav_opt_set(enc_ctx->priv_data, "tune", "zerolatency", 0); // 直播推流，最低延迟
        av_opt_set(enc_ctx->priv_data, "tune", "film",        0); // 电影，保留细节
        av_opt_set(enc_ctx->priv_data, "tune", "animation",   0); // 动画
        av_opt_set(enc_ctx->priv_data, "tune", "grain",       0); // 保留噪点/胶片感
        av_opt_set(enc_ctx->priv_data, "tune", "stillimage",  0); // 静态图像
    profile：兼容性控制：
        cav_opt_set(enc_ctx->priv_data, "profile", "baseline", 0); // 最高兼容性（老设备）
        av_opt_set(enc_ctx->priv_data, "profile", "main",     0); // 中等
        av_opt_set(enc_ctx->priv_data, "profile", "high",     0); // 最高质量，现代设备
    */
    av_opt_set(app->record.enc_ctx->priv_data,"preset","veryfast",0);

    ret = avcodec_open2(app->record.enc_ctx,app->record.encoder,NULL);
    if(ret < 0){
        fprintf(stderr, "avcodec_open2 (record) failed\n");
        return -1;
    }

    return 0;
}

static int record_init_buffers(AppState *app){
    int ret;

    app->record.sws_ctx = sws_getContext(
        app->width,
        app->height,
        AV_PIX_FMT_RGB24,
        app->width,
        app->height,
        AV_PIX_FMT_YUV420P,
        SWS_BILINEAR,
        NULL,NULL,NULL);
    if(!app->record.sws_ctx){
        fprintf(stderr, "sws_getContext (record) failed\n");
        return -1;
    }

    app->record.yuv_frame = av_frame_alloc();
    if(!app->record.yuv_frame){
        fprintf(stderr, "av_frame_alloc (record) failed\n");
        return -1;
    }

    app->record.yuv_frame->format = AV_PIX_FMT_YUV420P;
    app->record.yuv_frame->width = app->width;
    app->record.yuv_frame->height = app->height;

    ret = av_frame_get_buffer(app->record.yuv_frame,32);
    if(ret < 0){
        fprintf(stderr, "av_frame_get_buffer (record) failed\n");
        return -1;
    }

    app->record.pkt = av_packet_alloc();
    if(!app->record.pkt){
        fprintf(stderr, "av_packet_alloc (record) failed\n");
        return -1;
    }

    app->record.mutex = SDL_CreateMutex();
    if(!app->record.mutex){
        fprintf(stderr, "SDL_CreateMutex (record) failed\n");
        return -1;
    }

    return 0;
}

static int record_init_output(AppState *app){
    int ret;

    ret = avformat_alloc_output_context2(
        &app->record.ofmt_ctx,
        NULL,
        "mp4",
        app->record.output_path);
    if(ret < 0 || !app->record.ofmt_ctx){
        fprintf(stderr, "avformat_alloc_output_context2 (record) failed\n");
        return -1;
    }

    app->record.video_st = avformat_new_stream(app->record.ofmt_ctx,NULL);
    if(!app->record.video_st){
        fprintf(stderr, "avformat_new_record (record) failed\n");
        return -1;
    }

    app->record.video_st->time_base = app->record.enc_ctx->time_base;

    ret = avcodec_parameters_from_context(app->record.video_st->codecpar,app->record.enc_ctx);

    if(ret < 0){
        fprintf(stderr, "avcodec_parameters_from_context (record) failed\n");
        return -1;
    }

    /*
        & → 问"有没有这个标志"  → 用于判断
        | → 说"加上这个标志"    → 用于赋值
        设置（用 |=）
            ofmt_ctx->oformat->flags |= AVFMT_NOFILE;  // 添加标志
            ofmt_ctx->oformat->flags &= ~AVFMT_NOFILE; // 移除标志
    */
    if(!(app->record.ofmt_ctx->oformat->flags & AVFMT_NOFILE)){
        ret = avio_open2(
            &app->record.ofmt_ctx->pb,
            app->record.output_path,
            AVIO_FLAG_WRITE,
            NULL,NULL);
        if(ret < 0){
            fprintf(stderr, "avio_open2 (record) failed\n");
            return -1;
        }
    }

    ret = avformat_write_header(app->record.ofmt_ctx,NULL);
    if(ret < 0){
        fprintf(stderr, "avformat_write_header (record) failed\n");
        return -1;
    }
    
    return 0;
}

int record_init(AppState *app){
    if(record_init_encoder(app) < 0){
        return -1;
    }
    if(record_init_buffers(app) < 0){
        return -1;
    }
    if(record_init_output(app) < 0){
        return -1;
    }

    app->record.enabled = 1;
    
    return 0;
}

static int record_write_one_packet(AppState *app){
    int ret;

    while(1){
        ret = avcodec_receive_packet(app->record.enc_ctx,app->record.pkt);
        if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
            return 0;
        }
        if(ret < 0){
            return -1;
        }

        av_packet_rescale_ts(
            app->record.pkt,
            app->record.enc_ctx->time_base,
            app->record.video_st->time_base);
        app->record.pkt->stream_index = app->record.video_st->index;

        ret = av_interleaved_write_frame(app->record.ofmt_ctx,app->record.pkt);
        av_packet_unref(app->record.pkt);

        if(ret < 0){
            return -1;
        }
    }
}

static void record_flush_encoder(AppState *app){
    int ret;

    if(!app->record.enc_ctx || !app->record.ofmt_ctx){
        return;
    }

    ret = avcodec_send_frame(app->record.enc_ctx,NULL);
    if(ret < 0){
        return;
    }

    (void)record_write_one_packet(app);
}

int record_push_rgb_frame(AppState *app,const unsigned char *rgb){
    const uint8_t *src_slices[1];
    int src_stride[1];
    int ret;

    if(!app->record.enabled || !rgb){
        return 0;
    }

    SDL_LockMutex(app->record.mutex);

    ret = av_frame_make_writable(app->record.yuv_frame);
    if(ret < 0){
        SDL_UnlockMutex(app->record.mutex);
        return -1;
    }

    src_slices[0] = rgb;
    src_stride[0] = app->width*3;

    sws_scale(
        app->record.sws_ctx,
        src_slices,src_stride,
        0,
        app->height,
        app->record.yuv_frame->data,
        app->record.yuv_frame->linesize);
    
    app->record.yuv_frame->pts = app->record.frame_index++;

    ret = avcodec_send_frame(app->record.enc_ctx,app->record.yuv_frame);
    if(ret < 0){
        SDL_UnlockMutex(app->record.mutex);
        return -1;
    }

    ret = record_write_one_packet(app);
    SDL_UnlockMutex(app->record.mutex);

    if(ret < 0){
        return -1;
    }

    return 0;
}

void record_close(AppState *app)
{
    if (app->record.enabled) {
        SDL_LockMutex(app->record.mutex);
        record_flush_encoder(app);
        SDL_UnlockMutex(app->record.mutex);
    }

    if (app->record.ofmt_ctx &&
        !(app->record.ofmt_ctx->oformat->flags & AVFMT_NOFILE) &&
        app->record.ofmt_ctx->pb) {
        av_write_trailer(app->record.ofmt_ctx);
        avio_closep(&app->record.ofmt_ctx->pb);
    }

    if (app->record.ofmt_ctx) {
        avformat_free_context(app->record.ofmt_ctx);
        app->record.ofmt_ctx = NULL;
    }

    if (app->record.enc_ctx) {
        avcodec_free_context(&app->record.enc_ctx);
    }

    if (app->record.yuv_frame) {
        av_frame_free(&app->record.yuv_frame);
    }

    if (app->record.pkt) {
        av_packet_free(&app->record.pkt);
    }

    if (app->record.sws_ctx) {
        sws_freeContext(app->record.sws_ctx);
        app->record.sws_ctx = NULL;
    }

    if (app->record.mutex) {
        SDL_DestroyMutex(app->record.mutex);
        app->record.mutex = NULL;
    }

    app->record.enabled = 0;
}