#ifndef _KVSTORE_H_
#define _KVSTORE_H_
#define ENABLE_REACTOR 0
#define ENABLE_PROACTOR 1
#define ENABLE_NTYCO 2
#define NETWORK_SELECT ENABLE_NTYCO
#define ENGINE_ARRAY 0
#define ENGINE_HASH 1
#define ENGINE_RBTREE 2
#define ENGINE ENGINE_RBTREE
#define KV_COMPLEX 1
#include<stddef.h>
#include<stdlib.h>
#include<stdint.h>
#include"server.h"
//int kv_protocol(char *msg,int length,char *request);//解析协议函数，通过调用kv_split_token解析字符，然后再调用kv_filter_protocol，根据内容填充response
int kv_split_token(char *msg,char* tokens[]);
int kv_protocol(char *msg,int len,result*c);
int kv_filter_protocol(char **token,int count,char *response);
void *kv_malloc(size_t size);
void kv_free(void* ptr);
extern int reactor_entry(unsigned int port);
extern int uring_start(unsigned int port);
extern int nytco(unsigned int port);
enum{
    KV_CMD_START,
    KV_CMD_SET=KV_CMD_START,
    KV_CMD_GET,
    KV_CMD_DEL,
    KV_CMD_MOD,
    KV_CMD_EXIST,
    
    KV_CMD_COUNT
};
enum{
    KV_RES_OK,
    KV_RES_ERROR,
    KV_RES_EXIST,
    KV_RES_NOT_EXIST,

    KV_RES_COUNT
};


#endif