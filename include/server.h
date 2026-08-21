#ifndef _SERVER_H
#define _SERVER_H
#define EVENT_SIZE 1024
#define BUFFER_SIZE 512
#define CONNECTION_SIZE 1048576
#define LISTENFD 1
#define CLIENTFD 0
#ifndef INFO
#define INFO 
#endif
#define ENABLEKV 1
#define TEST100w 0
#define MAX_PORT 20
#define TIME_SUB_MS(tv1, tv2)  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)
typedef int (*ECALLBACK)(int fd,void *user_arg);
struct conn{
    int fd;
    int status;
    int is_listenfd;
    char rbuffer[BUFFER_SIZE];
    int rlength;
    char wbuffer[BUFFER_SIZE];
    int wlength;
    ECALLBACK send_callback;
    union{
        ECALLBACK recv_callback;
        ECALLBACK accept_callback;
    }r_action;
};
int kv_request(struct conn*c);
int kv_response(struct conn*c);
#endif
