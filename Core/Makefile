CC:=gcc
SRCS:= kvstore.c test_kvstore.c ../protocol/kvproto.c ../Tool/tool.c ../mempool/mempool.c \
		../Tool/queue.c \
		upload.c \
		resp.c \
		../AL/adapt.c ../engine/kv_array.c ../engine/kv_hash.c ../engine/kv_rbtree.c \
		../network/nytco/rbtree.c
CFLAGS:= 	-I../protocol \
			-I../engine \
			-I../Tool \
			-I../AL \
			-I../network/nytco \
			-I../mempool \
			-g -w
# LDFLAGS:= 
TARGET:= test
all: $(TARGET)
$(TARGET):$(SRCS)
	$(CC) -o $@ $(SRCS) $(CFLAGS)
clean:
	rm -f $(TARGET)