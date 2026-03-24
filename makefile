CC := gcc

TARGET := v4l2_camera_preview

SRCS := main.c \
        app/app_ctrl.c \
        app/app_config.c \
        app/app_apply.c \
        app/app_startup.c \
        media/frame_queue.c \
        media/audio_queue.c \
        device/v4l2_core.c \
        pipeline/stream.c \
        pipeline/record.c \
        audio/alsa_capture.c \
        ui/display.c \

OBJS := $(SRCS:.c=.o)

CFLAGS := -Wall -Wextra -O2 -g \
          -I. \
          -Iapp \
          -Imedia \
          -Idevice \
          -Ipipeline \
          -Iaudio \
          -Iui \
          -Iutils

PKG_CFLAGS := $(shell pkg-config --cflags sdl2 alsa libavcodec libavformat libavutil libswscale libswresample)
PKG_LIBS   := $(shell pkg-config --libs   sdl2 alsa libavcodec libavformat libavutil libswscale libswresample)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(PKG_LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) record.mp4

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run