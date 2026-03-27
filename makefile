CC              := gcc
TARGET          := v4l2_camera_preview

PKG_CONFIG      ?= pkg-config
PKGS            := sdl2 alsa libavcodec libavformat libavutil libswscale libswresample

SRCS := \
	main.c \
	app/app_apply.c \
	app/app_clock.c \
	app/app_config.c \
	app/app_ctrl.c \
	app/app_startup.c \
	audio/alsa_capture.c \
	device/v4l2_core.c \
	media/audio_queue.c \
	media/frame_queue.c \
	pipeline/record.c \
	pipeline/stream.c \
	ui/display.c \
	utils/path_utils.c \
	utils/time_utils.c

OBJS := $(SRCS:.c=.o)
DEPS := $(OBJS:.o=.d)

INCLUDES := \
	-I. \
	-Iapp \
	-Iaudio \
	-Idevice \
	-Imedia \
	-Ipipeline \
	-Iui \
	-Iutils

WARNINGS := \
	-Wall \
	-Wextra \
	-Wshadow \
	-Wformat=2 \
	-Wundef \
	-Wstrict-prototypes \
	-Wmissing-prototypes \
	-Wno-unused-parameter

CSTD    := -std=c11
OPT     := -O2 -g
DEPFLAGS:= -MMD -MP

CFLAGS  := $(CSTD) $(OPT) $(WARNINGS) $(DEPFLAGS) $(INCLUDES)
PKG_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PKGS))
LDLIBS     := $(shell $(PKG_CONFIG) --libs $(PKGS))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)
	rm -rf recordings snapshots

.PHONY: all run clean

-include $(DEPS)