# 编译器与编译选项
CC       := gcc
CFLAGS   := -Wall -Wextra -g -O0 \
            -I../include \
            -I../network/nytco \
            -D_GNU_SOURCE\
			-fno-pie \
			-mcmodel=medium
LDFLAGS  :=-no-pie -L../liburing/src -luring -ldl -lpthread 

# 公共目标文件目录：顶层主Makefile编译好的所有公共.o都在这里
COMMON_OBJ_DIR := ../obj

# 当前模块仅有的源文件：只编译自己的测试代码和功能代码
MODULE_SRCS := test.c
MODULE_OBJS := $(MODULE_SRCS:.c=.o)

# 生成的测试程序名
TARGET := test_sync

# 默认目标：编译模块代码 + 链接公共.o
all: $(TARGET)

# 链接：模块自己的.o + 公共目录所有预编译好的.o
$(TARGET): $(MODULE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(COMMON_OBJ_DIR)/*.o $(LDFLAGS)

# 编译当前目录的.c文件
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# 清理：只删除当前模块的编译产物，不碰公共的obj目录
.PHONY: clean run
clean:
	rm -f $(MODULE_OBJS) $(TARGET)

# 一键编译并运行测试
run: $(TARGET)
	./$(TARGET)
