#ifndef _REACTOR_H_
#define _REACTOR_H_
#include"server.h"
#define LISTENFD 1
#define LISTEN_CLIENT LISTENFD
#define LISTEN_SLAVE 4
#define CLIENTFD 0
#define SERVER_MASTER 2
#define SERVER_SLAVE 3
#define CONNECTION_SIZE MAX_CONNECT
#define EVENT_SIZE 1024
#define BUFFER_SIZE 1024
#define SLAVE_PORT 8000
typedef int (*ECALLBACK)(int fd,void *user_arg);
struct conn{
    int fd;//status=0,初态。status=1，中间态。status=2,终态。
    int status;//控制状态机发送不同部分，网络层只给三种状态，初态，中间态，终态。
    //具体每个状态发什么，网络层不管，上层自己决定。什么时候转换也是自己决定
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