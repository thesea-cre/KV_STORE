#ifndef _KVSTORE_H_
#define _KVSTORE_H_
#define ENABLE_REACTOR 0
#define ENABLE_PROACTOR 1
#define ENABLE_NTYCO 2
#define NETWORK_SELECT ENABLE_NTYCO//控制启用哪一个网络框架
#define ENGINE_ARRAY 0
#define ENGINE_HASH 1
#define ENGINE_RBTREE 2
#define ENGINE_SKIPLIST 3
#define ENGINE ENGINE_ARRAY//控制启用哪一种引擎
#define KV_COMPLEX 1
#ifndef INFO
    #define INFO
#endif  
#define AOF  1
#define SYNC  1
#define LOG 1
#define ENABLEKV 1
#define TEST100w 0
#define MAX_PORT 20
#define MAX_CONNECT 100
#define SIZE BUFFER_SIZE
#define BUFFER_SIZE 1024
#define SLAVE_PORT 8080
#define MASTER_IP "192.168.149.128"
#define TIME_SUB_MS(tv1, tv2)  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)
#include<stddef.h>
#include<stdlib.h>
#include<stdint.h>
#include<arpa/inet.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<assert.h>
#define KV_MAX_TOKENS 8  
typedef struct info{
    uint8_t **res;
    uint32_t *res_len;
    int num;
    int cons_;
    int status;
}kvstore_t;

void KV_server(char *rbuffer,int rlength,char *wbuffer,int wlength,int *cons,int *send);

extern int uring_start(char *ip,unsigned int port);
extern int reactor_entry(char *ip,unsigned int port);
extern int nytco(char *ip,unsigned int port);
#endif