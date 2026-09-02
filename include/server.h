#ifndef _SERVER_H//上层为网路层提供的服务接口
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
#include<stddef.h>
#include<stdint.h>
#include"kvproto.h"
typedef int (*ECALLBACK)(int fd,void *user_arg);
typedef struct {//存储上层处理包后要回应的协议tlv
    TLV *tlv_array;
    uint32_t tlv_num;
}result;//定义的一个贯穿fd生命周期的用于存储上层协议处理结果的结构体，不然上层处理完接受的包后，后续不知道怎么发包
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
int kv_request(struct conn*c);
int kv_response(struct conn*c);
#endif
