#include<stdio.h>
#include<string.h>
#include<errno.h>
#include<sys/ioctl.h>
#include<unistd.h>
#include<fcntl.h>
#include"app.h"

static int xioctl(int fd,int request,void *arg){
    int ret;

    do{
        ret = ioctl(fd,request,arg);
    }while(ret == -1 && errno == EINTR);

    return ret;
}

static void app_state_init(AppState *app,const char *device_path){
    memset(app,0,sizeof(AppState));

    snprintf(app->device_path,sizeof(app->device_path),"%s",device_path);
    app->fd = -1;
    app->width = 640;
    app->height = 480;
    app->pixfmt = V4L2_PIX_FMT_YUYV;
}

static int open_device(AppState *app){
    app->fd = open(app->device_path,O_RDWR | O_NONBLOCK,0);
    if(app->fd < 0){
        perror("open");
        exit(EXIT_FAILURE);
    }

    return 0;
}

static int query_capability(AppState *app){
    struct v4l2_capability cap;

    memset(&cap,0,sizeof(cap));

    if(xioctl(app->fd,VIDIOC_QUERYCAP,&cap) < 0){
        perror("VIDIOC_QUERYCAP");
        exit(EXIT_FAILURE);
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

static void cleanup(AppState *app){
    if(app->fd >= 0){
        close(app->fd);
        app->fd = -1;
    }
}

int main(int argc, char const *argv[])
{
    AppState app;
    const char *device = "/dev/video0";

    if(argc > 1){
        device = argv[1];
    }

    app_state_init(&app,device);

    if(open_device(&app) < 0){
        cleanup(&app);
        return -1;
    }
    if(query_capability(&app) < 0){
        cleanup(&app);
        return -1;
    }

    printf("V4L2 设备打开成功，能力检查通过。\n");

    cleanup(&app);
    return 0;
}
