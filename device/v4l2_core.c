#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>

#include "app.h"
#include "app_clock.h"
#include "log.h"
#include "record.h"
#include "stream.h"
#include "time_utils.h"
#include "v4l2_core.h"

/*
 * 这个文件专注于“视频采集核心”：
 * - V4L2 设备打开/协商格式
 * - mmap 缓冲区
 * - 采集线程
 * - control 查询与设置
 *
 * 这一轮整理不主动改变主业务行为，
 * 重点是收紧边界、统一命名语义、移除重复逻辑。
 */


 /*
    在 V4L2 中几乎所有操作都通过它完成
        ioctl(fd, request, arg);
            │    │        └─ 数据（传入或传出）
            │    └────────── 命令（做什么操作）
            └─────────────── 文件描述符（哪个设备）
    对 ioctl 加了一层重试保护，屏蔽信号中断的干扰，
    让上层调用者只需要关心成功或真正的错误，不用关心信号问题。
*/
static int xioctl(int fd, int request, void *arg)
{
    int ret;

    do {
        ret = ioctl(fd, request, arg);
    } while (ret == -1 && errno == EINTR);

    return ret;
}

void app_state_init(AppState *app, const char *device_path)
{
    if (!app) {
        return;
    }

    memset(app, 0, sizeof(*app));

    if (device_path) {
        snprintf(app->device_path, sizeof(app->device_path), "%s", device_path);
    }

    app->fd = -1;

    /*
     * 这里只保留“安全默认值”，
     * 不在这里决定最终业务配置，避免和 app_apply_config() 语义冲突。
     */
    app->width = 640;
    app->height = 480;
    app->pixfmt = V4L2_PIX_FMT_YUYV;

    app->stream_on = 0;
    app->record_on = 0;

    app->latest.rgb = NULL;
    app->latest.width = 0;
    app->latest.height = 0;
    app->latest.bytes = 0;
    app->latest.frame_id = 0;
    app->latest.mutex = NULL;

    app->preview_rgb = NULL;
    app->preview_rgb_bytes = 0;

    app->capture_yuyv = NULL;
    app->capture_yuyv_bytes = 0;

    app->buffers = NULL;
    app->n_buffers = 0;

    app->bytesperline = 0;
    app->sizeimage = 0;

    app->frames_captured = 0;
    app->frames_dropped = 0;
    app->last_stats_ms = 0;

    app->quit = 0;
    app->paused = 0;
}

int open_device(AppState *app)
{
    if (!app) {
        return -1;
    }

    app->fd = open(app->device_path, O_RDWR | O_NONBLOCK, 0);
    if (app->fd < 0) {
        perror("open");
        return -1;
    }

    return 0;
}

int query_capability(AppState *app)
{
    struct v4l2_capability cap;

    if (!app) {
        return -1;
    }

    memset(&cap, 0, sizeof(cap));

    if (xioctl(app->fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("VIDIOC_QUERYCAP");
        return -1;
    }

    printf("driver:%s\n", cap.driver);
    printf("card:%s\n", cap.card);
    printf("bus_info:%s\n", cap.bus_info);
    printf("version:%u.%u.%u\n",
           (cap.version >> 16) & 0xff,
           (cap.version >> 8) & 0xff,
           cap.version & 0xff);
    printf("capabilities:0x%08x\n", cap.capabilities);

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "设备不支持 V4L2_CAP_VIDEO_CAPTURE\n");
        return -1;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "设备不支持 V4L2_CAP_STREAMING\n");
        return -1;
    }

    return 0;
}

static void print_fourcc(uint32_t pixfmt)
{
    printf("'%c%c%c%c'",
           pixfmt & 0xff,
           (pixfmt >> 8) & 0xff,
           (pixfmt >> 16) & 0xff,
           (pixfmt >> 24) & 0xff);
}

int enum_formats(AppState *app)
{
    struct v4l2_fmtdesc fmtdesc;

    if (!app) {
        return -1;
    }

    memset(&fmtdesc, 0, sizeof(fmtdesc));
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    printf("设备支持的采集格式：\n");

    for (fmtdesc.index = 0;; fmtdesc.index++) {
        if (xioctl(app->fd, VIDIOC_ENUM_FMT, &fmtdesc) < 0) {
            if (errno == EINVAL) {
                break;
            }
            perror("VIDIOC_ENUM_FMT");
            return -1;
        }

        printf("[%u] ", fmtdesc.index);
        print_fourcc(fmtdesc.pixelformat);
        printf(" %s\n", fmtdesc.description);
    }

    return 0;
}

