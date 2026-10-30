# mempool.c 单元测试（只链 mempool.c）
# AddressSanitizer 默认开启（double free/越界/泄漏全靠它）；要关闭去掉两行 -fsanitize=address
CC       := gcc
CFLAGS   := -Wall -Wextra -g -O0 \
            -I../include \
            -D_GNU_SOURCE \
            -fsanitize=address
LDFLAGS  := -fsanitize=address -lpthread

SRCS     := mempool.c test_mempool.c
OBJS     := $(SRCS:.c=.o)
TARGET   := test_mempool

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: run clean
run: $(TARGET)
	ASAN_OPTIONS=detect_leaks=1 ./$(TARGET)

clean:
	rm -rf $(OBJS) $(TARGET)
