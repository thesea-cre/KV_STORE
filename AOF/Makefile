# 编译器与编译选项
CC       := gcc
CFLAGS   := -Wall -Wextra -g -O0 -I../include -I../network/nytco
LDFLAGS  := -lpthread

# 源文件搜索路径：自动在这些目录查找 .c 和 .h 文件
VPATH := ../protocol:../engine:../include:../network/nytco

# 源文件列表（按你的实际目录结构）
SRCS := test.c \
        aof.c \
        kvproto.c \
        kvstore.c \
        kv_array.c \
		kv_hash.c \
		kv_rbtree.c \
		rbtree.c

# 目标文件：所有源文件编译后都输出到当前目录
OBJS   := $(notdir $(SRCS:.c=.o))
TARGET := test_aof

# 默认目标：编译生成可执行文件
all: $(TARGET)

# 链接生成可执行文件
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 模式规则：自动从 VPATH 中找到对应源文件编译
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# 清理编译产物
.PHONY: clean run
clean:
	rm -f $(OBJS) $(TARGET)

# 一键编译并运行测试
run: $(TARGET)
	./$(TARGET)
