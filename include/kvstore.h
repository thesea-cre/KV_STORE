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
#include<stddef.h>
#include<stdlib.h>
int kv_protocol(char *msg,int length,char *request);//解析协议函数，通过调用kv_split_token解析字符，然后再调用kv_filter_protocol，根据内容填充response
int kv_split_token(char *msg,char* tokens[]);
int kv_filter_protocol(char **token,int count,char *response);
void *kv_malloc(size_t size);
void kv_free(void* ptr);


extern int reactor_entry(unsigned int port);
extern int uring_start(unsigned int port);
extern int nytco(unsigned int port);






#endif