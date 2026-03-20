#include<stdio.h>
#include<string.h>
#include<errno.h>
#include<sys/ioctl.h>
#include<unistd.h>
#include<fcntl.h>
#include<sys/mman.h>
#include"app.h"
#include"v4l2_core.h"
#include"stream.h"
#include"record.h"
#include"log.h"
#include"time_utils.h"

/*
    在 V4L2 中几乎所有操作都通过它完成
        ioctl(fd, request, arg);
            │    │        └─ 数据（传入或传出）
            │    └────────── 命令（做什么操作）
            └─────────────── 文件描述符（哪个设备）
    对 ioctl 加了一层重试保护，屏蔽信号中断的干扰，
    让上层调用者只需要关心成功或真正的错误，不用关心信号问题。
*/
static int xioctl(int fd,int request,void *arg){
    int ret;

    do{
        ret = ioctl(fd,request,arg);
    }while(ret == -1 && errno == EINTR);

    return ret;
}

void app_state_init(AppState *app,const char *device_path){
    memset(app,0,sizeof(AppState));

    snprintf(app->device_path,sizeof(app->device_path),"%s",device_path);
    app->fd = -1;
    app->width = 640;
    app->height = 480;
    app->pixfmt = V4L2_PIX_FMT_YUYV;

    app->latest.rgb = NULL;
    app->latest.width = 0;
    app->latest.height = 0;
    app->latest.bytes = 0;
    app->latest.frame_id = 0;
    app->latest.mutex = NULL;

    app->stream_on = 1;
    app->record_on = 1;

}

int open_device(AppState *app){
    app->fd = open(app->device_path,O_RDWR | O_NONBLOCK,0);
    if(app->fd < 0){
        perror("open");
        return -1;
    }

    return 0;
}

int query_capability(AppState *app){
    struct v4l2_capability cap;

    memset(&cap,0,sizeof(cap));

    if(xioctl(app->fd,VIDIOC_QUERYCAP,&cap) < 0){
        perror("VIDIOC_QUERYCAP");
        return -1;
    }
    
    printf("driver:%s\n",cap.driver);
    printf("card:%s\n",cap.card);
    printf("bus_info:%s\n",cap.bus_info);
    printf("version:%u.%u.%u\n",(cap.version >> 16) & 0xff,
        (cap.version >> 8) & 0xff,
        cap.version  & 0xff);
    printf("capabilities:0x%08x\n",cap.capabilities);

    if(!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)){
        fprintf(stderr, "设备不支持 V4L2_CAP_VIDEO_CAPTURE\n");
        return -1;
    }
    if(!(cap.capabilities & V4L2_CAP_STREAMING)){
        fprintf(stderr, "设备不支持 V4L2_CAP_VIDEO_CAPTURE\n");
        return -1;
    }

    return 0;
}

static void print_fourcc(uint32_t pixfmt){
    /*
        进制      每位表示   表示1字节需要
        二进制     1 bit        8位
        十六进制    4 bit        2位
        0x00565955 = 0000 0000 0101 0110 0101 1001 0101 0101
        & 0xff     = 0000 0000 0000 0000 0000 0000 1111 1111
    */
    printf("'%c%c%c%c'",pixfmt & 0xff,
        (pixfmt >> 8) & 0xff,
        (pixfmt >> 16) & 0xff,
        (pixfmt >> 24) & 0xff);
}

int enum_formats(AppState *app){
    struct v4l2_fmtdesc fmtdesc;

    memset(&fmtdesc,0,sizeof(struct v4l2_fmtdesc));
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    printf("设备支持的采集格式：\n");

    for (fmtdesc.index = 0; ; fmtdesc.index++)
    {
        if(xioctl(app->fd,VIDIOC_ENUM_FMT,&fmtdesc)<0){
            if(errno == EINVAL){
                break;
            }
            perror("VIDIOC_ENUM_FMT");
            return -1;
        }

        printf("[%u]",fmtdesc.index);
        print_fourcc(fmtdesc.pixelformat);
        printf("%s\n",fmtdesc.description);
    }
    
    return 0;
}

