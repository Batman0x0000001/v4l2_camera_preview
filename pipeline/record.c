#include<stdio.h>
#include<string.h>
#include"record.h"
#include"log.h"
#include"path_utils.h"

static void record_reset_timeline_state(AppState *app){
    app->record.frame_index = 0;
    app->record.last_input_pts = AV_NOPTS_VALUE;

    app->record.media_base_timestamp_us = 0;
    app->record.have_media_base_timestamp = 0;

    app->record.audio_anchor_capture_time_us = 0;
    app->record.audio_anchor_first_frame_index = 0;
    app->record.have_audio_anchor = 0;

    app->record.audio_next_pts = AV_NOPTS_VALUE;

    app->record.frames_encoded = 0;
    app->record.audio_frames_encoded = 0;
    app->record.audio_chunks_consumed = 0;

    app->record.session_start_media_us = 0;
    app->record.session_last_media_us = 0;
}

static void record_enter_fatal_error(AppState *app,const char *reason){
    if(!app){
        return;
    }

    SDL_LockMutex(app->record.mutex);
    if(!app->record.fatal_error){
        app->record.fatal_error = 1;
        app->record.accepting_frames = 0;
        LOG_ERROR("record fatal error: %s", reason ? reason : "unknown");
    }
    SDL_UnlockMutex(app->record.mutex);

    frame_queue_stop(&app->record.queue);
    audio_queue_stop(&app->record.audio_queue);
}

static int64_t timestamp_us_to_pts(uint64_t delta_us,AVRational time_base){
    return av_rescale_q((int64_t)delta_us,(AVRational){1,1000000},time_base);
}

static enum AVSampleFormat pick_audio_sample_fmt(const AVCodec *codec){
    const enum AVSampleFormat *p;

    if(!codec || !codec->sample_fmts){
        return AV_SAMPLE_FMT_FLTP;
    }

    for(p = codec->sample_fmts; *p != AV_SAMPLE_FMT_NONE; ++p){
        if(*p == AV_SAMPLE_FMT_FLTP){
            return *p;
        }
    }

    return codec->sample_fmts[0];
}

static int pick_audio_sample_rate(const AVCodec *codec,int requested_rate){
    const int *p;
    int best_rate;
    int best_diff;

    if(!codec || !codec->supported_samplerates || requested_rate <= 0){
        return requested_rate;
    }

    best_rate = codec->supported_samplerates[0];
    best_diff = abs(best_rate - requested_rate);

    for(p = codec->supported_samplerates; *p != 0; ++p){
        int diff = abs(*p - requested_rate);

        if(*p == requested_rate){
            return *p;
        }
        if(diff < best_diff){
            best_rate = *p;
            best_diff = diff;
        }
    }

    return best_rate;
}

static int pick_audio_ch_layout(const AVCodec *codec,int requested_channels,AVChannelLayout *out){
    const AVChannelLayout *p;
    AVChannelLayout requested = {0};
    int ret;

    if(!out || requested_channels <= 0){
        return -1;
    }

    av_channel_layout_default(&requested, requested_channels);

    if(!codec || !codec->ch_layouts){
        *out = requested;
        return 0;
    }

    for(p = codec->ch_layouts; p->nb_channels != 0; ++p){
        if(p->nb_channels == requested_channels){
            ret = av_channel_layout_copy(out, p);
            av_channel_layout_uninit(&requested);
            return ret;
        }
    }

    ret = av_channel_layout_copy(out, &codec->ch_layouts[0]);
    av_channel_layout_uninit(&requested);
    return ret;
}

static void record_set_media_base_if_needed(AppState *app,uint64_t capture_time_us){
    if(!app->record.have_media_base_timestamp){
        app->record.media_base_timestamp_us = capture_time_us;
        app->record.have_media_base_timestamp = 1;
        app->record.session_start_media_us = capture_time_us;
    }
    app->record.session_last_media_us = capture_time_us;
}

void record_state_init(AppState *app,const char *output_dir,int fps){
    memset(&app->record,0,sizeof(app->record));

    snprintf(app->record.output_dir,
             sizeof(app->record.output_dir),
             "%s",
             output_dir ? output_dir : "recordings");

    app->record.fps = fps;
    app->record.enabled = 0;
    app->record.accepting_frames = 0;
    app->record.fatal_error = 0;
    app->record.session_active = 0;
    app->record.stopping_session = 0;
    app->record.session_count = 0;

    record_reset_timeline_state(app);
}

static int record_init_common_objects(AppState *app){
    app->record.mutex = SDL_CreateMutex();
    if(!app->record.mutex){
        fprintf(stderr, "SDL_CreateMutex (record) failed\n");
        return -1;
    }
    return 0;
}