int enum_frame_sizes(AppState *app, uint32_t pixfmt)
{
    struct v4l2_frmsizeenum frmsize;

    if (!app) {
        return -1;
    }

    memset(&frmsize, 0, sizeof(frmsize));
    frmsize.pixel_format = pixfmt;

    printf("格式 ");
    print_fourcc(pixfmt);
    printf(" 支持的分辨率：\n");

    for (frmsize.index = 0;; frmsize.index++) {
        if (xioctl(app->fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) < 0) {
            if (errno == EINVAL) {
                break;
            }
            perror("VIDIOC_ENUM_FRAMESIZES");
            return -1;
        }

        if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
            printf("[%u] %u x %u\n",
                   frmsize.index,
                   frmsize.discrete.width,
                   frmsize.discrete.height);
        } else if (frmsize.type == V4L2_FRMSIZE_TYPE_STEPWISE) {
            printf("[%u] stepwise: %u x %u -> %u x %u, step %u x %u\n",
                   frmsize.index,
                   frmsize.stepwise.min_width,
                   frmsize.stepwise.min_height,
                   frmsize.stepwise.max_width,
                   frmsize.stepwise.max_height,
                   frmsize.stepwise.step_width,
                   frmsize.stepwise.step_height);
        } else if (frmsize.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
            printf("[%u] continuous: %u x %u -> %u x %u\n",
                   frmsize.index,
                   frmsize.stepwise.min_width,
                   frmsize.stepwise.min_height,
                   frmsize.stepwise.max_width,
                   frmsize.stepwise.max_height);
        }
    }

    return 0;
}

int enum_frame_intervals(AppState *app,
                         uint32_t pixfmt,
                         unsigned int width,
                         unsigned int height)
{
    struct v4l2_frmivalenum frmival;

    if (!app) {
        return -1;
    }

    memset(&frmival, 0, sizeof(frmival));
    frmival.pixel_format = pixfmt;
    frmival.width = width;
    frmival.height = height;

    printf("格式 ");
    print_fourcc(pixfmt);
    printf(" 在 %u x %u 下支持的帧间隔:\n", width, height);

    for (frmival.index = 0;; frmival.index++) {
        if (xioctl(app->fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmival) < 0) {
            if (errno == EINVAL) {
                break;
            }
            perror("VIDIOC_ENUM_FRAMEINTERVALS");
            return -1;
        }

        if (frmival.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
            printf("[%u] %u / %u 秒 (约 %.2f fps)\n",
                   frmival.index,
                   frmival.discrete.numerator,
                   frmival.discrete.denominator,
                   (double)frmival.discrete.denominator /
                       frmival.discrete.numerator);
        } else {
            printf("[%u] 非离散帧间隔 (stepwise/continuous)\n",
                   frmival.index);
        }
    }

    return 0;
}

