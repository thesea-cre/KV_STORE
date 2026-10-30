#ifndef _KVSTORE_H_
#define _KVSTORE_H_
#define ENABLE_REACTOR 0
#define ENABLE_PROACTOR 1
#define ENABLE_NTYCO 2
#define NETWORK_SELECT ENABLE_REACTOR//控制启用哪一个网络框架
#define ENGINE_ARRAY 0
#define ENGINE_HASH 1
#define ENGINE_RBTREE 2
#define ENGINE ENGINE_HASH//控制启用哪一种引擎
#define KV_COMPLEX 1
#ifndef INFO
#define INFO  
#endif  
#define ENABLEKV 1
#define TEST100w 0
#define MAX_PORT 20
#define MAX_CONNECT 1024
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
#define KV_MAX_TOKENS 8

int kv_protocol(char *msg,int len,result*c);
int kv_incremental(char *rbuffer,int rlength);

//int kv_slave_incremental(char *msg,int rlength);

int kv_response(char *msg,int length,result*c);

int kv_cmd_set(kv_data_t req,result*c);
int kv_cmd_get(kv_data_t req,result*c);
int kv_cmd_mod(kv_data_t req,result*c);
int kv_cmd_del(kv_data_t req,result*c);
int kv_cmd_exist(kv_data_t req,result*c);
int kv_response_expectation(kv_data_t req,result*c,int cmd);
int kv_cmd_execute(kv_data_t *req,result *c,int cmd);


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
// int kv_split_token(char *msg,char* tokens[]);
// int kv_master_protocol(char*msg,int len,result*c);
// int kv_slave_protocol(char*msg,int len,result*c);
// int kv_filter_protocol(char **token,int count,char *response);
#endif