CC              := gcc
CXX				:= g++
TARGET          := v4l2_camera_preview

PKG_CONFIG      ?= pkg-config
PKGS            := sdl2 alsa libavcodec libavformat libavutil libswscale libswresample

C_SRCS := \
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

CPP_SRCS := \
	webrtc/webrtc_bridge.cpp \
	webrtc/webrtc_publisher.cpp \
	# webrtc/webrtc_signaling.cpp

C_OBJS   := $(C_SRCS:.c=.o)
CPP_OBJS := $(CPP_SRCS:.cpp=.o)
OBJS     := $(C_OBJS) $(CPP_OBJS)
DEPS     := $(OBJS:.o=.d)

INCLUDES := \
	-I. \
	-Iapp \
	-Iaudio \
	-Idevice \
	-Imedia \
	-Ipipeline \
	-Iui \
	-Iutils \
	-Iwebrtc

COMMON_WARNINGS := \
	-Wall \
	-Wextra \
	-Wshadow \
	-Wformat=2 \
	-Wundef \
	-Wno-unused-parameter

C_WARNINGS := \
	-Wstrict-prototypes \
	-Wmissing-prototypes

CXX_WARNINGS := \
	# 先保持精简，后面 C++ 文件多了再按需要补充

CSTD      := -std=c11
CXXSTD    := -std=c++17
OPT       := -O2 -g
DEPFLAGS  := -MMD -MP

CFLAGS    := $(CSTD) $(OPT) $(DEPFLAGS) $(INCLUDES) \
             $(COMMON_WARNINGS) $(C_WARNINGS)

CXXFLAGS  := $(CXXSTD) $(OPT) $(DEPFLAGS) $(INCLUDES) \
             $(COMMON_WARNINGS) $(CXX_WARNINGS)

PKG_CONFIG      ?= pkg-config
PKGS            := sdl2 alsa libavcodec libavformat libavutil libswscale libswresample

PKG_CFLAGS      := $(shell $(PKG_CONFIG) --cflags $(PKGS))
PKG_LDLIBS      := $(shell $(PKG_CONFIG) --libs $(PKGS))

WEBRTC_CFLAGS   := -I/usr/local/include
WEBRTC_LDLIBS   := -L/usr/local/lib -ldatachannel

LDLIBS          := $(PKG_LDLIBS) $(WEBRTC_LDLIBS)

all: $(TARGET)

LINKER := $(if $(strip $(CPP_OBJS)),$(CXX),$(CC))

$(TARGET): $(OBJS)
	$(LINKER) $(OBJS) -o $@ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(PKG_CFLAGS) $(WEBRTC_CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(PKG_CFLAGS) $(WEBRTC_CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)
	rm -rf recordings snapshots
	rm -rf webrtc_manual

.PHONY: all run clean

-include $(DEPS)