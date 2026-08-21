CC=gcc
CFLAGS = -g -O0 -D_GNU_SOURCE
LDFLAGS = -ldl
TARGET = server
TESTCASE=test_ctx
SRCS = server.c coroutine.c rbtree.c ctx_switch.S
TESTCASE_SRC=test_ctx.c	ctx_switch.S
all: $(TARGET) $(TESTCASE)
$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS)
$(TESTCASE):$(TESTCASE_SRC)
	$(CC) -o $(TESTCASE) $(TESTCASE_SRC)

clean:
	rm -f $(TARGET) $(TESTCASE)