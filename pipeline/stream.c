#include<stdio.h>
#include<string.h>
#include"stream.h"
#include"log.h"

static void stream_enter_fatal_error(AppState *app,const char *reason){
    if(!app){
        return;
    }

    SDL_LockMutex(app->stream.mutex);
    if(!app->stream.fatal_error){
        app->stream.fatal_error = 1;
        app->stream.accepting_frames = 0;
        LOG_ERROR("stream fatal error :%s\n",reason ? reason : "unknown");
    }
    SDL_UnlockMutex(app->stream.mutex);
    frame_queue_stop(&app->stream.queue);
}

static int64_t timestamp_us_to_pts(uint64_t delta_us,AVRational time_base){
    return av_rescale_q((int64_t)delta_us,(AVRational){1,1000000},time_base);
}

void stream_state_init(AppState *app,const char *url,int fps){
    memset(&app->stream,0,sizeof(app->stream));
    snprintf(app->stream.output_url,sizeof(app->stream.output_url),"%s",url);
    app->stream.fps = fps;
    app->stream.frame_index = 0;
    app->stream.base_timestamp_us = 0;
    app->stream.have_base_timestamp = 0;
    app->stream.last_input_pts = AV_NOPTS_VALUE;
    app->stream.frames_encoded = 0;
    app->stream.enabled = 0;
    app->stream.accepting_frames = 0;
    app->stream.fatal_error = 0;
}

static int stream_init_encoder(AppState *app){
    int ret;

    app->stream.encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    if(!app->stream.encoder){
        fprintf(stderr, "avcodec_find_encoder failed\n");
        return -1;
    }

    app->stream.enc_ctx = avcodec_alloc_context3(app->stream.encoder);
    if(!app->stream.enc_ctx){
        fprintf(stderr, "avcodec_alloc_context3 failed\n");
        return -1;
    }

    app->stream.enc_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
    app->stream.enc_ctx->width = app->width;
    app->stream.enc_ctx->height = app->height;
    app->stream.enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    app->stream.enc_ctx->time_base = (AVRational){1, app->stream.fps};
    app->stream.enc_ctx->framerate = (AVRational){app->stream.fps, 1};
    app->stream.enc_ctx->gop_size = app->stream.fps;
    app->stream.enc_ctx->max_b_frames = 0;
    app->stream.enc_ctx->bit_rate = 1000000;

    av_opt_set(app->stream.enc_ctx->priv_data,"preset","veryfast",0);
    av_opt_set(app->stream.enc_ctx->priv_data,"tune","zerolatency",0);

    ret = avcodec_open2(app->stream.enc_ctx,app->stream.encoder,NULL);
    if(ret < 0){
        fprintf(stderr, "avcodec_open2 failed\n");
        return -1;
    }

    return 0;
}

static int stream_init_buffers(AppState *app){
    int ret;

    app->stream.sws_ctx = sws_getContext(
        app->width,
        app->height,
        AV_PIX_FMT_YUYV422,
        app->width,
        app->height,
        AV_PIX_FMT_YUV420P,
        SWS_BILINEAR,
        NULL,NULL,NULL);
    if(!app->stream.sws_ctx){
        fprintf(stderr, "sws_getContext failed\n");
        return -1;
    }

    app->stream.yuv_frame = av_frame_alloc();
    if(!app->stream.yuv_frame){
        fprintf(stderr, "av_frame_alloc failed\n");
        return -1;
    }

    app->stream.yuv_frame->format = AV_PIX_FMT_YUV420P;
    app->stream.yuv_frame->width = app->width;
    app->stream.yuv_frame->height = app->height;

    ret = av_frame_get_buffer(app->stream.yuv_frame,32);
    if(ret < 0){
        fprintf(stderr, "av_frame_get_buffer failed\n");
        return -1;
    }

    app->stream.pkt = av_packet_alloc();
    if(!app->stream.pkt){
        fprintf(stderr, "av_packet_alloc failed\n");
        return -1;
    }

    app->stream.mutex = SDL_CreateMutex();
    if(!app->stream.mutex){
        fprintf(stderr, "SDL_CreateMutex failed\n");
        return -1;
    }

    return 0;
}