static int record_init_queue(AppState *app){
    size_t frame_bytes = app->sizeimage;

    if(frame_queue_init(&app->record.queue, 8, frame_bytes,
                        app->width, app->height,
                        (int)app->bytesperline, app->pixfmt) < 0){
        fprintf(stderr, "frame_queue_init (record) failed\n");
        return -1;
    }

    return 0;
}

static int record_init_video_encoder(AppState *app){
    int ret;

    app->record.encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    if(!app->record.encoder){
        fprintf(stderr, "avcodec_find_encoder (record video) failed\n");
        return -1;
    }

    app->record.enc_ctx = avcodec_alloc_context3(app->record.encoder);
    if(!app->record.enc_ctx){
        fprintf(stderr, "avcodec_alloc_context3 (record video) failed\n");
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

    av_opt_set(app->record.enc_ctx->priv_data,"preset","veryfast",0);

    ret = avcodec_open2(app->record.enc_ctx,app->record.encoder,NULL);
    if(ret < 0){
        fprintf(stderr, "avcodec_open2 (record video) failed\n");
        return -1;
    }

    return 0;
}

static int record_init_audio_encoder(AppState *app){
    int ret;
    AVChannelLayout enc_layout = {0};

    app->record.audio_encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if(!app->record.audio_encoder){
        fprintf(stderr, "avcodec_find_encoder (record audio) failed\n");
        return -1;
    }

    app->record.audio_enc_ctx = avcodec_alloc_context3(app->record.audio_encoder);
    if(!app->record.audio_enc_ctx){
        fprintf(stderr, "avcodec_alloc_context3 (record audio) failed\n");
        return -1;
    }

    app->record.audio_enc_ctx->codec_type = AVMEDIA_TYPE_AUDIO;
    app->record.audio_enc_ctx->sample_fmt =
        pick_audio_sample_fmt(app->record.audio_encoder);
    app->record.audio_enc_ctx->sample_rate =
        pick_audio_sample_rate(app->record.audio_encoder, (int)app->audio.sample_rate);
    app->record.audio_enc_ctx->time_base =
        (AVRational){1, app->record.audio_enc_ctx->sample_rate};
    app->record.audio_enc_ctx->bit_rate = 128000;
    app->record.audio_enc_ctx->profile = FF_PROFILE_AAC_LOW;

    if(pick_audio_ch_layout(app->record.audio_encoder,
                            app->audio.channels,
                            &enc_layout) < 0){
        fprintf(stderr, "pick_audio_ch_layout (record audio) failed\n");
        return -1;
    }

    ret = av_channel_layout_copy(&app->record.audio_enc_ctx->ch_layout, &enc_layout);
    av_channel_layout_uninit(&enc_layout);
    if(ret < 0){
        fprintf(stderr, "av_channel_layout_copy (record audio) failed\n");
        return -1;
    }

    ret = avcodec_open2(app->record.audio_enc_ctx, app->record.audio_encoder, NULL);
    if(ret < 0){
        fprintf(stderr, "avcodec_open2 (record audio) failed\n");
        return -1;
    }

    return 0;
}

static int record_init_video_buffers(AppState *app){
    int ret;

    app->record.sws_ctx = sws_getContext(
        app->width,
        app->height,
        AV_PIX_FMT_YUYV422,
        app->width,
        app->height,
        AV_PIX_FMT_YUV420P,
        SWS_BILINEAR,
        NULL,NULL,NULL);
    if(!app->record.sws_ctx){
        fprintf(stderr, "sws_getContext (record video) failed\n");
        return -1;
    }

    app->record.yuv_frame = av_frame_alloc();
    if(!app->record.yuv_frame){
        fprintf(stderr, "av_frame_alloc (record video) failed\n");
        return -1;
    }

    app->record.yuv_frame->format = AV_PIX_FMT_YUV420P;
    app->record.yuv_frame->width = app->width;
    app->record.yuv_frame->height = app->height;

    ret = av_frame_get_buffer(app->record.yuv_frame,32);
    if(ret < 0){
        fprintf(stderr, "av_frame_get_buffer (record video) failed\n");
        return -1;
    }

    app->record.pkt = av_packet_alloc();
    if(!app->record.pkt){
        fprintf(stderr, "av_packet_alloc (record video) failed\n");
        return -1;
    }

    return 0;
}

static int record_init_audio_buffers(AppState *app){
    int ret;
    AVChannelLayout in_layout = {0};

    av_channel_layout_default(&in_layout, app->audio.channels);

    ret = swr_alloc_set_opts2(&app->record.swr_ctx,
                              &app->record.audio_enc_ctx->ch_layout,
                              app->record.audio_enc_ctx->sample_fmt,
                              app->record.audio_enc_ctx->sample_rate,
                              &in_layout,
                              AV_SAMPLE_FMT_S16,
                              (int)app->audio.sample_rate,
                              0,
                              NULL);
    av_channel_layout_uninit(&in_layout);
    if(ret < 0 || !app->record.swr_ctx){
        fprintf(stderr, "swr_alloc_set_opts2 (record audio) failed\n");
        return -1;
    }

    ret = swr_init(app->record.swr_ctx);
    if(ret < 0){
        fprintf(stderr, "swr_init (record audio) failed\n");
        return -1;
    }

    app->record.audio_frame = av_frame_alloc();
    if(!app->record.audio_frame){
        fprintf(stderr, "av_frame_alloc (record audio) failed\n");
        return -1;
    }

    app->record.audio_frame->format = app->record.audio_enc_ctx->sample_fmt;
    app->record.audio_frame->sample_rate = app->record.audio_enc_ctx->sample_rate;

    ret = av_channel_layout_copy(&app->record.audio_frame->ch_layout,
                                 &app->record.audio_enc_ctx->ch_layout);
    if(ret < 0){
        fprintf(stderr, "av_channel_layout_copy (record audio frame) failed\n");
        return -1;
    }

    app->record.audio_frame->nb_samples = app->record.audio_enc_ctx->frame_size;

    ret = av_frame_get_buffer(app->record.audio_frame,0);
    if(ret < 0){
        fprintf(stderr, "av_frame_get_buffer (record audio) failed\n");
        return -1;
    }

    app->record.audio_pkt = av_packet_alloc();
    if(!app->record.audio_pkt){
        fprintf(stderr, "av_packet_alloc (record audio) failed\n");
        return -1;
    }

    app->record.audio_fifo = av_audio_fifo_alloc(app->record.audio_enc_ctx->sample_fmt,
                                                 app->record.audio_enc_ctx->ch_layout.nb_channels,
                                                 app->record.audio_enc_ctx->frame_size * 8);
    if(!app->record.audio_fifo){
        fprintf(stderr, "av_audio_fifo_alloc (record audio) failed\n");
        return -1;
    }

    return 0;
}

static int record_init_output(AppState *app){
    int ret;

    ret = avformat_alloc_output_context2(&app->record.ofmt_ctx,
                                         NULL,
                                         "mp4",
                                         app->record.active_output_path);
    if(ret < 0 || !app->record.ofmt_ctx){
        fprintf(stderr, "avformat_alloc_output_context2 (record) failed\n");
        return -1;
    }

    app->record.video_st = avformat_new_stream(app->record.ofmt_ctx,NULL);
    if(!app->record.video_st){
        fprintf(stderr, "avformat_new_stream (record video) failed\n");
        return -1;
    }
    app->record.video_st->time_base = app->record.enc_ctx->time_base;

    ret = avcodec_parameters_from_context(app->record.video_st->codecpar,
                                          app->record.enc_ctx);
    if(ret < 0){
        fprintf(stderr, "avcodec_parameters_from_context (record video) failed\n");
        return -1;
    }

    app->record.audio_st = avformat_new_stream(app->record.ofmt_ctx,NULL);
    if(!app->record.audio_st){
        fprintf(stderr, "avformat_new_stream (record audio) failed\n");
        return -1;
    }
    app->record.audio_st->time_base = app->record.audio_enc_ctx->time_base;

    ret = avcodec_parameters_from_context(app->record.audio_st->codecpar,
                                          app->record.audio_enc_ctx);
    if(ret < 0){
        fprintf(stderr, "avcodec_parameters_from_context (record audio) failed\n");
        return -1;
    }

    if(!(app->record.ofmt_ctx->oformat->flags & AVFMT_NOFILE)){
        ret = avio_open2(&app->record.ofmt_ctx->pb,
                         app->record.active_output_path,
                         AVIO_FLAG_WRITE,
                         NULL,
                         NULL);
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

static void record_close_session_resources(AppState *app){
    if(!app){
        return;
    }

    if(app->record.ofmt_ctx &&
       !(app->record.ofmt_ctx->oformat->flags & AVFMT_NOFILE) &&
       app->record.ofmt_ctx->pb){
        av_write_trailer(app->record.ofmt_ctx);
        avio_closep(&app->record.ofmt_ctx->pb);
    }

    if(app->record.ofmt_ctx){
        avformat_free_context(app->record.ofmt_ctx);
        app->record.ofmt_ctx = NULL;
    }

    if(app->record.enc_ctx){
        avcodec_free_context(&app->record.enc_ctx);
    }

    if(app->record.audio_enc_ctx){
        avcodec_free_context(&app->record.audio_enc_ctx);
    }

    if(app->record.yuv_frame){
        av_frame_free(&app->record.yuv_frame);
    }

    if(app->record.audio_frame){
        av_frame_free(&app->record.audio_frame);
    }

    if(app->record.pkt){
        av_packet_free(&app->record.pkt);
    }

    if(app->record.audio_pkt){
        av_packet_free(&app->record.audio_pkt);
    }

    if(app->record.sws_ctx){
        sws_freeContext(app->record.sws_ctx);
        app->record.sws_ctx = NULL;
    }

    if(app->record.swr_ctx){
        swr_free(&app->record.swr_ctx);
    }

    if(app->record.audio_fifo){
        av_audio_fifo_free(app->record.audio_fifo);
        app->record.audio_fifo = NULL;
    }

    app->record.video_st = NULL;
    app->record.audio_st = NULL;
}

static int record_open_session_resources(AppState *app){
    if(record_init_video_encoder(app) < 0){
        return -1;
    }
    if(record_init_audio_encoder(app) < 0){
        return -1;
    }
    if(record_init_video_buffers(app) < 0){
        return -1;
    }
    if(record_init_audio_buffers(app) < 0){
        return -1;
    }
    if(record_init_output(app) < 0){
        return -1;
    }
    return 0;
}

static int record_write_encoded_packets(AppState *app,
                                        AVCodecContext *enc_ctx,
                                        AVPacket *pkt,
                                        AVStream *st,
                                        const char *tag){
    int ret;

    while(1){
        ret = avcodec_receive_packet(enc_ctx,pkt);
        if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
            return 0;
        }
        if(ret < 0){
            LOG_ERROR("avcodec_receive_packet (%s) failed", tag ? tag : "unknown");
            return -1;
        }

        av_packet_rescale_ts(pkt, enc_ctx->time_base, st->time_base);
        pkt->stream_index = st->index;

        if(pkt->pts == AV_NOPTS_VALUE || pkt->dts == AV_NOPTS_VALUE){
            LOG_WARN("record packet has invalid timestamps (%s): pts=%lld dts=%lld",
                     tag ? tag : "unknown",
                     (long long)pkt->pts,
                     (long long)pkt->dts);
            av_packet_unref(pkt);
            continue;
        }

        ret = av_interleaved_write_frame(app->record.ofmt_ctx,pkt);
        av_packet_unref(pkt);
        if(ret < 0){
            LOG_ERROR("av_interleaved_write_frame (%s) failed", tag ? tag : "unknown");
            return -1;
        }
    }
}

static int record_feed_audio_fifo(AppState *app,const AudioPacket *pkt){
    const uint8_t *in_data[1];
    uint8_t **converted_data = NULL;
    int dst_nb_samples;
    int converted_samples;
    int fifo_target;
    int ret;

    if(!pkt || pkt->meta.frames == 0){
        return 0;
    }

    dst_nb_samples = av_rescale_rnd(
        swr_get_delay(app->record.swr_ctx, (int)app->audio.sample_rate) +
        (int64_t)pkt->meta.frames,
        app->record.audio_enc_ctx->sample_rate,
        (int)app->audio.sample_rate,
        AV_ROUND_UP);

    if(dst_nb_samples <= 0){
        return -1;
    }

    ret = av_samples_alloc_array_and_samples(&converted_data,
                                             NULL,
                                             app->record.audio_enc_ctx->ch_layout.nb_channels,
                                             dst_nb_samples,
                                             app->record.audio_enc_ctx->sample_fmt,
                                             0);
    if(ret < 0){
        return -1;
    }

    in_data[0] = pkt->data;

    converted_samples = swr_convert(app->record.swr_ctx,
                                    converted_data,
                                    dst_nb_samples,
                                    in_data,
                                    (int)pkt->meta.frames);
    if(converted_samples < 0){
        av_freep(&converted_data[0]);
        av_freep(&converted_data);
        return -1;
    }

    fifo_target = av_audio_fifo_size(app->record.audio_fifo) + converted_samples;
    ret = av_audio_fifo_realloc(app->record.audio_fifo, fifo_target);
    if(ret < 0){
        av_freep(&converted_data[0]);
        av_freep(&converted_data);
        return -1;
    }

    ret = av_audio_fifo_write(app->record.audio_fifo,
                              (void **)converted_data,
                              converted_samples);

    av_freep(&converted_data[0]);
    av_freep(&converted_data);

    if(ret < converted_samples){
        return -1;
    }

    return 0;
}

static int record_encode_audio_from_fifo(AppState *app){
    int ret;
    int frame_size = app->record.audio_enc_ctx->frame_size;

    while(av_audio_fifo_size(app->record.audio_fifo) >= frame_size){
        ret = av_frame_make_writable(app->record.audio_frame);
        if(ret < 0){
            return -1;
        }

        ret = av_audio_fifo_read(app->record.audio_fifo,
                                 (void **)app->record.audio_frame->data,
                                 frame_size);
        if(ret < frame_size){
            return -1;
        }

        app->record.audio_frame->pts = app->record.audio_next_pts;
        app->record.audio_next_pts += frame_size;

        ret = avcodec_send_frame(app->record.audio_enc_ctx, app->record.audio_frame);
        if(ret < 0){
            return -1;
        }

        ret = record_write_encoded_packets(app,
                                           app->record.audio_enc_ctx,
                                           app->record.audio_pkt,
                                           app->record.audio_st,
                                           "record audio");
        if(ret < 0){
            return -1;
        }

        app->record.audio_frames_encoded += (uint64_t)frame_size;
    }

    return 0;
}

static int64_t record_compute_audio_chunk_start_pts(AppState *app,const AudioPacket *pkt){
    uint64_t delta_us;
    int64_t anchor_pts;
    int64_t frame_offset;

    record_set_media_base_if_needed(app, pkt->meta.capture_time_us);

    if(!app->record.have_audio_anchor){
        app->record.audio_anchor_capture_time_us = pkt->meta.capture_time_us;
        app->record.audio_anchor_first_frame_index = pkt->meta.first_frame_index;
        app->record.have_audio_anchor = 1;
    }

    if(app->record.audio_anchor_capture_time_us < app->record.media_base_timestamp_us){
        delta_us = 0;
    }else{
        delta_us = app->record.audio_anchor_capture_time_us -
                   app->record.media_base_timestamp_us;
    }

    anchor_pts = timestamp_us_to_pts(delta_us, app->record.audio_enc_ctx->time_base);
    frame_offset = (int64_t)(pkt->meta.first_frame_index -
                             app->record.audio_anchor_first_frame_index);

    return anchor_pts + frame_offset;
}

static int record_consume_audio_packet(AppState *app,const AudioPacket *pkt){
    int ret;
    int64_t chunk_start_pts;
    int64_t expected_pts;

    if(!app->record.session_active || !pkt || !pkt->data){
        return 0;
    }

    SDL_LockMutex(app->record.mutex);

    chunk_start_pts = record_compute_audio_chunk_start_pts(app, pkt);

    if(app->record.audio_next_pts == AV_NOPTS_VALUE){
        app->record.audio_next_pts = chunk_start_pts;
    }else{
        expected_pts = app->record.audio_next_pts +
                       av_audio_fifo_size(app->record.audio_fifo);

        if(llabs(chunk_start_pts - expected_pts) >
           app->record.audio_enc_ctx->frame_size){
            LOG_WARN("record audio start drift: chunk_start=%lld expected=%lld diff=%lld",
                     (long long)chunk_start_pts,
                     (long long)expected_pts,
                     (long long)(chunk_start_pts - expected_pts));
        }
    }

    ret = record_feed_audio_fifo(app, pkt);
    if(ret < 0){
        SDL_UnlockMutex(app->record.mutex);
        return -1;
    }

    ret = record_encode_audio_from_fifo(app);
    if(ret < 0){
        SDL_UnlockMutex(app->record.mutex);
        return -1;
    }

    app->record.audio_chunks_consumed++;
    SDL_UnlockMutex(app->record.mutex);
    return 0;
}

static int record_consume_video_packet(AppState *app,const FramePacket *pkt){
    const uint8_t *src_slices[1];
    int src_stride[1];
    int ret;
    uint64_t delta_us;
    int64_t pts;
    int64_t pts_raw;

    if(!app->record.session_active || !pkt || !pkt->data){
        return 0;
    }

    SDL_LockMutex(app->record.mutex);

    record_set_media_base_if_needed(app, pkt->meta.capture_time_us);

    if(pkt->meta.capture_time_us < app->record.media_base_timestamp_us){
        delta_us = 0;
    }else{
        delta_us = pkt->meta.capture_time_us - app->record.media_base_timestamp_us;
    }

    pts_raw = timestamp_us_to_pts(delta_us, app->record.enc_ctx->time_base);
    pts = pts_raw;

    if(app->record.last_input_pts != AV_NOPTS_VALUE && pts_raw <= app->record.last_input_pts){
        LOG_WARN("record video pts adjusted for monotonicity: raw=%lld last=%lld adjusted=%lld",
                 (long long)pts_raw,
                 (long long)app->record.last_input_pts,
                 (long long)(app->record.last_input_pts + 1));
        pts = app->record.last_input_pts + 1;
    }
    app->record.last_input_pts = pts;

    ret = av_frame_make_writable(app->record.yuv_frame);
    if(ret < 0){
        SDL_UnlockMutex(app->record.mutex);
        return -1;
    }

    if(pkt->pixfmt != V4L2_PIX_FMT_YUYV){
        SDL_UnlockMutex(app->record.mutex);
        fprintf(stderr, "unsupported record packet pixfmt:0x%x\n", pkt->pixfmt);
        return -1;
    }

    src_slices[0] = pkt->data;
    if(pkt->stride <= 0){
        SDL_UnlockMutex(app->record.mutex);
        fprintf(stderr, "invalid record packet stride:%d\n", pkt->stride);
        return -1;
    }
    src_stride[0] = pkt->stride;

    sws_scale(app->record.sws_ctx,
              src_slices,
              src_stride,
              0,
              pkt->height,
              app->record.yuv_frame->data,
              app->record.yuv_frame->linesize);

    if(pts == AV_NOPTS_VALUE || pts < 0){
        SDL_UnlockMutex(app->record.mutex);
        return -1;
    }

    app->record.yuv_frame->pts = pts;
    app->record.frame_index++;

    ret = avcodec_send_frame(app->record.enc_ctx, app->record.yuv_frame);
    if(ret < 0){
        SDL_UnlockMutex(app->record.mutex);
        return -1;
    }

    ret = record_write_encoded_packets(app,
                                       app->record.enc_ctx,
                                       app->record.pkt,
                                       app->record.video_st,
                                       "record video");
    if(ret < 0){
        SDL_UnlockMutex(app->record.mutex);
        return -1;
    }

    app->record.frames_encoded++;
    SDL_UnlockMutex(app->record.mutex);
    return 0;
}

static int record_flush_video_encoder(AppState *app){
    int ret;

    if(!app->record.enc_ctx || !app->record.ofmt_ctx){
        return 0;
    }

    ret = avcodec_send_frame(app->record.enc_ctx,NULL);
    if(ret < 0){
        return -1;
    }

    return record_write_encoded_packets(app,
                                        app->record.enc_ctx,
                                        app->record.pkt,
                                        app->record.video_st,
                                        "record video flush");
}

static int record_flush_audio_encoder(AppState *app){
    int ret;
    int frame_size;
    int remaining;
    int to_copy;

    if(!app->record.audio_enc_ctx || !app->record.ofmt_ctx){
        return 0;
    }

    frame_size = app->record.audio_enc_ctx->frame_size;

    while(app->record.audio_fifo && av_audio_fifo_size(app->record.audio_fifo) > 0){
        remaining = av_audio_fifo_size(app->record.audio_fifo);
        to_copy = remaining < frame_size ? remaining : frame_size;

        ret = av_frame_make_writable(app->record.audio_frame);
        if(ret < 0){
            return -1;
        }

        ret = av_samples_set_silence(app->record.audio_frame->data,
                                     0,
                                     frame_size,
                                     app->record.audio_enc_ctx->ch_layout.nb_channels,
                                     app->record.audio_enc_ctx->sample_fmt);
        if(ret < 0){
            return -1;
        }

        ret = av_audio_fifo_read(app->record.audio_fifo,
                                 (void **)app->record.audio_frame->data,
                                 to_copy);
        if(ret < to_copy){
            return -1;
        }

        app->record.audio_frame->pts = app->record.audio_next_pts;
        app->record.audio_next_pts += frame_size;

        ret = avcodec_send_frame(app->record.audio_enc_ctx, app->record.audio_frame);
        if(ret < 0){
            return -1;
        }

        ret = record_write_encoded_packets(app,
                                           app->record.audio_enc_ctx,
                                           app->record.audio_pkt,
                                           app->record.audio_st,
                                           "record audio drain");
        if(ret < 0){
            return -1;
        }
    }

    ret = avcodec_send_frame(app->record.audio_enc_ctx, NULL);
    if(ret < 0){
        return -1;
    }

    return record_write_encoded_packets(app,
                                        app->record.audio_enc_ctx,
                                        app->record.audio_pkt,
                                        app->record.audio_st,
                                        "record audio flush");
}

static void record_drop_audio_chunk(AppState *app,const AudioPacket *pkt){
    static uint64_t last_chunk_id = 0;

    if(!pkt){
        return;
    }

    if(last_chunk_id != 0 && pkt->chunk_id != last_chunk_id + 1){
        LOG_WARN("record audio chunk gap: last=%llu current=%llu lost=%llu",
                 (unsigned long long)last_chunk_id,
                 (unsigned long long)pkt->chunk_id,
                 (unsigned long long)(pkt->chunk_id - last_chunk_id - 1));
    }
    last_chunk_id = pkt->chunk_id;
    (void)app;
}

static int record_audio_queue_size(AudioQueue *q){
    int size = 0;

    if(!q || !q->mutex){
        return 0;
    }

    SDL_LockMutex(q->mutex);
    size = q->size;
    SDL_UnlockMutex(q->mutex);

    return size;
}

static int record_thread_main(void *userdata){
    AppState *app = (AppState *)userdata;
    FramePacket video_pkt;
    AudioPacket audio_pkt;
    int video_queue_stopped = 0;
    int audio_queue_stopped = 0;

    if(frame_packet_init(&video_pkt,
                         app->sizeimage,
                         app->width,
                         app->height,
                         (int)app->bytesperline,
                         app->pixfmt) < 0){
        fprintf(stderr, "frame_packet_init (record worker) failed\n");
        return -1;
    }

    if(audio_packet_init(&audio_pkt, app->audio.period_buffer_bytes) < 0){
        frame_packet_free(&video_pkt);
        fprintf(stderr, "audio_packet_init (record worker) failed\n");
        return -1;
    }

    while(1){
        int did_work = 0;
        int audio_backlog = 0;
        int audio_burst = 1;

        if(!audio_queue_stopped){
            audio_backlog = record_audio_queue_size(&app->record.audio_queue);

            if(audio_backlog >= 128){
                audio_burst = 16;
            }else if(audio_backlog >= 64){
                audio_burst = 8;
            }else if(audio_backlog >= 16){
                audio_burst = 4;
            }else{
                audio_burst = 1;
            }

            for(int i = 0; i < audio_burst; ++i){
                AudioQueuePopResult audio_ret;

                audio_ret = audio_queue_pop(&app->record.audio_queue,
                                            &audio_pkt,
                                            0);
                if(audio_ret == AUDIO_QUEUE_POP_OK){
                    did_work = 1;

                    if(!app->record.session_active){
                        record_drop_audio_chunk(app, &audio_pkt);
                        continue;
                    }

                    if(record_consume_audio_packet(app, &audio_pkt) < 0){
                        record_enter_fatal_error(app, "record_consume_audio_packet failed");
                        goto out;
                    }
                }else if(audio_ret == AUDIO_QUEUE_POP_STOPPED){
                    audio_queue_stopped = 1;
                    break;
                }else if(audio_ret == AUDIO_QUEUE_POP_TIMEOUT){
                    break;
                }else{
                    fprintf(stderr, "audio_queue_pop (record audio) failed\n");
                    goto out;
                }
            }
        }

        if(!video_queue_stopped){
            FrameQueuePopResult video_ret;

            video_ret = frame_queue_pop(&app->record.queue, &video_pkt, 5);
            if(video_ret == FRAME_QUEUE_POP_OK){
                did_work = 1;

                if(!app->record.session_active){
                    continue;
                }

                if(record_consume_video_packet(app, &video_pkt) < 0){
                    record_enter_fatal_error(app, "record_consume_video_packet failed");
                    goto out;
                }
            }else if(video_ret == FRAME_QUEUE_POP_STOPPED){
                video_queue_stopped = 1;
            }else if(video_ret == FRAME_QUEUE_POP_ERROR){
                fprintf(stderr, "frame_queue_pop (record video) failed\n");
                goto out;
            }
        }

        if(video_queue_stopped && audio_queue_stopped){
            break;
        }

        if(!did_work){
            SDL_Delay(1);
        }
    }

out:
    audio_packet_free(&audio_pkt);
    frame_packet_free(&video_pkt);
    return 0;
}

int record_session_start(AppState *app){
    int ret;

    if(!app || !app->record.enabled){
        return -1;
    }

    SDL_LockMutex(app->record.mutex);

    if(app->record.session_active || app->record.stopping_session){
        SDL_UnlockMutex(app->record.mutex);
        return 0;
    }

    if(ensure_dir_exists(app->record.output_dir) < 0){
        SDL_UnlockMutex(app->record.mutex);
        LOG_ERROR("ensure_dir_exists(record_dir) failed");
        return -1;
    }

    if(make_record_filename(app->record.active_output_path,
                            sizeof(app->record.active_output_path),
                            app->record.output_dir) < 0){
        SDL_UnlockMutex(app->record.mutex);
        LOG_ERROR("make_record_filename failed");
        return -1;
    }

    record_reset_timeline_state(app);
    app->record.accepting_frames = 0;
    app->record.stopping_session = 0;

    SDL_UnlockMutex(app->record.mutex);

    ret = record_open_session_resources(app);
    if(ret < 0){
        SDL_LockMutex(app->record.mutex);
        record_close_session_resources(app);
        app->record.session_active = 0;
        app->record.accepting_frames = 0;
        app->record.stopping_session = 0;
        SDL_UnlockMutex(app->record.mutex);
        return -1;
    }

    frame_queue_flush(&app->record.queue);
    audio_queue_flush(&app->record.audio_queue);

    SDL_LockMutex(app->record.mutex);
    app->record.session_active = 1;
    app->record.accepting_frames = 1;
    app->record.stopping_session = 0;
    app->record.session_count++;
    SDL_UnlockMutex(app->record.mutex);

    LOG_INFO("record session started: %s", app->record.active_output_path);
    return 0;
}

int record_session_stop(AppState *app){
    if(!app || !app->record.enabled){
        return -1;
    }

    SDL_LockMutex(app->record.mutex);

    if(!app->record.session_active || app->record.stopping_session){
        SDL_UnlockMutex(app->record.mutex);
        return 0;
    }

    app->record.stopping_session = 1;
    app->record.accepting_frames = 0;

    SDL_UnlockMutex(app->record.mutex);

    frame_queue_flush(&app->record.queue);
    audio_queue_flush(&app->record.audio_queue);

    SDL_LockMutex(app->record.mutex);

    (void)record_flush_video_encoder(app);
    (void)record_flush_audio_encoder(app);

    record_close_session_resources(app);

    app->record.session_active = 0;
    app->record.stopping_session = 0;
    app->record.accepting_frames = 0;

    SDL_UnlockMutex(app->record.mutex);

    LOG_INFO("record session stopped");
    return 0;
}

void record_notify_pause(AppState *app){
    if(!app || !app->record.enabled){
        return;
    }

    frame_queue_flush(&app->record.queue);
    audio_queue_flush(&app->record.audio_queue);

    if(app->record.mutex){
        SDL_LockMutex(app->record.mutex);

        if(app->record.audio_fifo){
            av_audio_fifo_drain(app->record.audio_fifo,
                                av_audio_fifo_size(app->record.audio_fifo));
        }

        app->record.have_audio_anchor = 0;
        app->record.audio_next_pts = AV_NOPTS_VALUE;

        SDL_UnlockMutex(app->record.mutex);
    }
}

int record_init(AppState *app){
    if(!app){
        return -1;
    }

    if(record_init_common_objects(app) < 0){
        goto fail;
    }
    if(record_init_queue(app) < 0){
        goto fail;
    }

    app->record.thread = SDL_CreateThread(record_thread_main, "record_thread", app);
    if(!app->record.thread){
        LOG_ERROR("SDL_CreateThread (record) failed: %s", SDL_GetError());
        goto fail;
    }

    app->record.fatal_error = 0;
    app->record.accepting_frames = 0;
    app->record.enabled = 1;
    app->record.session_active = 0;
    app->record.stopping_session = 0;

    return 0;

fail:
    record_close(app);
    return -1;
}

void record_close(AppState *app)
{
    if(!app){
        return;
    }

    if(app->record.session_active){
        (void)record_session_stop(app);
    }

    app->record.accepting_frames = 0;

    frame_queue_stop(&app->record.queue);
    audio_queue_stop(&app->record.audio_queue);

    if(app->record.thread){
        SDL_WaitThread(app->record.thread, NULL);
        app->record.thread = NULL;
    }

    SDL_LockMutex(app->record.mutex);
    record_close_session_resources(app);
    SDL_UnlockMutex(app->record.mutex);

    frame_queue_destroy(&app->record.queue);
    audio_queue_destroy(&app->record.audio_queue);

    if(app->record.mutex){
        SDL_DestroyMutex(app->record.mutex);
        app->record.mutex = NULL;
    }

    app->record.enabled = 0;
    app->record.accepting_frames = 0;
    app->record.fatal_error = 0;
    app->record.session_active = 0;
    app->record.stopping_session = 0;
    app->record.session_count = 0;
    app->record.active_output_path[0] = '\0';

    record_reset_timeline_state(app);
}