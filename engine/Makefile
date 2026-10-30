# 引擎层单元测试：test_engine.c 用函数指针表统一驱动数组/哈希/红黑树三个引擎
# AddressSanitizer 默认开启（抓越界/泄漏）；要关闭就把两行 -fsanitize=address 去掉
CC       := gcc
CFLAGS   := -Wall -Wextra -g -O0 \
            -I../include \
            -I../network/nytco \
            -D_GNU_SOURCE \
            -Wno-cast-function-type \
            -fsanitize=address
LDFLAGS  := -fsanitize=address

SRCS     := test_engine.c kv_array.c kv_hash.c kv_rbtree.c ../network/nytco/rbtree.c
TARGET   := test_engine

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

# 注：KV_Map_get 对不存在的 key 会死循环（已知bug），timeout 杀掉属预期
.PHONY: run clean
run: $(TARGET)
	ASAN_OPTIONS=detect_leaks=1 timeout 30 ./$(TARGET)

clean:
	rm -f $(TARGET)
