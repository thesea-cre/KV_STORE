CC=gcc
SRCS=protocol/kvstore.c \
	 engine/kv_array.c \
	 network/reactor.c \
	 network/nytco/rbtree.c \
	 engine/kv_hash.c \
	 engine/kv_rbtree.c \
	 mempool/mempool.c \
	 protocol/kvproto.c \
	 log/log.c \
	 network/uring_tcp_server.c \
	 network/nytco.c \
	 network/nytco/coroutine.c \
 	 network/nytco/ctx_switch.S 
TARGET=kvstore
CFLAGS= -I/home/snow/share/KV_STORE/lib/liburing/src/include -I/home/snow/share/KV_STORE/network/nytco \
	-D_GNU_SOURCE \
	-mcmodel=large \
	-I/home/snow/share/KV_STORE/include \
	-g
LDFLAGS= -L/home/snow/share/KV_STORE/lib/liburing/src -luring -ldl -lpthread
TESTCASE_SRC=testcase.c 
TESTCASE=testcase
OBJS := $(SRCS:.c=.o)
OBJS := $(OBJS:.S=.o)
all: $(TARGET) $(TESTCASE)

$(TARGET):$(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(TESTCASE):$(TESTCASE_SRC)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o:%.c
	$(CC) $(CFLAGS) -c $^ -o $@
%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@
# nytco:
# 	$(MAKE) -C network/nytco
clean:
	rm  -rf $(OBJS) $(TARGET) $(TESTCASE) 
	$(MAKE) -C network/nytco clean
TESTNETREACTOR_SRC=network/reactor.c test_net_work_reactor.c network/uring_tcp_server.c
test_net: $(TESTNETREACTOR_SRC)
	$(CC) -o $@ $(TESTNETREACTOR_SRC) $(CFLAGS) $(LDFLAGS)

