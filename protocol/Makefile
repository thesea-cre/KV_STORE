# kvproto.c 编解码单元测试（只链 kvproto.c，不碰 kv_test.c 和引擎层）
# AddressSanitizer 默认开启（抓越界/泄漏）；要关闭就把两行 -fsanitize=address 去掉
CC       := gcc
CFLAGS   := -w -Wextra -g -O0 \
            -D_GNU_SOURCE \
			-I../mempool  \
			-I../Tool   \
            -fsanitize=address
LDFLAGS  := -fsanitize=address
SRCS     := kvproto.c test_kvproto.c ../mempool/mempool.c ../Tool/tool.c
TARGET   := test

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: run clean
run: $(TARGET)
	ASAN_OPTIONS=detect_leaks=1 ./$(TARGET)

clean:
	rm -rf  $(TARGET)
