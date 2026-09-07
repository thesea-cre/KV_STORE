# CC=gcc
# SRCS=protocol/kvstore.c \
# 	 engine/kv_array.c \
# 	 network/reactor.c \
# 	 network/nytco/rbtree.c \
# 	 engine/kv_hash.c \
# 	 engine/kv_rbtree.c \
# 	 mempool/mempool.c \
# 	 protocol/kvproto.c \
# 	 log/log.c \
#  	 network/uring_tcp_server.c \
#  	 network/nytco.c \
#  	 network/nytco/coroutine.c \
#   	 network/nytco/ctx_switch.S \
	AOF/aof.c \
# 	 Main/main.c
# TARGET=main.exe
# CFLAGS= -I/home/snow/share/KV_STORE/lib/liburing/src/include -I/home/snow/share/KV_STORE/network/nytco \
# 	-D_GNU_SOURCE \
# 	-mcmodel=large \
# 	-I/home/snow/share/KV_STORE/include \
# 	-g
# LDFLAGS= -L/home/snow/share/KV_STORE/lib/liburing/src -luring -ldl -lpthread
# TESTCASE_SRC=testcase.c 
# TESTCASE=testcase
# OBJS := $(SRCS:.c=.o)
# OBJS := $(OBJS:.S=.o)
# all: $(TARGET) $(TESTCASE)

# $(TARGET):$(OBJS)
# 	$(CC) -o $@ $^ $(LDFLAGS)

# $(TESTCASE):$(TESTCASE_SRC)
# 	$(CC) -o $@ $^ $(LDFLAGS)

# %.o:%.c
# 	$(CC) $(CFLAGS) -c $^ -o $@
# %.o: %.S
# 	$(CC) $(CFLAGS) -c $< -o $@
# # nytco:
# # 	$(MAKE) -C network/nytco
# clean:
# 	rm  -rf $(OBJS) $(TARGET) $(TESTCASE) 
# 	$(MAKE) -C network/nytco clean
# TESTNETREACTOR_SRC=network/reactor.c test_net_work_reactor.c network/uring_tcp_server.c
# test_net: $(TESTNETREACTOR_SRC)
# 	$(CC) -o $@ $(TESTNETREACTOR_SRC) $(CFLAGS) $(LDFLAGS)
# 编译器与编译选项
CC     := gcc
CFLAGS := -I/home/snow/share/KV_STORE/lib/liburing/src/include \
          -I/home/snow/share/KV_STORE/network/nytco \
          -D_GNU_SOURCE \
          -mcmodel=large \
          -I/home/snow/share/KV_STORE/include \
          -g
LDFLAGS  := -L/home/snow/share/KV_STORE/lib/liburing/src -luring -ldl -lpthread

OBJ_DIR := obj

COMMON_SRCS := \
    protocol/kvproto.c \
    protocol/kvstore.c \
    engine/kv_array.c \
    engine/kv_hash.c \
    engine/kv_rbtree.c \
    network/reactor.c \
    network/nytco/rbtree.c \
    network/nytco/coroutine.c \
    network/nytco/ctx_switch.S \
    network/uring_tcp_server.c \
    network/nytco.c \
    mempool/mempool.c \
    log/log.c \
    AOF/aof.c

# 后缀替换 + 路径处理
TEMP_OBJS := $(COMMON_SRCS:.c=.o)
TEMP_OBJS := $(TEMP_OBJS:.S=.o)
COMMON_OBJS := $(addprefix $(OBJ_DIR)/, $(notdir $(TEMP_OBJS)))

# 源文件搜索路径（模式规则的核心配套）
VPATH := protocol:engine:network/nytco:network:mempool:log:AOF

all: $(OBJ_DIR) $(COMMON_OBJS)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# 修正：模式规则依赖用 %.c / %.S，编译用 $< 取单个源文件
$(OBJ_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: clean
clean:
	rm -rf $(OBJ_DIR)





