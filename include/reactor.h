#ifndef _REACTOR_H_
#define _REACTOR_H_
#include"server.h"
#define LISTENFD 1
#define CLIENTFD 0
#define CONNECTION_SIZE MAX_CONNECT
#define EVENT_SIZE 1024
#define BUFFER_SIZE 1024

typedef int (*ECALLBACK)(int fd,void *user_arg);
struct conn{
    int fd;
    int status;
    int is_listenfd;
    char rbuffer[BUFFER_SIZE];//应用层数据累积缓冲区，这里该考虑一下环形缓冲区，这样可以好移除已经处理的数据，实际这里用的是线性缓冲区
    int rlength;//已经累积的数据的长度
    char wbuffer[BUFFER_SIZE];
    int wlength;//
    //result res;
    ECALLBACK send_callback;
    union{
        ECALLBACK recv_callback;//读回调是业务入口
        ECALLBACK accept_callback;//写回调是网络出口
    }r_action;
};
#endif