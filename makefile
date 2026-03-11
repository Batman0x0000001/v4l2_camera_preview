CC := gcc

TARGET := v4l2_camera_preview

SRCS := main.c \
        v4l2_core.c \
		display.c

OBJS := $(SRCS:.c=.o)

CFLAGS := -Wall -Wextra -O2 -g
PKG_CFLAGS := $(shell pkg-config --cflags sdl2)
PKG_LIBS := $(shell pkg-config --libs sdl2)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(PKG_LIBS)

%.o: %.c app.h v4l2_core.h
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) frame.raw frame.ppm

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run