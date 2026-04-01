#include <stdio.h>
#include <string.h>
#include<sys/stat.h>
#include<errno.h>

#include "stream.h"
#include "log.h"
#include "webrtc_bridge.h"


static int stream_webrtc_ensure_signal_dir(AppState *app){
    if(!app || app->stream.webrtc_signal_dir[0] == '\0'){
        return -1;
    }

    if(mkdir(app->stream.webrtc_signal_dir,0755) == 0){
        return 0;
    }

    if (errno == EEXIST) {
        return 0;
    }

    LOG_ERROR("mkdir(%s) failed: %s",
              app->stream.webrtc_signal_dir,
              strerror(errno));
    return -1;
}

static int stream_webrtc_write_text_file(const char *path,const char *text){
    FILE *fp;

    if(!path || !text){
        return -1;
    }

    fp = fopen(path,"wb");
    if (!fp) {
        LOG_ERROR("fopen(%s, wb) failed: %s", path, strerror(errno));
        return -1;
    }

    if (fwrite(text, 1, strlen(text), fp) != strlen(text)) {
        LOG_ERROR("fwrite(%s) failed", path);
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

static char *stream_webrtc_read_text_file(const char *path)
{
    FILE *fp;
    long size;
    char *buf;

    if (!path) {
        return NULL;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }

    //int fseek(FILE *stream, long offset, int whence);
    //                  文件指针  偏移量/字节数   从哪个位置开始偏移
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }

    if (size > 0 && fread(buf, 1, (size_t)size, fp) != (size_t)size) {
        free(buf);
        fclose(fp);
        return NULL;
    }

    buf[size] = '\0';
    fclose(fp);
    return buf;
}

static int stream_webrtc_parse_candidate_file(const char *text,
                                              char *mid,
                                              size_t mid_size,
                                              char *candidate,
                                              size_t candidate_size)
{
    const char *line_end;
    const char *body;

    if (!text || !mid || !candidate || mid_size == 0 || candidate_size == 0) {
        return -1;
    }

    mid[0] = '\0';
    candidate[0] = '\0';

    if (strncmp(text, "mid=", 4) == 0) {// 有 mid 行
        line_end = strchr(text, '\n'); // 找到第一行的结尾
        if (!line_end) {
            return -1;
        }
        // 提取 "mid=" 之后到换行符之前的内容
        snprintf(mid, mid_size, "%.*s", (int)(line_end - (text + 4)), text + 4);
        body = line_end + 1;// body 指向第二行，即 candidate 内容
    } else {
        body = text;// 没有 mid 行，整个文本都是 candidate
    }

    while (*body == '\r' || *body == '\n') {
        ++body;
    }

    snprintf(candidate, candidate_size, "%s", body);

    while (candidate[0] != '\0') {
        size_t len = strlen(candidate);
        if (len == 0) {
            break;
        }

        if (candidate[len - 1] == '\r' || candidate[len - 1] == '\n') {
            candidate[len - 1] = '\0';
            continue;
        }

        break;
    }

    return candidate[0] != '\0' ? 0 : -1;
}

int stream_webrtc_load_next_remote_candidate_file(AppState *app)
{
    char path[512];
    char *text;
    char mid[128];
    char candidate[2048];
    unsigned int index;
    int ret;

    if (!app || !app->stream.webrtc_sender) {
        return -1;
    }

    index = app->stream.webrtc_remote_candidate_index;

    snprintf(path,
             sizeof(path),
             "%s/browser_candidate_%06u.txt",
             app->stream.webrtc_signal_dir,
             index);

    text = stream_webrtc_read_text_file(path);
    if (!text) {
        LOG_WARN("webrtc candidate file not found: %s", path);
        return 1;
    }

    if (stream_webrtc_parse_candidate_file(text,
                                           mid,
                                           sizeof(mid),
                                           candidate,
                                           sizeof(candidate)) < 0) {
        free(text);
        LOG_ERROR("failed to parse webrtc candidate file: %s", path);
        return -1;
    }

    ret = webrtc_sender_add_remote_candidate(app->stream.webrtc_sender,
                                             candidate,
                                             mid[0] ? mid : NULL);
    free(text);

    if (ret < 0) {
        LOG_ERROR("webrtc_sender_add_remote_candidate failed for %s", path);
        return -1;
    }

    app->stream.webrtc_remote_candidate_index++;
    LOG_INFO("webrtc remote candidate loaded from %s", path);
    return 0;
}

//加入WebRtc回调函数
static void stream_webrtc_on_log(WebRtcBridgeLogLevel level,
                                 const char *message,
                                 void *userdata)
{
    (void)userdata;

    switch (level) {
    case WEBRTC_LOG_INFO:
        LOG_INFO("webrtc: %s", message ? message : "");
        break;
    case WEBRTC_LOG_WARN:
        LOG_WARN("webrtc: %s", message ? message : "");
        break;
    case WEBRTC_LOG_ERROR:
    default:
        LOG_ERROR("webrtc: %s", message ? message : "");
        break;
    }
}

static void stream_webrtc_on_state(WebRtcPeerState state, void *userdata)
{
    AppState *app = (AppState *)userdata;
    (void)app;

    LOG_INFO("webrtc state changed: %s", webrtc_peer_state_name(state));
}

static void stream_webrtc_on_local_description(const char *type,
                                               const char *sdp,
                                               void *userdata)
{
    AppState *app = (AppState *)userdata;
    char path[512];

    if(!app){
        return;
    }

    LOG_INFO("webrtc local description ready: type=%s", type ? type : "");
    if (sdp && sdp[0] != '\0') {
        LOG_INFO("webrtc local sdp:\n%s", sdp);
    }

    if (stream_webrtc_ensure_signal_dir(app) < 0) {
        return;
    }

    snprintf(path, sizeof(path), "%s/native_offer.sdp", app->stream.webrtc_signal_dir);

    if (stream_webrtc_write_text_file(path, sdp ? sdp : "") == 0) {
        LOG_INFO("webrtc offer written to %s", path);
    }
}

static void stream_webrtc_on_local_candidate(const char *candidate,
                                             const char *mid,
                                             void *userdata)
{
    AppState *app = (AppState *)userdata;
    char path[512];
    char body[2048];
    unsigned int index;
    
    if(!app){
        return;
    }

    LOG_INFO("webrtc local candidate ready: mid=%s candidate=%s",
             mid ? mid : "",
             candidate ? candidate : "");

    if(stream_webrtc_ensure_signal_dir(app) < 0){
        return;
    }

    index = app->stream.webrtc_local_candidate_index++;

    snprintf(path,
             sizeof(path),
             "%s/native_candidate_%06u.txt",
             app->stream.webrtc_signal_dir,
             index);

    snprintf(body,
             sizeof(body),
             "mid=%s\n%s\n",
             mid ? mid : "",
             candidate ? candidate : "");

    if (stream_webrtc_write_text_file(path, body) == 0) {
        LOG_INFO("webrtc local candidate written to %s", path);
    }    
}

int stream_webrtc_load_remote_answer_file(AppState *app){
    char path[512];
    char *sdp;
    int ret;

    if(!app || !app->stream.webrtc_sender){
        return -1;
    }

    snprintf(path,sizeof(path),"%s/browser_answer.sdp",app->stream.webrtc_signal_dir);

    sdp = stream_webrtc_read_text_file(path);
    if (!sdp) {
        LOG_WARN("webrtc answer file not found: %s", path);
        return 1;
    }

    ret = webrtc_sender_set_remote_description(app->stream.webrtc_sender,"answer",sdp);

    free(sdp);

    if (ret < 0) {
        LOG_ERROR("webrtc_sender_set_remote_description(answer) failed");
        return -1;
    }

    LOG_INFO("webrtc remote answer loaded from %s", path);
    return 0;
}

static StreamBackendType stream_normalize_backend(StreamBackendType backend_type)
{
    switch (backend_type) {
    case STREAM_BACKEND_RTSP:
    case STREAM_BACKEND_WEBRTC:
        return backend_type;

    default:
        return STREAM_BACKEND_RTSP;
    }
}

const char *stream_backend_name(StreamBackendType type)
{
    switch (stream_normalize_backend(type)) {
    case STREAM_BACKEND_WEBRTC:
        return "webrtc";
    case STREAM_BACKEND_RTSP:
    default:
        return "rtsp";
    }
}

static int64_t stream_timestamp_us_to_pts(uint64_t delta_us, AVRational time_base)
{
    return av_rescale_q((int64_t)delta_us,
                        (AVRational){1, 1000000},
                        time_base);
}

static int stream_is_supported_input_pixfmt(uint32_t pixfmt)
{
    return pixfmt == V4L2_PIX_FMT_YUYV;
}

static void stream_stop_queues(AppState *app)
{
    if (!app) {
        return;
    }

    frame_queue_stop(&app->stream.queue);
    audio_queue_stop(&app->stream.audio_queue);
}

static void stream_enter_fatal_error(AppState *app, const char *reason)
{
    if (!app) {
        return;
    }

    if (app->stream.mutex) {
        SDL_LockMutex(app->stream.mutex);

        if (!app->stream.fatal_error) {
            app->stream.fatal_error = 1;
            app->stream.accepting_frames = 0;
            LOG_ERROR("stream fatal error: %s",
                      reason ? reason : "unknown");
        }

        SDL_UnlockMutex(app->stream.mutex);
    }

    stream_stop_queues(app);
}

static void stream_reset_runtime_state(AppState *app)
{
    if (!app) {
        return;
    }

    app->stream.frame_index = 0;
    app->stream.base_timestamp_us = 0;
    app->stream.have_base_timestamp = 0;
    app->stream.last_input_pts = AV_NOPTS_VALUE;
    app->stream.frames_encoded = 0;
    app->stream.accepting_frames = 0;
    app->stream.fatal_error = 0;
}

void stream_state_init(AppState *app, const char *url, int fps,StreamBackendType backend_type)
{
    if (!app) {
        return;
    }

    memset(&app->stream, 0, sizeof(app->stream));

    app->stream.backend_type = stream_normalize_backend(backend_type);

    snprintf(app->stream.output_url,
             sizeof(app->stream.output_url),
             "%s",
             url ? url : "");

    snprintf(app->stream.webrtc_signal_dir,
             sizeof(app->stream.webrtc_signal_dir),
             "%s",
             "./webrtc_manual");

    app->stream.webrtc_local_candidate_index = 1;
    app->stream.webrtc_remote_candidate_index = 1;

    app->stream.fps = (fps > 0) ? fps : 30;
    app->stream.enabled = 0;

    stream_reset_runtime_state(app);
}

static void stream_apply_webrtc_h264_options(AppState *app, AVDictionary **codec_opts)
{
    int fps;
    char x264_params[256];

    if (!app || !app->stream.enc_ctx) {
        return;
    }

    fps = (app->stream.fps > 0) ? app->stream.fps : 30;

    /*
     * WebRTC / browser 更喜欢：
     * - baseline profile
     * - 无 B 帧
     * - 固定 GOP
     * - in-band SPS/PPS
     */
    app->stream.enc_ctx->gop_size = fps;
    app->stream.enc_ctx->max_b_frames = 0;
    app->stream.enc_ctx->refs = 1;

    av_opt_set(app->stream.enc_ctx->priv_data, "preset", "veryfast", 0);
    av_opt_set(app->stream.enc_ctx->priv_data, "tune", "zerolatency", 0);
    av_opt_set(app->stream.enc_ctx->priv_data, "profile", "baseline", 0);
    av_opt_set(app->stream.enc_ctx->priv_data, "level", "3.1", 0);

    /*
     * 对 libx264 最关键：
     * - repeat-headers=1 让关键帧附近带上 SPS/PPS
     * - annexb=1 输出 Annex B 起始码格式
     * - keyint/min-keyint 固定关键帧周期
     * - scenecut=0 避免 GOP 被场景切换打乱
     */
    if (app->stream.encoder &&
        strstr(app->stream.encoder->name, "libx264") != NULL) {
        snprintf(x264_params,
                 sizeof(x264_params),
                 "repeat-headers=1:annexb=1:keyint=%d:min-keyint=%d:scenecut=0:bframes=0:ref=1",
                 fps,
                 fps);

        av_dict_set(codec_opts, "x264-params", x264_params, 0);
    }

    LOG_INFO("webrtc h264 encoder config: codec=%s gop=%d profile=baseline",
             app->stream.encoder ? app->stream.encoder->name : "unknown",
             fps);
}

static int stream_init_encoder(AppState *app)
{
    int ret;
    AVDictionary *codec_opts = NULL;

    // app->stream.encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    // if (!app->stream.encoder) {
    //     LOG_ERROR("avcodec_find_encoder(H264) failed");
    //     return -1;
    // }

    app->stream.encoder = avcodec_find_encoder_by_name("libx264");
    if (!app->stream.encoder) {
        LOG_ERROR("avcodec_find_encoder_by_name(libx264) failed");
        return -1;
    }

    app->stream.enc_ctx = avcodec_alloc_context3(app->stream.encoder);
    if (!app->stream.enc_ctx) {
        LOG_ERROR("avcodec_alloc_context3 failed");
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

    stream_apply_webrtc_h264_options(app,&codec_opts);

    ret = avcodec_open2(app->stream.enc_ctx, app->stream.encoder, &codec_opts);
    av_dict_free(&codec_opts);

    if (ret < 0) {
        LOG_ERROR("avcodec_open2 failed");
        return -1;
    }

    return 0;
}

static int stream_init_buffers(AppState *app)
{
    int ret;

    app->stream.sws_ctx = sws_getContext(app->width,
                                         app->height,
                                         AV_PIX_FMT_YUYV422,
                                         app->width,
                                         app->height,
                                         AV_PIX_FMT_YUV420P,
                                         SWS_BILINEAR,
                                         NULL,
                                         NULL,
                                         NULL);
    if (!app->stream.sws_ctx) {
        LOG_ERROR("sws_getContext failed");
        return -1;
    }

    app->stream.yuv_frame = av_frame_alloc();
    if (!app->stream.yuv_frame) {
        LOG_ERROR("av_frame_alloc failed");
        return -1;
    }

    app->stream.yuv_frame->format = AV_PIX_FMT_YUV420P;
    app->stream.yuv_frame->width = app->width;
    app->stream.yuv_frame->height = app->height;

    ret = av_frame_get_buffer(app->stream.yuv_frame, 32);
    if (ret < 0) {
        LOG_ERROR("av_frame_get_buffer failed");
        return -1;
    }

    app->stream.pkt = av_packet_alloc();
    if (!app->stream.pkt) {
        LOG_ERROR("av_packet_alloc failed");
        return -1;
    }

    app->stream.mutex = SDL_CreateMutex();
    if (!app->stream.mutex) {
        LOG_ERROR("SDL_CreateMutex(stream) failed: %s", SDL_GetError());
        return -1;
    }

    return 0;
}

static int stream_init_queue(AppState *app)
{
    size_t frame_bytes = app->sizeimage;

    if (frame_queue_init(&app->stream.queue,
                         8,
                         frame_bytes,
                         app->width,
                         app->height,
                         (int)app->bytesperline,
                         app->pixfmt) < 0) {
        LOG_ERROR("frame_queue_init(stream) failed");
        return -1;
    }

    return 0;
}

static int stream_init_rtsp_output(AppState *app)
{
    int ret;
    AVDictionary *opts = NULL;

    ret = avformat_alloc_output_context2(&app->stream.ofmt_ctx,
                                         NULL,
                                         "rtsp",
                                         app->stream.output_url);
    if (ret < 0 || !app->stream.ofmt_ctx) {
        LOG_ERROR("avformat_alloc_output_context2 failed");
        return -1;
    }

    app->stream.video_st = avformat_new_stream(app->stream.ofmt_ctx, NULL);
    if (!app->stream.video_st) {
        LOG_ERROR("avformat_new_stream failed");
        return -1;
    }

    app->stream.video_st->time_base = app->stream.enc_ctx->time_base;
    app->stream.video_st->avg_frame_rate = app->stream.enc_ctx->framerate;
    app->stream.video_st->codecpar->codec_tag = 0;

    ret = avcodec_parameters_from_context(app->stream.video_st->codecpar,
                                          app->stream.enc_ctx);
    if (ret < 0) {
        LOG_ERROR("avcodec_parameters_from_context failed");
        return -1;
    }

    /*
     * RTSP 推流不需要手动 avio_open2。
     * 通过 muxer 私有选项把传输方式交给 avformat_write_header。
     */
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);

    ret = avformat_write_header(app->stream.ofmt_ctx, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        LOG_ERROR("avformat_write_header failed");
        return -1;
    }

    return 0;
}

static int stream_init_webrtc_output(AppState *app)
{
    WebRtcSenderConfig config;
    WebRtcSenderCallbacks callbacks;
    int ret;

    if (!app) {
        return -1;
    }

    webrtc_sender_config_init(&config);
    webrtc_sender_callbacks_init(&callbacks);

    snprintf(config.stream_name,
             sizeof(config.stream_name),
             "%s",
             "camera");
    snprintf(config.video_codec,
             sizeof(config.video_codec),
             "%s",
             "H264");

    config.width = app->width;
    config.height = app->height;
    config.fps = app->stream.fps;

    callbacks.on_log = stream_webrtc_on_log;
    callbacks.on_state = stream_webrtc_on_state;
    callbacks.on_local_description = stream_webrtc_on_local_description;
    callbacks.on_local_candidate = stream_webrtc_on_local_candidate;
    callbacks.userdata = app;

    ret = webrtc_sender_create(&app->stream.webrtc_sender,
                               &config,
                               &callbacks);
    if (ret < 0) {
        LOG_ERROR("webrtc_sender_create failed");
        return -1;
    }

    ret = webrtc_sender_start(app->stream.webrtc_sender);
    if (ret < 0) {
        LOG_ERROR("webrtc_sender_start failed");
        webrtc_sender_destroy(&app->stream.webrtc_sender);
        return -1;
    }

    return 0;
}

static int stream_init_output(AppState *app)
{
    StreamBackendType backend_type;

    if (!app) {
        return -1;
    }

    backend_type = stream_normalize_backend(app->stream.backend_type);

    switch (backend_type) {
    case STREAM_BACKEND_RTSP:
        return stream_init_rtsp_output(app);

    case STREAM_BACKEND_WEBRTC:
        return stream_init_webrtc_output(app);


    default:
        LOG_ERROR("unsupported stream backend type: %d", (int)backend_type);
        return -1;
    }
}

static int stream_write_webrtc_packets(AppState *app)
{
    int ret;

    while (1) {
        WebRtcEncodedVideoFrame frame;
        int64_t pts_us;
        int64_t dts_us;

        ret = avcodec_receive_packet(app->stream.enc_ctx, app->stream.pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return 0;
        }

        if (ret < 0) {
            LOG_ERROR("avcodec_receive_packet(webrtc) failed");
            return -1;
        }

        pts_us = 0;
        dts_us = 0;

        if (app->stream.pkt->pts != AV_NOPTS_VALUE) {
            pts_us = av_rescale_q(app->stream.pkt->pts,
                                  app->stream.enc_ctx->time_base,
                                  (AVRational){1, 1000000});
        }

        if (app->stream.pkt->dts != AV_NOPTS_VALUE) {
            dts_us = av_rescale_q(app->stream.pkt->dts,
                                  app->stream.enc_ctx->time_base,
                                  (AVRational){1, 1000000});
        }

        frame.data = app->stream.pkt->data;
        frame.size = (size_t)app->stream.pkt->size;
        frame.pts_us = pts_us;
        frame.dts_us = dts_us;
        frame.is_keyframe = (app->stream.pkt->flags & AV_PKT_FLAG_KEY) ? 1 : 0;

        ret = webrtc_sender_send_video(app->stream.webrtc_sender, &frame);
        av_packet_unref(app->stream.pkt);

        if (ret < 0) {
            LOG_ERROR("webrtc_sender_send_video failed");
            return -1;
        }
    }
}

static int stream_write_rtsp_packets(AppState *app)
{
    int ret;

    while (1) {
        ret = avcodec_receive_packet(app->stream.enc_ctx, app->stream.pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return 0;
        }

        if (ret < 0) {
            LOG_ERROR("avcodec_receive_packet(rtsp) failed");
            return -1;
        }

        av_packet_rescale_ts(app->stream.pkt,
                             app->stream.enc_ctx->time_base,
                             app->stream.video_st->time_base);
        app->stream.pkt->stream_index = app->stream.video_st->index;

        ret = av_interleaved_write_frame(app->stream.ofmt_ctx, app->stream.pkt);
        av_packet_unref(app->stream.pkt);

        if (ret < 0) {
            LOG_ERROR("av_interleaved_write_frame failed");
            return -1;
        }
    }
}

static int stream_write_encoded_packets(AppState *app)
{
    StreamBackendType backend_type;

    if (!app) {
        return -1;
    }

    backend_type = stream_normalize_backend(app->stream.backend_type);

    switch (backend_type) {
    case STREAM_BACKEND_RTSP:
        return stream_write_rtsp_packets(app);

    case STREAM_BACKEND_WEBRTC:
        return stream_write_webrtc_packets(app);

    default:
        LOG_ERROR("unsupported backend in stream_write_encoded_packets: %d",
                  (int)backend_type);
        return -1;
    }
}

static void stream_flush_encoder(AppState *app)
{
    int ret;

    if (!app || !app->stream.enc_ctx || !app->stream.ofmt_ctx) {
        return;
    }

    ret = avcodec_send_frame(app->stream.enc_ctx, NULL);
    if (ret < 0) {
        return;
    }

    (void)stream_write_encoded_packets(app);
}

static int stream_consume_packet(AppState *app, const FramePacket *pkt)
{
    const uint8_t *src_slices[1];
    int src_stride[1];
    int ret;
    uint64_t delta_us;
    int64_t pts_raw;
    int64_t pts;

    if (!app || !pkt || !pkt->data || !app->stream.enabled) {
        return 0;
    }

    if (!app->stream.mutex) {
        return -1;
    }

    if (!stream_is_supported_input_pixfmt(pkt->pixfmt)) {
        LOG_ERROR("unsupported stream packet pixfmt: 0x%x", pkt->pixfmt);
        return -1;
    }

    if (pkt->stride <= 0) {
        LOG_ERROR("invalid stream packet stride: %d", pkt->stride);
        return -1;
    }

    if (pkt->width != app->width || pkt->height != app->height) {
        LOG_ERROR("stream packet size mismatch: pkt=%dx%d app=%dx%d",
                  pkt->width,
                  pkt->height,
                  app->width,
                  app->height);
        return -1;
    }

    SDL_LockMutex(app->stream.mutex);

    /*
     * 把统一媒体时钟映射到编码器 time_base：
     * capture_time_us
     *   -> 相对首帧时间 delta_us
     *   -> time_base 下的 pts
     */
    if (!app->stream.have_base_timestamp) {
        app->stream.base_timestamp_us = pkt->meta.capture_time_us;
        app->stream.have_base_timestamp = 1;
    }

    if (pkt->meta.capture_time_us < app->stream.base_timestamp_us) {
        delta_us = 0;
    } else {
        delta_us = pkt->meta.capture_time_us - app->stream.base_timestamp_us;
    }

    pts_raw = stream_timestamp_us_to_pts(delta_us,
                                         app->stream.enc_ctx->time_base);
    pts = pts_raw;

    if (app->stream.last_input_pts != AV_NOPTS_VALUE &&
        pts_raw <= app->stream.last_input_pts) {
        LOG_WARN("stream pts adjusted for monotonicity: raw=%lld last=%lld adjusted=%lld",
                 (long long)pts_raw,
                 (long long)app->stream.last_input_pts,
                 (long long)(app->stream.last_input_pts + 1));
        pts = app->stream.last_input_pts + 1;
    }

    app->stream.last_input_pts = pts;

    ret = av_frame_make_writable(app->stream.yuv_frame);
    if (ret < 0) {
        SDL_UnlockMutex(app->stream.mutex);
        LOG_ERROR("av_frame_make_writable failed");
        return -1;
    }

    src_slices[0] = pkt->data;
    src_stride[0] = pkt->stride;

    sws_scale(app->stream.sws_ctx,
              src_slices,
              src_stride,
              0,
              pkt->height,
              app->stream.yuv_frame->data,
              app->stream.yuv_frame->linesize);

    app->stream.yuv_frame->pts = pts;
    app->stream.frame_index++;

    ret = avcodec_send_frame(app->stream.enc_ctx, app->stream.yuv_frame);
    if (ret < 0) {
        SDL_UnlockMutex(app->stream.mutex);
        LOG_ERROR("avcodec_send_frame failed");
        return -1;
    }

    ret = stream_write_encoded_packets(app);
    if (ret < 0) {
        SDL_UnlockMutex(app->stream.mutex);
        return -1;
    }

    app->stream.frames_encoded++;
    SDL_UnlockMutex(app->stream.mutex);

    return 0;
}

static int stream_thread_main(void *userdata)
{
    AppState *app = (AppState *)userdata;
    FramePacket pkt;
    FrameQueuePopResult pop_ret;
    size_t frame_bytes;

    if (!app) {
        return -1;
    }

    frame_bytes = app->sizeimage;

    if (frame_packet_init(&pkt,
                          frame_bytes,
                          app->width,
                          app->height,
                          (int)app->bytesperline,
                          app->pixfmt) < 0) {
        LOG_ERROR("frame_packet_init(stream) failed");
        return -1;
    }

    while (1) {
        pop_ret = frame_queue_pop(&app->stream.queue, &pkt, 200);

        if (pop_ret == FRAME_QUEUE_POP_TIMEOUT) {
            continue;
        }

        if (pop_ret == FRAME_QUEUE_POP_STOPPED) {
            break;
        }

        if (pop_ret == FRAME_QUEUE_POP_ERROR) {
            LOG_ERROR("frame_queue_pop(stream) failed");
            break;
        }

        if (stream_consume_packet(app, &pkt) < 0) {
            stream_enter_fatal_error(app, "stream_consume_packet failed");
            break;
        }
    }

    frame_packet_free(&pkt);
    return 0;
}

int stream_init(AppState *app)
{
    if (!app) {
        return -1;
    }

    if (stream_init_encoder(app) < 0) {
        goto fail;
    }

    if (stream_init_buffers(app) < 0) {
        goto fail;
    }

    if (stream_init_queue(app) < 0) {
        goto fail;
    }

    if (stream_init_output(app) < 0) {
        goto fail;
    }

    /*
     * 到这里说明编码器、队列、输出上下文都已经准备好了，
     * 模块可视为“已初始化完成”。
     * 线程是否正在取帧，由 accepting_frames 单独表达。
     */
    app->stream.enabled = 1;

    app->stream.thread = SDL_CreateThread(stream_thread_main,
                                          "stream_thread",
                                          app);
    if (!app->stream.thread) {
        LOG_ERROR("SDL_CreateThread(stream) failed: %s", SDL_GetError());
        goto fail;
    }

    app->stream.accepting_frames = 1;
    app->stream.fatal_error = 0;

    return 0;

fail:
    stream_close(app);
    return -1;
}

void stream_close(AppState *app)
{
    if (!app) {
        return;
    }

    app->stream.accepting_frames = 0;
    stream_stop_queues(app);

    if (app->stream.thread) {
        SDL_WaitThread(app->stream.thread, NULL);
        app->stream.thread = NULL;
    }

    if (app->stream.enabled && app->stream.mutex) {
        SDL_LockMutex(app->stream.mutex);
        stream_flush_encoder(app);
        SDL_UnlockMutex(app->stream.mutex);
    }

    /*
     * 对 RTSP 这种 nofile muxer，也可能需要 write_trailer。
     * 这里只要输出上下文和视频流已经建立，就尝试正常收尾。
     */
    if (app->stream.ofmt_ctx && app->stream.video_st) {
        av_write_trailer(app->stream.ofmt_ctx);
    }

    if (app->stream.ofmt_ctx &&
        !(app->stream.ofmt_ctx->oformat->flags & AVFMT_NOFILE) &&
        app->stream.ofmt_ctx->pb) {
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
    audio_queue_destroy(&app->stream.audio_queue);

    if (app->stream.mutex) {
        SDL_DestroyMutex(app->stream.mutex);
        app->stream.mutex = NULL;
    }

    app->stream.video_st = NULL;
    app->stream.encoder = NULL;
    app->stream.enabled = 0;

    if (app->stream.webrtc_sender) {
        webrtc_sender_stop(app->stream.webrtc_sender);
        webrtc_sender_destroy(&app->stream.webrtc_sender);
    }

    stream_reset_runtime_state(app);
}