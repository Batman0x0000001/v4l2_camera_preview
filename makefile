CC := gcc
CFLAGS := -Wall -Wextra -O2 -g
TARGET := v4l2_open_test
SRC := main.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC)

clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET)

run-dev0: $(TARGET)
	./$(TARGET) /dev/video0

.PHONY: all clean run run-dev0