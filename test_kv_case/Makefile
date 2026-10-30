# QPS 压测程序构建（独立小工具：bench.c + 项目的 kvproto.c，不依赖 obj/）
CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -I../include -D_GNU_SOURCE
TARGET  := bench

all: $(TARGET)

$(TARGET): bench.c ../protocol/kvproto.c ../include/kvproto.h
	$(CC) $(CFLAGS) -o $@ bench.c ../protocol/kvproto.c -lpthread

.PHONY: clean
clean:
	rm -f $(TARGET)