int set_format(AppState *app)
{
    struct v4l2_format fmt;

    if (!app) {
        return -1;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = app->width;
    fmt.fmt.pix.height = app->height;
    fmt.fmt.pix.pixelformat = app->pixfmt;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (xioctl(app->fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("VIDIOC_S_FMT");
        return -1;
    }

    app->width = fmt.fmt.pix.width;
    app->height = fmt.fmt.pix.height;
    app->pixfmt = fmt.fmt.pix.pixelformat;
    app->bytesperline = fmt.fmt.pix.bytesperline;
    app->sizeimage = fmt.fmt.pix.sizeimage;

    LOG_INFO("format negotiated: width=%d height=%d pixfmt=0x%x bytesperline=%u sizeimage=%u",
             app->width,
             app->height,
             app->pixfmt,
             app->bytesperline,
             app->sizeimage);

    return 0;
}

void explain_selected_format(uint32_t pixfmt)
{
    printf("当前选择的格式说明：\n");

    if (pixfmt == V4L2_PIX_FMT_YUYV) {
        printf("YUYV: 原始打包 YUV 格式，适合理解 V4L2 buffer 与像素布局。\n");
        printf("  优点: 无需先解码，调试采集链路直观。\n");
        printf("  缺点: 带宽占用通常比 MJPEG 更大。\n");
    } else if (pixfmt == V4L2_PIX_FMT_MJPEG) {
        printf("MJPEG: 压缩格式，常见于 USB 摄像头。\n");
        printf("  优点: 带宽压力较小，高分辨率时更容易跑高 fps。\n");
        printf("  缺点: 还需要额外解码才能显示或处理。\n");
    } else {
        printf("其他格式：后续需要根据具体格式决定是否解码或做颜色转换。\n");
    }
}

/*
 * mmap 让用户空间和内核共享同一块视频缓冲区，
 * 避免每帧都额外拷贝一次驱动数据。
 */
int init_mmap(AppState *app)
{
    struct v4l2_requestbuffers req;

    if (!app) {
        return -1;
    }

    memset(&req, 0, sizeof(req));
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(app->fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        return -1;
    }

    if (req.count < 2) {
        fprintf(stderr, "可用的 MMAP 缓冲区太少: %u\n", req.count);
        return -1;
    }

    app->buffers = calloc(req.count, sizeof(Buffer));
    if (!app->buffers) {
        perror("calloc buffers");
        return -1;
    }

    app->n_buffers = req.count;

    for (unsigned int i = 0; i < app->n_buffers; i++) {
        struct v4l2_buffer buf;

        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(app->fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            return -1;
        }

        app->buffers[i].length = buf.length;
        app->buffers[i].start = mmap(NULL,
                                     buf.length,
                                     PROT_READ | PROT_WRITE,
                                     MAP_SHARED,
                                     app->fd,
                                     buf.m.offset);

        if (app->buffers[i].start == MAP_FAILED) {
            perror("mmap");
            app->buffers[i].start = NULL;
            app->n_buffers = i;
            uninit_mmap(app);
            return -1;
        }

        printf("buffer[%u]: length=%zu offset=%u\n",
               i,
               app->buffers[i].length,
               buf.m.offset);
    }

    return 0;
}

int start_capturing(AppState *app)
{
    enum v4l2_buf_type type;

    if (!app) {
        return -1;
    }

    for (unsigned int i = 0; i < app->n_buffers; i++) {
        struct v4l2_buffer buf;

        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(app->fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            return -1;
        }
    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (xioctl(app->fd, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        return -1;
    }

    return 0;
}

void stop_capturing(AppState *app)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (!app) {
        return;
    }

    if (app->fd >= 0) {
        if (xioctl(app->fd, VIDIOC_STREAMOFF, &type) < 0) {
            perror("VIDIOC_STREAMOFF");
        }
    }
}

void uninit_mmap(AppState *app)
{
    if (!app || !app->buffers) {
        return;
    }

    for (unsigned int i = 0; i < app->n_buffers; i++) {
        if (app->buffers[i].start && app->buffers[i].start != MAP_FAILED) {
            munmap(app->buffers[i].start, app->buffers[i].length);
        }
    }

    free(app->buffers);
    app->buffers = NULL;
    app->n_buffers = 0;
}

static int save_raw_frame(const char *filename, const void *data, size_t length)
{
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("fopen");
        return -1;
    }

    if (fwrite(data, 1, length, fp) != length) {
        perror("fwrite");
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

int capture_one_frame(AppState *app, const char *output_path)
{
    fd_set fds;
    struct timeval tv;
    int ret;

    if (!app || !output_path) {
        return -1;
    }

    while (1) {
        FD_ZERO(&fds);
        FD_SET(app->fd, &fds);

        tv.tv_sec = 2;
        tv.tv_usec = 0;

        ret = select(app->fd + 1, &fds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            return -1;
        }

        if (ret == 0) {
            fprintf(stderr, "select timeout\n");
            return -1;
        }

        {
            struct v4l2_buffer buf;

            memset(&buf, 0, sizeof(buf));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;

            if (xioctl(app->fd, VIDIOC_DQBUF, &buf) < 0) {
                if (errno == EAGAIN) {
                    continue;
                }
                perror("VIDIOC_DQBUF");
                return -1;
            }

            printf("成功取回一帧: index=%u, bytesused=%u, sequence=%u\n",
                   buf.index,
                   buf.bytesused,
                   buf.sequence);

            if (save_raw_frame(output_path,
                               app->buffers[buf.index].start,
                               buf.bytesused) < 0) {
                if (xioctl(app->fd, VIDIOC_QBUF, &buf) < 0) {
                    perror("VIDIOC_QBUF");
                }
                return -1;
            }

            if (xioctl(app->fd, VIDIOC_QBUF, &buf) < 0) {
                perror("VIDIOC_QBUF");
                return -1;
            }

            break;
        }
    }

    return 0;
}

static inline unsigned char clip_int(int v)
{
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return (unsigned char)v;
}

static void yuyv_to_rgb24(const unsigned char *src,
                          unsigned char *dst,
                          int width,
                          int height)
{
    int x;
    int y;

    for (y = 0; y < height; y++) {
        const unsigned char *line = src + y * width * 2;
        unsigned char *out = dst + y * width * 3;

        for (x = 0; x < width; x += 2) {
            int y0 = line[0];
            int u = line[1];
            int y1 = line[2];
            int v = line[3];

            int c0 = y0 - 16;
            int c1 = y1 - 16;
            int d = u - 128;
            int e = v - 128;

            int r0 = (298 * c0 + 409 * e + 128) >> 8;
            int g0 = (298 * c0 - 100 * d - 208 * e + 128) >> 8;
            int b0 = (298 * c0 + 516 * d + 128) >> 8;

            int r1 = (298 * c1 + 409 * e + 128) >> 8;
            int g1 = (298 * c1 - 100 * d - 208 * e + 128) >> 8;
            int b1 = (298 * c1 + 516 * d + 128) >> 8;

            out[0] = clip_int(r0);
            out[1] = clip_int(g0);
            out[2] = clip_int(b0);

            out[3] = clip_int(r1);
            out[4] = clip_int(g1);
            out[5] = clip_int(b1);

            line += 4;
            out += 6;
        }
    }
}

static int save_ppm(const char *filename,
                    const unsigned char *rgb,
                    int width,
                    int height)
{
    FILE *fp = fopen(filename, "wb");

    if (!fp) {
        perror("fopen");
        return -1;
    }

    fprintf(fp, "P6\n%d %d\n255\n", width, height);

    for (int y = 0; y < height; y++) {
        if (fwrite(rgb + y * width * 3, 1, width * 3, fp) !=
            (size_t)(width * 3)) {
            perror("fwrite");
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);
    return 0;
}

int capture_one_frame_as_ppm(AppState *app, const char *output_path)
{
    fd_set fds;
    struct timeval tv;
    int ret;

    if (!app || !output_path) {
        return -1;
    }

    while (1) {
        FD_ZERO(&fds);
        FD_SET(app->fd, &fds);

        tv.tv_sec = 2;
        tv.tv_usec = 0;

        ret = select(app->fd + 1, &fds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            return -1;
        }

        if (ret == 0) {
            fprintf(stderr, "select timeout\n");
            return -1;
        }

        {
            struct v4l2_buffer buf;
            unsigned char *rgb = NULL;
            int save_ret = 0;

            memset(&buf, 0, sizeof(buf));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;

            if (xioctl(app->fd, VIDIOC_DQBUF, &buf) < 0) {
                if (errno == EAGAIN) {
                    continue;
                }
                perror("VIDIOC_DQBUF");
                return -1;
            }

            printf("成功取回一帧: index=%u, bytesused=%u, sequence=%u\n",
                   buf.index,
                   buf.bytesused,
                   buf.sequence);

            rgb = malloc((size_t)app->width * app->height * 3);
            if (!rgb) {
                perror("malloc rgb");
                if (xioctl(app->fd, VIDIOC_QBUF, &buf) < 0) {
                    perror("VIDIOC_QBUF");
                }
                return -1;
            }

            yuyv_to_rgb24((const unsigned char *)app->buffers[buf.index].start,
                          rgb,
                          app->width,
                          app->height);

            save_ret = save_ppm(output_path, rgb, app->width, app->height);
            free(rgb);

            if (xioctl(app->fd, VIDIOC_QBUF, &buf) < 0) {
                perror("VIDIOC_QBUF");
                return -1;
            }

            if (save_ret < 0) {
                return -1;
            }

            break;
        }
    }

    return 0;
}

int init_shared_frame(AppState *app)
{
    if (!app) {
        return -1;
    }

    app->latest.width = app->width;
    app->latest.height = app->height;
    app->latest.bytes = (size_t)app->width * app->height * 3;

    app->latest.rgb = malloc(app->latest.bytes);
    if (!app->latest.rgb) {
        perror("malloc latest.rgb");
        return -1;
    }

    memset(app->latest.rgb, 0, app->latest.bytes);

    app->latest.frame_id = 0;
    app->latest.meta.sequence = 0;
    app->latest.meta.bytesused = 0;
    app->latest.meta.capture_time_us = 0;
    app->latest.meta.device_time_us = 0;

    app->latest.mutex = SDL_CreateMutex();
    if (!app->latest.mutex) {
        fprintf(stderr, "SDL_CreateMutex failed: %s\n", SDL_GetError());
        free(app->latest.rgb);
        app->latest.rgb = NULL;
        app->latest.bytes = 0;
        return -1;
    }

    return 0;
}

static uint64_t timeval_to_us(const struct timeval *tv)
{
    return (uint64_t)tv->tv_sec * 1000000ULL +
           (uint64_t)tv->tv_usec;
}

static int capture_thread(void *userdata)
{
    AppState *app = (AppState *)userdata;
    uint32_t last_sequence = 0;
    int have_last_sequence = 0;

    while (!app->quit) {
        fd_set fds;
        struct timeval tv;
        int ret;

        FD_ZERO(&fds);
        FD_SET(app->fd, &fds);

        tv.tv_sec = 0;
        tv.tv_usec = 200000;

        ret = select(app->fd + 1, &fds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            return -1;
        }

        if (ret == 0) {
            continue;
        }

        {
            struct v4l2_buffer buf;

            memset(&buf, 0, sizeof(buf));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;

            if (xioctl(app->fd, VIDIOC_DQBUF, &buf) < 0) {
                if (errno == EAGAIN) {
                    continue;
                }
                perror("VIDIOC_DQBUF");
                break;
            }

            if (!app->paused) {
                CaptureMeta meta;
                uint64_t frame_id;
                const unsigned char *src_yuyv =
                    (const unsigned char *)app->buffers[buf.index].start;
                size_t copy_bytes = (size_t)buf.bytesused;
                size_t expected_min_bytes =
                    (size_t)app->bytesperline * (size_t)app->height;

                if (app->bytesperline == 0) {
                    fprintf(stderr, "invalid bytesperline\n");
                    goto requeue_buffer;
                }

                if (copy_bytes == 0) {
                    fprintf(stderr, "warning: captured empty frame\n");
                    goto requeue_buffer;
                }

                if (copy_bytes > app->capture_yuyv_bytes) {
                    fprintf(stderr, "captured bytes exceed capture_yuyv buffer\n");
                    goto requeue_buffer;
                }

                if (copy_bytes < expected_min_bytes) {
                    fprintf(stderr,
                            "warning: bytesused smaller than bytesperline * height: %zu < %zu\n",
                            copy_bytes,
                            expected_min_bytes);
                }

                app->frames_captured++;

                if (have_last_sequence) {
                    if (buf.sequence > last_sequence + 1) {
                        app->frames_dropped +=
                            (uint64_t)(buf.sequence - last_sequence - 1);
                    }
                }

                last_sequence = buf.sequence;
                have_last_sequence = 1;

                memcpy(app->capture_yuyv, src_yuyv, copy_bytes);

                yuyv_to_rgb24(app->capture_yuyv,
                              app->preview_rgb,
                              app->width,
                              app->height);

                if (app_is_paused(app)) {
                    if (app->clock_mutex) {
                        SDL_LockMutex(app->clock_mutex);
                        app->paused_video_frames_discarded++;
                        SDL_UnlockMutex(app->clock_mutex);
                    }

                    if (xioctl(app->fd, VIDIOC_QBUF, &buf) < 0) {
                        perror("VIDIOC_QBUF");
                        break;
                    }

                    continue;
                }

                meta.sequence = buf.sequence;
                meta.bytesused = buf.bytesused;
                meta.device_time_us = timeval_to_us(&buf.timestamp);
                meta.capture_time_us = app_media_clock_us(app);

                SDL_LockMutex(app->latest.mutex);
                memcpy(app->latest.rgb, app->preview_rgb, app->preview_rgb_bytes);
                app->latest.frame_id++;
                app->latest.meta = meta;
                frame_id = app->latest.frame_id;
                SDL_UnlockMutex(app->latest.mutex);

                if (app->stream.enabled &&
                    app->stream_on &&
                    app->stream.accepting_frames &&
                    !app->stream.fatal_error) {
                    if (frame_queue_push(&app->stream.queue,
                                         app->capture_yuyv,
                                         copy_bytes,
                                         app->width,
                                         app->height,
                                         (int)app->bytesperline,
                                         app->pixfmt,
                                         frame_id,
                                         &meta) < 0) {
                        fprintf(stderr, "frame_queue_push(stream) failed\n");
                    }
                }

                if (app->record.enabled &&
                    app->record_on &&
                    app->record.accepting_frames &&
                    !app->record.fatal_error) {
                    if (frame_queue_push(&app->record.queue,
                                         app->capture_yuyv,
                                         copy_bytes,
                                         app->width,
                                         app->height,
                                         (int)app->bytesperline,
                                         app->pixfmt,
                                         frame_id,
                                         &meta) < 0) {
                        fprintf(stderr, "frame_queue_push(record) failed\n");
                    }
                }
            }

        requeue_buffer:
            if (xioctl(app->fd, VIDIOC_QBUF, &buf) < 0) {
                perror("VIDIOC_QBUF");
                break;
            }
        }
    }

    return 0;
}

int capture_start_thread(AppState *app)
{
    if (!app) {
        return -1;
    }

    app->capture_tid = SDL_CreateThread(capture_thread,
                                        "capture_thread",
                                        app);
    if (!app->capture_tid) {
        fprintf(stderr, "SDL_CreateThread failed: %s\n", SDL_GetError());
        return -1;
    }

    return 0;
}

static const char *ctrl_type_name(__u32 type)
{
    switch (type) {
    case V4L2_CTRL_TYPE_INTEGER:
        return "INTEGER";
    case V4L2_CTRL_TYPE_BOOLEAN:
        return "BOOLEAN";
    case V4L2_CTRL_TYPE_MENU:
        return "MENU";
    case V4L2_CTRL_TYPE_BUTTON:
        return "BUTTON";
    case V4L2_CTRL_TYPE_INTEGER64:
        return "INTEGER64";
    case V4L2_CTRL_TYPE_CTRL_CLASS:
        return "CTRL_CLASS";
    case V4L2_CTRL_TYPE_STRING:
        return "STRING";
    case V4L2_CTRL_TYPE_BITMASK:
        return "BITMASK";
    case V4L2_CTRL_TYPE_INTEGER_MENU:
        return "INTEGER_MENU";
    default:
        return "OTHER";
    }
}

int enum_controls(AppState *app)
{
    struct v4l2_queryctrl qctrl;
    int printed_header = 0;

    if (!app) {
        return -1;
    }

    memset(&qctrl, 0, sizeof(qctrl));
    qctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL;

    app->control_count = 0;

    for (;;) {
        if (xioctl(app->fd, VIDIOC_QUERYCTRL, &qctrl) < 0) {
            if (errno == EINVAL) {
                break;
            }

            /*
             * 某些设备在后续 control 上可能返回 EIO/EPIPE 等异常。
             * 不把整次启动判死，保留已经成功枚举的 control。
             */
            perror("VIDIOC_QUERYCTRL");
            fprintf(stderr,
                    "[WARN] stop control enumeration early, keep %d valid controls\n",
                    app->control_count);
            break;
        }

        if ((qctrl.flags & V4L2_CTRL_FLAG_DISABLED) ||
            qctrl.type == V4L2_CTRL_TYPE_CTRL_CLASS) {
            qctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
            continue;
        }

        if (!printed_header) {
            printf("设备支持的 V4L2 controls:\n");
            printed_header = 1;
        }

        printf("id=0x%08x name=%s type=%s min=%d max=%d step=%d default=%d flags=0x%x\n",
               qctrl.id,
               qctrl.name,
               ctrl_type_name(qctrl.type),
               qctrl.minimum,
               qctrl.maximum,
               qctrl.step,
               qctrl.default_value,
               qctrl.flags);

        if (qctrl.type == V4L2_CTRL_TYPE_MENU) {
            enum_control_menu(app, &qctrl);
        }

        if (app->control_count < MAX_CONTROLS) {
            CameraControl *c = &app->controls[app->control_count++];

            c->id = qctrl.id;
            snprintf(c->name, sizeof(c->name), "%s", (char *)qctrl.name);
            c->type = qctrl.type;
            c->min = qctrl.minimum;
            c->max = qctrl.maximum;
            c->step = qctrl.step;
            c->def = qctrl.default_value;
        }

        qctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
    }

    return 0;
}

int get_control_value(AppState *app, uint32_t id, int32_t *value)
{
    struct v4l2_control ctrl;

    if (!app || !value) {
        return -1;
    }

    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = id;

    if (xioctl(app->fd, VIDIOC_G_CTRL, &ctrl) < 0) {
        perror("VIDIOC_G_CTRL");
        return -1;
    }

    *value = ctrl.value;
    return 0;
}

int set_control_value(AppState *app, uint32_t id, int32_t value)
{
    struct v4l2_control ctrl;

    if (!app) {
        return -1;
    }

    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = id;
    ctrl.value = value;

    if (xioctl(app->fd, VIDIOC_S_CTRL, &ctrl) < 0) {
        perror("VIDIOC_S_CTRL");
        return -1;
    }

    return 0;
}

int query_control_info(AppState *app, uint32_t id, struct v4l2_queryctrl *out)
{
    if (!app || !out) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->id = id;

    if (xioctl(app->fd, VIDIOC_QUERYCTRL, out) < 0) {
        perror("VIDIOC_QUERYCTRL");
        return -1;
    }

    if (out->flags & V4L2_CTRL_FLAG_DISABLED) {
        return -1;
    }

    return 0;
}

int enum_control_menu(AppState *app, struct v4l2_queryctrl *qctrl)
{
    struct v4l2_querymenu qmenu;

    if (!app || !qctrl) {
        return -1;
    }

    memset(&qmenu, 0, sizeof(qmenu));
    qmenu.id = qctrl->id;

    printf("  MENU options:\n");

    for (qmenu.index = qctrl->minimum;
         qmenu.index <= (unsigned int)qctrl->maximum;
         qmenu.index++) {
        if (xioctl(app->fd, VIDIOC_QUERYMENU, &qmenu) == 0) {
            printf("  %d: %s\n", qmenu.index, qmenu.name);
        }
    }

    return 0;
}

int scan_controls(AppState *app)
{
    /*
     * 为了兼容现有调用点，保留 scan_controls() 接口，
     * 但实际只复用 enum_controls()，避免维护两套几乎一样的逻辑。
     */
    return enum_controls(app);
}

void print_controls(AppState *app)
{
    if (!app) {
        return;
    }

    printf("\n=== Camera Controls ===\n");

    for (int i = 0; i < app->control_count; i++) {
        CameraControl *c = &app->controls[i];

        printf("[%d] %s (%d ~ %d)\n",
               i,
               c->name,
               c->min,
               c->max);
    }

    printf("=======================\n");
}

int get_control_by_index(AppState *app, int index, int *value)
{
    if (!app || !value) {
        return -1;
    }

    if (index < 0 || index >= app->control_count) {
        return -1;
    }

    return get_control_value(app, app->controls[index].id, value);
}

int set_control_by_index(AppState *app, int index, int value)
{
    if (!app) {
        return -1;
    }

    if (index < 0 || index >= app->control_count) {
        return -1;
    }

    return set_control_value(app, app->controls[index].id, value);
}

int alloc_preview_rgb_buffer(AppState *app)
{
    if (!app) {
        return -1;
    }

    app->preview_rgb_bytes = (size_t)app->width * app->height * 3;
    app->preview_rgb = (unsigned char *)malloc(app->preview_rgb_bytes);
    if (!app->preview_rgb) {
        perror("malloc preview_rgb");
        return -1;
    }

    memset(app->preview_rgb, 0, app->preview_rgb_bytes);
    return 0;
}

int alloc_capture_yuyv_buffer(AppState *app)
{
    if (!app) {
        return -1;
    }

    app->capture_yuyv_bytes = app->sizeimage;
    if (app->capture_yuyv_bytes == 0) {
        fprintf(stderr, "invalid capture_yuyv_bytes\n");
        return -1;
    }

    app->capture_yuyv = (unsigned char *)malloc(app->capture_yuyv_bytes);
    if (!app->capture_yuyv) {
        perror("malloc capture_yuyv");
        return -1;
    }

    memset(app->capture_yuyv, 0, app->capture_yuyv_bytes);
    return 0;
}