static int stream_init_output(AppState *app){
    int ret;
    AVDictionary *opts = NULL;

    ret = avformat_alloc_output_context2(
        &app->stream.ofmt_ctx,
        NULL,
        "rtsp",
        app->stream.output_url);
    if(ret < 0 || !app->stream.ofmt_ctx){
        fprintf(stderr, "avformat_alloc_output_context2 failed\n");
        return -1;
    }

    app->stream.video_st = avformat_new_stream(app->stream.ofmt_ctx,NULL);
    if(!app->stream.video_st){
        fprintf(stderr, "avformat_new_stream failed\n");
        return -1;
    }

    app->stream.video_st->time_base = app->stream.enc_ctx->time_base;

    ret = avcodec_parameters_from_context(app->stream.video_st->codecpar,app->stream.enc_ctx);

    if(ret < 0){
        fprintf(stderr, "avcodec_parameters_from_context failed\n");
        return -1;
    }

    // av_opt_set(app->stream.ofmt_ctx->priv_data,"rtsp_transport","tcp",0);

    // ret = avio_open2(
    //     &app->stream.ofmt_ctx->pb,
    //     app->stream.output_url,
    //     AVIO_FLAG_WRITE,
    //     NULL,NULL);
    // if(ret < 0){
    //     fprintf(stderr, "avio_open2 failed\n");
    //     return -1;
    // }

    av_dict_set(&opts, "rtsp_transport", "tcp", 0);
    //RTSP 推流不应该用 avio_open2 直接连接，应该用 avformat_write_header 配合 AVDictionary 设置传输参数。

    ret = avformat_write_header(app->stream.ofmt_ctx,&opts);
    if(ret < 0){
        fprintf(stderr, "avformat_write_header failed\n");
        return -1;
    }
    
    return 0;
}

static int stream_init_queue(AppState *app){
    size_t frame_bytes = app->sizeimage;

    if(frame_queue_init(&app->stream.queue,8,frame_bytes,app->width,app->height,(int)app->bytesperline,app->pixfmt) < 0){
        fprintf(stderr, "frame_queue_init (stream) failed\n");
        return -1;
    }

    return 0;
}

static int stream_write_one_packet(AppState *app){
    int ret;

    while(1){
        ret = avcodec_receive_packet(app->stream.enc_ctx,app->stream.pkt);
        if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
            return 0;
        }
        if(ret < 0){
            return -1;
        }

        av_packet_rescale_ts(app->stream.pkt,app->stream.enc_ctx->time_base,app->stream.video_st->time_base);
        app->stream.pkt->stream_index = app->stream.video_st->index;

        ret = av_interleaved_write_frame(app->stream.ofmt_ctx,app->stream.pkt);
        av_packet_unref(app->stream.pkt);

        if(ret < 0){
            return -1;
        }
    }
}

static void stream_flush_encoder(AppState *app){
    int ret;

    if(!app->stream.enc_ctx || !app->stream.ofmt_ctx){
        return;
    }

    ret = avcodec_send_frame(app->stream.enc_ctx,NULL);
    if(ret < 0){
        return;
    }

    (void)stream_write_one_packet(app);
}

static int stream_consume_packet(AppState *app,const FramePacket *pkt){
    const uint8_t *src_slices[1];
    int src_stride[1];
    int ret;
    uint64_t delta_us;
    int64_t pts;
    int64_t pts_raw;

    if(!app->stream.enabled || !pkt || !pkt->data){
        return 0;
    }

    SDL_LockMutex(app->stream.mutex);

    /*
        摄像头硬件时间戳（绝对微秒）
                ↓ 减去第1帧时间戳
        相对时间（微秒，从0开始）
                ↓ timestamp_us_to_pts
        编码器PTS（time_base单位）
    */
    if(!app->stream.have_base_timestamp){
        app->stream.base_timestamp_us = pkt->meta.timestamp_us;
        app->stream.have_base_timestamp = 1;
    }
    if(pkt->meta.timestamp_us < app->stream.base_timestamp_us){
        delta_us = 0;
    }else{
        delta_us = pkt->meta.timestamp_us - app->stream.base_timestamp_us;
    }
    pts_raw = timestamp_us_to_pts(delta_us,app->stream.enc_ctx->time_base);
    pts = pts_raw;

    if(app->stream.last_input_pts != AV_NOPTS_VALUE && pts_raw <= app->stream.last_input_pts){
        LOG_WARN("stream pts adjusted for monotonicity: raw=%lld last=%lld adjusted=%lld",
             (long long)pts,
             (long long)app->stream.last_input_pts,
             (long long)(app->stream.last_input_pts + 1));
        pts = app->stream.last_input_pts + 1;
    }
    app->stream.last_input_pts = pts;

    ret = av_frame_make_writable(app->stream.yuv_frame);
    if(ret < 0){
        SDL_UnlockMutex(app->stream.mutex);
        return -1;
    }

    if(pkt->pixfmt != V4L2_PIX_FMT_YUYV){
        SDL_UnlockMutex(app->stream.mutex);
        fprintf(stderr, "unsupported stream packet pixfmt:0x%x\n", pkt->pixfmt);
        return -1;
    }

    src_slices[0] = pkt->data;
    if(pkt->stride <= 0){
        fprintf(stderr, "invalid stream packet stride:%d\n", pkt->stride);
        return -1;
    }
    src_stride[0] = pkt->stride;

    sws_scale(
        app->stream.sws_ctx,
        src_slices,src_stride,
        0,
        app->height,
        app->stream.yuv_frame->data,
        app->stream.yuv_frame->linesize);

    app->stream.yuv_frame->pts = pts;
    app->stream.frame_index++;

    ret = avcodec_send_frame(app->stream.enc_ctx,app->stream.yuv_frame);
    if(ret < 0){
        SDL_UnlockMutex(app->stream.mutex);
        return -1;
    }

    ret = stream_write_one_packet(app);
    if(ret < 0){
        SDL_UnlockMutex(app->stream.mutex);
        return -1;
    }

    app->stream.frames_encoded++;
    SDL_UnlockMutex(app->stream.mutex);
    return 0;
}

