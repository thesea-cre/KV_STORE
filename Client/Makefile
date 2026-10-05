# 编译器与编译选项
CC       := gcc
CFLAGS   := -Wall -Wextra -g -O0 \
            -I../include \
            -D_GNU_SOURCE
LDFLAGS  := -L../liburing/src -luring -ldl -lpthread
SRCS:=client.c ../protocol/kvproto.c
# 生成的测试程序名
TARGET := client
# 默认目标：编译模块代码 + 链接公共.o
all: $(TARGET)

# 链接：模块自己的.o + 公共目录所有预编译好的.o
$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ 

# 编译当前目录的.c文件
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# 清理：只删除当前模块的编译产物，不碰公共的obj目录
.PHONY: clean run
clean:
	rm -f $(TARGET)
# 一键编译并运行测试
run: $(TARGET)
	./$(TARGET)
