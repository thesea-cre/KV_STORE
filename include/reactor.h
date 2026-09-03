#ifndef _REACTOR_H_
#define _REACTOR_H_
#define LISTENFD 1
#define CLIENTFD 0
#define CONNECTION_SIZE 1048576
#define EVENT_SIZE 1024
#define BUFFER_SIZE 512
#include"server.h"
typedef int (*ECALLBACK)(int fd,void *user_arg);
struct conn{
    int fd;
    int status;
    int is_listenfd;
    char rbuffer[BUFFER_SIZE];
    int rlength;
    char wbuffer[BUFFER_SIZE];
    int wlength;
    result res;
    ECALLBACK send_callback;
    union{
        ECALLBACK recv_callback;
        ECALLBACK accept_callback;
    }r_action;
};
#endif