static int stream_thread_main(void *userdata){
    AppState *app = (AppState *)userdata;
    FramePacket pkt;
    FrameQueuePopResult pop_ret;
    size_t frame_bytes = app->sizeimage;

    if(frame_packet_init(&pkt,frame_bytes,app->width,app->height,(int)app->bytesperline,app->pixfmt) < 0){
        fprintf(stderr, "frame_packet_init (stream) failed\n");
        return -1;
    }

    while (1)
    {
        pop_ret = frame_queue_pop(&app->stream.queue,&pkt,200);
        if(pop_ret == FRAME_QUEUE_POP_TIMEOUT){
            continue;
        }
        if(pop_ret == FRAME_QUEUE_POP_STOPPED){
            break;
        }
        if(pop_ret == FRAME_QUEUE_POP_ERROR){
            fprintf(stderr, "frame_queue_pop (stream) failed\n");
            break;
        }

        if(stream_consume_packet(app,&pkt) < 0){
            stream_enter_fatal_error(app, "stream_consume_packet failed\n");
            break;
        }
    }

    frame_packet_free(&pkt);
    return 0;
}

int stream_init(AppState *app){
    if(!app){
        return -1;
    }
    if(stream_init_encoder(app) < 0){
        goto fail;
    }
    if(stream_init_buffers(app) < 0){
        goto fail;
    }
    if(stream_init_queue(app) < 0){
        goto fail;
    }
    if(stream_init_output(app) < 0){
        goto fail;
    }

    app->stream.thread = SDL_CreateThread(stream_thread_main,"stream_thread",app);
    if(!app->stream.thread){
        fprintf(stderr, "SDL_CreateThread (stream) failed:%s\n",SDL_GetError());
        goto fail;
    }

    app->stream.fatal_error = 0;
    app->stream.accepting_frames = 1;
    app->stream.enabled = 1;

    return 0;
fail:    
    stream_close(app);
    return -1;
}

void stream_close(AppState *app)
{
    if(!app){
        return;
    }

    app->stream.accepting_frames = 0;

    if(app->stream.thread){
        frame_queue_stop(&app->stream.queue);
        SDL_WaitThread(app->stream.thread,NULL);
        app->stream.thread = NULL;
    }
    if (app->stream.enabled) {
        SDL_LockMutex(app->stream.mutex);
        stream_flush_encoder(app);
        SDL_UnlockMutex(app->stream.mutex);
    }

    if (app->stream.ofmt_ctx &&
        !(app->stream.ofmt_ctx->oformat->flags & AVFMT_NOFILE) &&
        app->stream.ofmt_ctx->pb) {
        av_write_trailer(app->stream.ofmt_ctx);
        avio_closep(&app->stream.ofmt_ctx->pb);
    }

    if (app->stream.ofmt_ctx) {
        avformat_free_context(app->stream.ofmt_ctx);
        app->stream.ofmt_ctx = NULL;
    }

    if (app->stream.enc_ctx) {
        avcodec_free_context(&app->stream.enc_ctx);
    }

    if (app->stream.yuv_frame) {
        av_frame_free(&app->stream.yuv_frame);
    }

    if (app->stream.pkt) {
        av_packet_free(&app->stream.pkt);
    }

    if (app->stream.sws_ctx) {
        sws_freeContext(app->stream.sws_ctx);
        app->stream.sws_ctx = NULL;
    }

    frame_queue_destroy(&app->stream.queue);

    if (app->stream.mutex) {
        SDL_DestroyMutex(app->stream.mutex);
        app->stream.mutex = NULL;
    }

    app->stream.enabled = 0;
    app->stream.accepting_frames = 0;
    app->stream.fatal_error = 0;
    app->stream.base_timestamp_us = 0;
    app->stream.have_base_timestamp = 0;
    app->stream.frame_index = 0;
    app->stream.last_input_pts = AV_NOPTS_VALUE;
    app->stream.frames_encoded = 0;
}