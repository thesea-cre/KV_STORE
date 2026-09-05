#ifndef _KVSTORE_H_
#define _KVSTORE_H_
#define ENABLE_REACTOR 0
#define ENABLE_PROACTOR 1
#define ENABLE_NTYCO 2
#define NETWORK_SELECT ENABLE_NTYCO//控制启用哪一个网络框架
#define ENGINE_ARRAY 0
#define ENGINE_HASH 1
#define ENGINE_RBTREE 2
#define ENGINE ENGINE_RBTREE//控制启用哪一种引擎
#define KV_COMPLEX 1
#ifndef INFO
#define INFO 
#endif  
#define ENABLEKV 1
#define TEST100w 0
#define MAX_PORT 20
#define MAX_CONNECT 1048576
#define SIZE 1024
#define BUFFER_SIZE 1024
#define TIME_SUB_MS(tv1, tv2)  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)
#include<stddef.h>
#include<stdlib.h>
#include<stdint.h>
#include<arpa/inet.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<assert.h>
/*核心层，上层是引擎层，协议层
*/
#include"kv_array.h"
#include"kv_hash.h"
#include"kv_rbtree.h"
/*
*/
#include"kvproto.h"
/*其它功能模块
*/
#include"mempool.h"


#define KV_MAX_TOKENS 8


// extern kv_array_t global_array;
// extern KV_Map global_hash;
// extern KV_Rbtree global_rbtree;
int kv_split_token(char *msg,char* tokens[]);
int kv_protocol(char *msg,int len,result*c);
int kv_filter_protocol(char **token,int count,char *response);
int kv_response(char *msg,int length,result*c);
void *kv_malloc(size_t size);
void kv_free(void* ptr);
int kv_cmd_set(kv_request_t req,result*c);
int kv_cmd_get(kv_request_t req,result*c);
int kv_cmd_mod(kv_request_t req,result*c);
int kv_cmd_del(kv_request_t req,result*c);
int kv_cmd_exist(kv_request_t req,result*c);
extern int reactor_entry(unsigned int port);
extern int uring_start(unsigned int port);
extern int nytco(unsigned int port);
void init_kvengine(void);
void dest_kvengine(void);
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