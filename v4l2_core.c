#include<stdio.h>
#include<string.h>
#include<errno.h>
#include<sys/ioctl.h>
#include<unistd.h>
#include<fcntl.h>
#include<sys/mman.h>
#include"app.h"
#include"v4l2_core.h"

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

    printf("协商后的格式：\n");
    printf("size:%d x %d\n",app->width,app->height);
    printf("pixfmt:");
    print_fourcc(app->pixfmt);
    printf("\n");
    printf("bytesline:%u\n",fmt.fmt.pix.bytesperline);
    printf("sizeimage:%u\n",fmt.fmt.pix.sizeimage);

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

        if(app->buffers[i].start == MAP_FAILED){
            perror("mmap");
            return -1;
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

        tv.tv_sec = 2;// 超时时间：2秒
        tv.tv_usec = 0;


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

    memset(app->latest.rgb,0,sizeof(app->latest.bytes));

    app->latest.mutex = SDL_CreateMutex();
    if(!app->latest.mutex){
        fprintf(stderr, "SDL_CreateMutex failed:%s\n",SDL_GetError());
        return -1;
    }

    return 0;
}

static int capture_thread(void *userdate){
    AppState *app = (AppState *)userdate;

    while(!app->quit){
        fd_set fds;
        struct timeval tv;
        int ret;

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
                SDL_LockMutex(app->latest.mutex);

                yuyv_to_rgb24(
                    (const unsigned char *)app->buffers[buf.index].start,
                    app->latest.rgb,
                    app->width,
                    app->height);
                app->latest.frame_id++;

                SDL_UnlockMutex(app->latest.mutex);
            }

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

void cleanup(AppState *app){
    app->quit = 1;

    if(app->capture_tid){
        SDL_WaitThread(app->capture_tid,NULL);
        app->capture_tid = NULL;
    }

    stop_capturing(app);
    uninit_mmap(app);

    if(app->latest.mutex){
        SDL_DestroyMutex(app->latest.mutex);
        app->latest.mutex = NULL;
    }

    free(app->latest.rgb);
    app->latest.rgb = NULL;

    if(app->fd > 0){
        close(app->fd);
        app->fd = -1;
    }
}