int enum_frame_sizes(AppState *app,uint32_t pixfmt){
    struct v4l2_frmsizeenum frmsize;

    memset(&frmsize,0,sizeof(struct v4l2_frmsizeenum));
    frmsize.pixel_format = pixfmt;

    printf("格式\n");
    print_fourcc(pixfmt);
    printf("支持的分辨率：\n");

    for(frmsize.index = 0;;frmsize.index++){
        if(xioctl(app->fd,VIDIOC_ENUM_FRAMESIZES,&frmsize)<0){
            if(errno == EINVAL){
                break;
            }
            perror("VIDIOC_ENUM_FRAMESIZES");
            return -1;
        }

        if(frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE){
            //离散值：设备只支持固定几种分辨率
            printf("[%u] %u x %u\n",frmsize.index,
                frmsize.discrete.width,
                frmsize.discrete.height);
        }else if (frmsize.type == V4L2_FRMSIZE_TYPE_STEPWISE)
        {
            //步进范围：在最小/最大之间，按步长递增
            printf("[%u] stepwise:%u x %u -> %u x %u, step %u x %u\n",
                frmsize.index,
                frmsize.stepwise.min_width,
                frmsize.stepwise.min_height,
                frmsize.stepwise.max_width,
                frmsize.stepwise.max_height,
                frmsize.stepwise.step_width,
                frmsize.stepwise.step_height);
        }else if (frmsize.type == V4L2_FRMSIZE_TYPE_CONTINUOUS)
        {
            //连续范围：最小到最大之间任意值
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

int enum_frame_intervals(AppState *app,uint32_t pixfmt,unsigned int width,unsigned int height){
    struct v4l2_frmivalenum frmival;

    memset(&frmival,0,sizeof(struct v4l2_frmivalenum));
    frmival.pixel_format = pixfmt;
    frmival.width = width;
    frmival.height = height;

    printf("格式\n");
    print_fourcc(pixfmt);
    printf("在%u x %u下支持的帧间隔:\n",width,height);

    for(frmival.index = 0;;frmival.index++){
        if(xioctl(app->fd,VIDIOC_ENUM_FRAMEINTERVALS,&frmival)<0){
            if(errno == EINVAL){
                break;
            }
            perror("VIDIOC_ENUM_FRAMEINTERVALS");
            return -1;
        }

        if(frmival.type == V4L2_FRMIVAL_TYPE_DISCRETE){
            printf("[%u] %u / %u秒(约%.2ffps)\n",
                frmival.index,
                frmival.discrete.numerator,//分子
                frmival.discrete.denominator,//分母
                (double)frmival.discrete.denominator/frmival.discrete.numerator);
        }else if (frmival.type == V4L2_FRMIVAL_TYPE_STEPWISE || frmival.type == V4L2_FRMIVAL_TYPE_CONTINUOUS)
        {
            printf("[%u]非离散帧间隔(stepwise/continuous)\n",frmival.index);
        }
    }

    return 0;
}

int set_format(AppState *app){
    struct v4l2_format fmt;

    memset(&fmt,0,sizeof(struct v4l2_format));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = app->width;
    fmt.fmt.pix.height = app->height;
    fmt.fmt.pix.pixelformat = app->pixfmt;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if(xioctl(app->fd,VIDIOC_S_FMT,&fmt)<0){
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

void explain_selected_format(uint32_t pixfmt){
    printf("当前选择的格式说明：\n");

    if(pixfmt == V4L2_PIX_FMT_YUYV){
        printf("YUYV: 原始打包 YUV 格式,适合理解V4L2 buffer与像素布局。\n");
        printf("    优点: 无需先解码,调试采集链路直观。\n");
        printf("    缺点: 带宽占用通常比 MJPEG 更大。\n");
    }else if (pixfmt == V4L2_PIX_FMT_MJPEG)
    {
        printf("MJPEG: 压缩格式,常见于 USB 摄像头。\n");
        printf("    优点: 带宽压力较小,高分辨率时常更容易跑高fps。\n");
        printf("    缺点: 还需要额外解码才能显示或处理。\n");
    }else{
        printf("这是其他格式,后续需要根据具体格式决定是否解码或做颜色转换。\n");
    }  
}


//mmap 让用户程序和内核共享同一块物理内存
int init_mmap(AppState *app){
    struct v4l2_requestbuffers req;

    memset(&req,0,sizeof(struct v4l2_requestbuffers));
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if(xioctl(app->fd,VIDIOC_REQBUFS,&req)<0){
        perror("VIDIOC_REQBUFS");
        return -1;
    }

    if(req.count < 2){
        fprintf(stderr, "可用的MMAP缓冲区太少: %u\n",req.count);
        return -1;
    }

    app->buffers = calloc(req.count,sizeof(Buffer));
    if(!app->buffers){
        perror("calloc buffers");
        return -1;
    }

    app->n_buffers = req.count;

    for (unsigned int i = 0; i < app->n_buffers; i++)
    {
        struct v4l2_buffer buf;

        memset(&buf,0,sizeof(struct v4l2_buffer));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        //查询：问驱动"第i个缓冲区在哪里，多大？"
        if(xioctl(app->fd,VIDIOC_QUERYBUF,&buf)<0){
            perror("VIDIOC_QUERYBUF");
            return -1;
        }

        app->buffers[i].length = buf.length;
        // 映射：把内核缓冲区映射到用户空间
        app->buffers[i].start = mmap(
            NULL,// 让系统选择映射地址
            buf.length,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            app->fd,
            buf.m.offset);

        if (app->buffers[i].start == MAP_FAILED) {
            perror("mmap");
            app->buffers[i].start = NULL;
            app->n_buffers = i;   // 记录已映射的数量
            uninit_mmap(app);     // 统一清理已映射的部分
            return -1;            // 正确清理再返回
        }

        //%zu输出size_t型
        printf("buffer[%u]:lenth=%zu offser=%u\n",
            i,
            app->buffers[i].length,
            buf.m.offset);
    }

    return 0;
}

int start_capturing(AppState *app){
    enum v4l2_buf_type type;

    for(unsigned int i = 0;i < app->n_buffers;i++){
        struct v4l2_buffer buf;

        memset(&buf,0,sizeof(struct v4l2_buffer));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        //VIDIOC_QBUF = enQueue Buffer（入队）
        if(xioctl(app->fd,VIDIOC_QBUF,&buf)<0){
            perror("VIDIOC_QBUF");
            return -1;
        }
    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    // 驱动开始让摄像头往缓冲区写数据
    if(xioctl(app->fd,VIDIOC_STREAMON,&type)<0){
        perror("VIDIOC_STREAMON");
        return -1;
    }

    return 0;
}

void stop_capturing(AppState *app){
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if(app->fd >= 0){
        if(xioctl(app->fd,VIDIOC_STREAMOFF,&type)<0){
            perror("VIDIOC_STREAMOFF");
        }
    }
}

void uninit_mmap(AppState *app){
    if(!app->buffers){
        return;
    }

    for(unsigned int i = 0;i<app->n_buffers;i++){
        if(app->buffers[i].start && app->buffers[i].start != MAP_FAILED){
            // 解除用户空间到内核空间的映射关系
            munmap(app->buffers[i].start,app->buffers[i].length);
        }
    }

    free(app->buffers);
    app->buffers = NULL;
    app->n_buffers = 0;
}   

static int save_faw_frame(const char *filename,const void *data,size_t length){
    FILE *fp = fopen(filename,"wb");
    if(!fp){
        perror("fopen");
        return -1;
    }

    if(fwrite(data,1,length,fp)!=length){
        perror("fwrite");
        fclose(fp);
        return -1;
    }

    fclose(fp);

    return 0;
}

int capture_one_frame(AppState *app,const char *output_path){
    fd_set fds;
    struct timeval tv;
    int ret;

    while (1)
    {
        FD_ZERO(&fds);// 清空监听集合
        FD_SET(app->fd,&fds);// 把摄像头fd加入监听

        tv.tv_sec = 2;//秒 超时时间：2秒
        tv.tv_usec = 0;//微秒，不足1秒的剩余部分，用微秒表示


        /*
            为什么用 select 而不是直接 DQBUF？
                不用select，直接DQBUF:
                摄像头还没准备好 → DQBUF阻塞 → 程序卡死，无法超时处理

                用select:
                2秒内有数据 → 返回 r>0 → 再去DQBUF，一定能取到
                2秒没数据   → 返回 r=0 → 报超时错误，程序可以继续处理
        */
        ret = select(app->fd + 1,&fds,NULL,NULL,&tv);
        //         │             │     │     │     └── 超时时间
        //         │             │     │     └──────── 监听异常（不监听）
        //         │             │     └────────────── 监听可写（不监听）
        //         │             └──────────────────── 监听可读
        //         └────────────────────────────────── 最大fd+1      
        if(ret < 0){
            if(errno == EINTR){// 被信号打断，重试
                continue;
            }
            perror("select");
            return -1;
        }

        if(ret == 0){
            fprintf(stderr, "select timeout\n");
            return -1;
        }

        struct v4l2_buffer buf;
        
        memset(&buf,0,sizeof(struct v4l2_buffer));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;


        // DQBUF = deQueue Buffer（出队）
        // 驱动把写满数据的缓冲区交给我们
        // buf.index     → 是哪个缓冲区（0~3）
        // buf.bytesused → 这帧实际数据大小
        // buf.sequence  → 帧序号（可检测丢帧）
        if(xioctl(app->fd,VIDIOC_DQBUF,&buf) < 0){
            if(errno == EAGAIN){
                continue;
            }
            perror("VIDIOC_DQBUF");
            return -1;
        }

        printf("成功取回一帧:index=%u,bytesused=%u,sequence=%u\n",
            buf.index,
            buf.bytesused,
            buf.sequence);
        
        if(save_faw_frame(output_path,app->buffers[buf.index].start,buf.bytesused) < 0){
            //  保存失败也要还回缓冲区，否则驱动队列会少一个
            if(xioctl(app->fd,VIDIOC_QBUF,&buf) < 0){
                perror("VIDIOC_QBUF");
            }
            return -1;
        }

        if(xioctl(app->fd,VIDIOC_QBUF,&buf) < 0){
            perror("VIDIOC_QBUF");
            return -1;
        }

        break;
    }
    return 0;
}

static inline unsigned char clip_int(int v){
    if(v < 0){
        return 0;
    }
    if(v > 255){
        return 255;
    }
    return (unsigned char)v;
}

static void yuyv_to_rgb24(const unsigned char *src,unsigned char *dst,int width,int height){
    int x,y;

    for(y = 0;y<height;y++){
        const unsigned char *line = src + y*width*2;
        unsigned char *out = dst + y*width*3;

        for(x = 0;x<width;x+=2){
            int y0 = line[0];
            int u  = line[1];
            int y1 = line[2];
            int v  = line[3];

            int c0 = y0 - 16;
            int c1 = y1 - 16;
            int d  = u - 128;
            int e  = v - 128;

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
            out  += 6;
        }
    }
}

static int save_ppm(const char *filename,const unsigned char *rgb,int width,int height){
    FILE *fp = fopen(filename,"wb");
    
    if(!fp){
        perror("fopen");
        return -1;
    }

    fprintf(fp,"P6\n%d %d\n255\n",width,height);

    for (int y = 0;y < height; y++){
        if(fwrite(rgb + y*width*3,1,width*3,fp)!=(size_t)(width*3)){
            perror("fwrite");
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);

    return 0;
}

int capture_one_frame_as_ppm(AppState *app,const char *output_path){
    fd_set fds;
    struct timeval tv;
    int ret;

    while(1){
        FD_ZERO(&fds);
        FD_SET(app->fd,&fds);

        tv.tv_sec = 2;
        tv.tv_usec = 0;

        ret = select(app->fd+1,&fds,NULL,NULL,&tv);
        if(ret < 0){
            if(errno == EINTR){
                continue;
            }
            perror("select");
            return -1;
        }

        if(ret == 0){
            fprintf(stderr, "select timeout\n");
            return -1;
        }

        {   //作用：创建一个独立的作用域
            struct v4l2_buffer buf;
            unsigned char *rgb = NULL;
            int ret = 0;

            memset(&buf,0,sizeof(struct v4l2_buffer));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;

            if(xioctl(app->fd,VIDIOC_DQBUF,&buf) < 0){
                if(errno == EAGAIN){
                    continue;
                }
                perror("VIDIOC_DQBUF");
                return -1;
            }

            printf("成功取回一帧:index=%u,bytesused=%u,sequence=%u\n",
                buf.index,
                buf.bytesused,
                buf.sequence);
            
            rgb = malloc((size_t)app->width*app->height*3);
            if(!rgb){
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
            ret = save_ppm(output_path,rgb,app->width,app->height);
            free(rgb);

            if(xioctl(app->fd,VIDIOC_QBUF,&buf) < 0){
                perror("VIDIOC_QBUF");
                return -1;
            }

            if(ret < 0){
                return -1;
            }

            break;
        }
    }
    return 0;
}

int init_shared_frame(AppState *app){
    app->latest.width = app->width;
    app->latest.height = app->height;
    app->latest.bytes = (size_t)app->width * app->height *3;

    app->latest.rgb = malloc(app->latest.bytes);
    if(!app->latest.rgb){
        perror("malloc");
        return -1;
    }

    memset(app->latest.rgb,0,app->latest.bytes);

    app->latest.frame_id = 0;
    app->latest.meta.sequence = 0;
    app->latest.meta.bytesused = 0;
    app->latest.meta.capture_time_us = 0;
    app->latest.meta.device_time_us = 0;

    app->latest.mutex = SDL_CreateMutex();
    if(!app->latest.mutex){
        fprintf(stderr, "SDL_CreateMutex failed:%s\n",SDL_GetError());
        return -1;
    }

    return 0;
}

static uint64_t timeval_to_us(const struct timeval *tv){
    return (uint64_t)tv->tv_sec * 1000000ULL + (uint64_t)tv->tv_usec;
}

static int capture_thread(void *userdate){
    AppState *app = (AppState *)userdate;

    uint32_t last_sequence = 0;
    int have_last_sequence = 0;

    while(!app->quit){
        fd_set fds;
        struct timeval tv;
        int ret;

        FD_ZERO(&fds);
        FD_SET(app->fd,&fds);

        tv.tv_sec = 0;
        tv.tv_usec = 200000;//200ms

        ret = select(app->fd+1,&fds,NULL,NULL,&tv);
        if(ret < 0){
            if(errno == EINTR){
                continue;
            }
            perror("select");
            return -1;
        }

        if(ret == 0){
            continue;
        }

        {   //作用：创建一个独立的作用域
            struct v4l2_buffer buf;

            memset(&buf,0,sizeof(struct v4l2_buffer));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;

            if(xioctl(app->fd,VIDIOC_DQBUF,&buf) < 0){
                if(errno == EAGAIN){
                    continue;
                }
                perror("VIDIOC_DQBUF");
                break;
            }

            if(!app->paused){
                CaptureMeta meta;
                uint64_t frame_id;
                const unsigned char *src_yuyv = (const unsigned char *)app->buffers[buf.index].start;
                size_t copy_bytes = (size_t)buf.bytesused;
                size_t expected_min_bytes = (size_t)app->bytesperline * (size_t)app->height;

                if(app->bytesperline == 0){
                    fprintf(stderr, "invalid bytesperline\n");
                    goto requeue_buffer;
                }
                if(copy_bytes == 0){
                    fprintf(stderr, "warning:captured empty frame\n");
                    goto requeue_buffer;
                }
                if(copy_bytes > app->capture_yuyv_bytes){
                    fprintf(stderr, "captured bytes exceed capture_yuyv buffer\n");
                    // 直接跳过这帧，不处理损坏的数据
                    goto requeue_buffer;
                }
                if(copy_bytes < expected_min_bytes){
                    fprintf(stderr, "warning:bytesused smaller than bytesperline * height :%zu < %zu\n",copy_bytes,expected_min_bytes );
                }

                app->frames_captured++;
                if(have_last_sequence){
                    if(buf.sequence > last_sequence +1){
                        app->frames_dropped += (uint64_t)(buf.sequence - last_sequence - 1);
                    }
                }
                last_sequence = buf.sequence;
                have_last_sequence = 1;

                memcpy(app->capture_yuyv,src_yuyv,copy_bytes);

                //先把设备帧转移到采集线程私有缓冲
                yuyv_to_rgb24(
                    app->capture_yuyv,
                    app->preview_rgb,
                    app->width,
                    app->height);

                meta.sequence = buf.sequence;
                meta.bytesused = buf.bytesused;
                meta.device_time_us = timeval_to_us(&buf.timestamp);
                meta.capture_time_us = app_now_monotonic_us();

                //快速更新latest,只在拷贝的时候持锁
                SDL_LockMutex(app->latest.mutex);
                memcpy(app->latest.rgb,app->preview_rgb,app->preview_rgb_bytes);
                app->latest.frame_id++;
                app->latest.meta = meta;
                frame_id = app->latest.frame_id;
                SDL_UnlockMutex(app->latest.mutex);

                if(app->stream.enabled && app->stream_on && app->stream.accepting_frames && !app->stream.fatal_error){
                    if(frame_queue_push(
                        &app->stream.queue,
                        app->capture_yuyv,
                        copy_bytes,
                        app->width,
                        app->height,
                        (int)app->bytesperline,
                        app->pixfmt,
                        frame_id,
                        &meta) < 0){
                        fprintf(stderr, "frame_queue_push (stream) failed\n");
                    }
                }

                if(app->record.enabled && app->record_on && app->record.accepting_frames && !app->record.fatal_error){
                    if(frame_queue_push(
                        &app->record.queue,
                        app->capture_yuyv,
                        copy_bytes,
                        app->width,
                        app->height,
                        (int)app->bytesperline,
                        app->pixfmt,
                        frame_id,
                        &meta) < 0){
                        fprintf(stderr, "frame_queue_push (record) failed\n");
                    }
                }
            }
        requeue_buffer:
            if(xioctl(app->fd,VIDIOC_QBUF,&buf) < 0){
                perror("VIDIOC_QBUF");
                break;
            }
        }
    }
    return 0;
}

int capture_start_thread(AppState *app){
    app->capture_tid = SDL_CreateThread(capture_thread,"capture_thread",app);
    if(!app->capture_tid){
        fprintf(stderr, "SDL_CreateThread failed:%s\n", SDL_GetError());
        return -1;
    }

    return 0;
}

static const char *ctrl_type_name(__u32 type){
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

int enum_controls(AppState *app){
    struct v4l2_queryctrl qctrl;

    memset(&qctrl,0,sizeof(struct v4l2_queryctrl));
    qctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL;

    printf("设备支持的V4L2 controls:\n");

    while (xioctl(app->fd,VIDIOC_QUERYCTRL,&qctrl) == 0)
    {
        if(!(qctrl.flags & V4L2_CTRL_FLAG_DISABLED)){
            printf("id=0x%08x name=%s type=%s min=%d max=%d step=%d default=%d flags=0x%x\n",
                   qctrl.id,
                   qctrl.name,
                   ctrl_type_name(qctrl.type),
                   qctrl.minimum,
                   qctrl.maximum,
                   qctrl.step,
                   qctrl.default_value,
                   qctrl.flags);

            if(qctrl.type == V4L2_CTRL_TYPE_MENU){
                enum_control_menu(app,&qctrl);
            }
        }
        qctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
    }

    if(errno != EINVAL){
        perror("VIDIOC_QUERYCTRL");
        return -1;
    }
    
    return 0;
}

int get_control_value(AppState *app,uint32_t id,int32_t *value){
    /*
        v4l2_queryctrl  // 描述控制项的"元信息"，用 VIDIOC_QUERYCTRL
        v4l2_control    // 读写控制项的"当前值"，用 VIDIOC_G_CTRL / VIDIOC_S_CTRL

        struct v4l2_queryctrl {
            __u32 id;            // 控制项ID
            __u32 type;          // 类型（INTEGER/BOOLEAN/MENU...）
            __u8  name[32];      // 名称 "Brightness"
            __s32 minimum;       // 最小值
            __s32 maximum;       // 最大值
            __s32 step;          // 步长
            __s32 default_value; // 默认值
            __u32 flags;         // 标志位
            __u32 reserved[2];   // 保留
        };

        struct v4l2_control {
            __u32 id;            // 控制项ID
            __s32 value;         // 当前值
        };
    */
    struct v4l2_control ctrl;

    if(!value){
        return -1;
    }

    memset(&ctrl,0,sizeof(struct v4l2_control));
    ctrl.id = id;

    if(xioctl(app->fd,VIDIOC_G_CTRL,&ctrl) < 0){
        perror("VIDIOC_G_CTRL");
        return -1;
    }

    *value = ctrl.value;

    return 0;
}

int set_control_value(AppState *app,uint32_t id,int32_t value){
    struct v4l2_control ctrl;

    memset(&ctrl,0,sizeof(struct v4l2_control));
    ctrl.id = id;
    ctrl.value = value;

    if(xioctl(app->fd,VIDIOC_S_CTRL,&ctrl) < 0){
        perror("VIDIOC_S_CTRL");
        return -1;
    }

    return 0;
}

int query_control_info(AppState *app,uint32_t id,struct v4l2_queryctrl *out){
    if(!out){
        return -1;
    }

    memset(out,0,sizeof(struct v4l2_queryctrl));
    out->id = id;

    if(xioctl(app->fd,VIDIOC_QUERYCTRL,out)){
        perror("VIDIOC_QUERYCTRL");
        return -1;
    }

    if(out->flags & V4L2_CTRL_FLAG_DISABLED){
        return -1;
    }

    return 0;
}

int enum_control_menu(AppState *app,struct v4l2_queryctrl *qctrl){
    struct v4l2_querymenu qmenu;

    memset(&qmenu,0,sizeof(struct v4l2_querymenu));

    qmenu.id = qctrl->id;

    printf("MENU options:\n");

    for(qmenu.index = qctrl->minimum;qmenu.index <= (unsigned int)qctrl->maximum;qmenu.index++){
        if(xioctl(app->fd,VIDIOC_QUERYMENU,&qmenu) == 0){
            printf("%d:%s\n",qmenu.index,qmenu.name);
        }
    }

    return 0;
}

int scan_controls(AppState *app){
    struct v4l2_queryctrl qctrl;

    memset(&qctrl,0,sizeof(struct v4l2_queryctrl));
    qctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL;// 初始值，表示"给我第一个"

    app->control_count = 0;

    while(xioctl(app->fd,VIDIOC_QUERYCTRL,&qctrl) == 0){
        if(app->control_count >= MAX_CONTROLS){
                break;
        }
        /*
            同时过滤 DISABLED 和 CTRL_CLASS
                遇到CTRL_CLASS时，return get_control_value会报错
            flags 是一个位掩码，每一位代表一种属性，可以同时拥有多个标志。
                用 & 按位与来检测某个标志是否存在
        */
        if(!(qctrl.flags & V4L2_CTRL_FLAG_DISABLED)&&
              qctrl.type != V4L2_CTRL_TYPE_CTRL_CLASS){
            //编译器会为整个结构体一次性分配所有成员的内存，包括里面的 controls 数组：
            CameraControl *c = &app->controls[app->control_count];

            c->id = qctrl.id;//控制项的唯一ID，如 V4L2_CID_BRIGHTNESS

            snprintf(c->name, sizeof(c->name), "%s", qctrl.name);
            c->type = qctrl.type;
            c->min = qctrl.minimum;
            c->max = qctrl.maximum;
            c->step = qctrl.step;
            c->def = qctrl.default_value;

            app->control_count++;

        }

        qctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;//在当前id基础上加标志，表示"给我下一个"
    }

    if(app->control_count == 0){
        return -1;
    }

    printf("发现%d个controls\n",app->control_count);

    return 0;
}

void print_controls(AppState *app){
    printf("\n=== Camera Controls ===\n");

    for(int i=0;i<app->control_count;i++)
    {
        CameraControl *c=&app->controls[i];

        printf("[%d] %s  (%d ~ %d)\n",
               i,
               c->name,
               c->min,
               c->max);
    }

    printf("=======================\n");
}

int get_control_by_index(AppState *app,int index,int *value){
    if(index >= app->control_count){
        return -1;
    }
    return get_control_value(app,app->controls[index].id,value);
}

int set_control_by_index(AppState *app,int index,int value){
    if(index >= app->control_count){
        return -1;
    }
    return set_control_value(app,app->controls[index].id,value);
}

int alloc_preview_rgb_buffer(AppState *app){
    app->preview_rgb_bytes = (size_t)app->width * app->height * 3;
    app->preview_rgb = (unsigned char *)malloc(app->preview_rgb_bytes);
    if(!app->preview_rgb){
        perror("capture_rgb malloc ");
        return -1;
    }

    return 0;
}

int alloc_capture_yuyv_buffer(AppState *app){
    app->capture_yuyv_bytes = app->sizeimage;
    if(app->capture_yuyv_bytes == 0){
        fprintf(stderr, "invalid capture_yuyv_bytes failed\n");
        return -1;
    }

    app->capture_yuyv = (unsigned char *)malloc(app->capture_yuyv_bytes);
    if(!app->capture_yuyv){
        perror("malloc");
        return -1;
    }

    return 0;
}