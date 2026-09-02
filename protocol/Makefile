CC=gcc
SRCS= ../engine/kv_array.c  \
	 ../network/nytco/rbtree.c \
	 ../engine/kv_hash.c \
	 ../engine/kv_rbtree.c \
	 kv_test.c
TARGET=kv_test
CFLAGS=  -I/home/snow/share/KV_STORE/network/nytco \
	-D_GNU_SOURCE \
	-I/home/snow/share/KV_STORE/include 
OBJS := $(SRCS:.c=.o)
$(TARGET):$(OBJS)
	$(CC) -o $@ $^ 

%.o:%.c
	$(CC) $(CFLAGS) -c $^ -o $@
clean:
	rm  -rf $(OBJS) $(TARGET) 
