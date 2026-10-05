#ifndef _SERVER_H//上层为网路层提供的服务接口
#define _SERVER_H
#ifndef INFO
#define INFO 
#endif
#define ENABLEKV 1
#define TEST100w 0
#define MAX_PORT 20
#define MAX_CONNECT 1024
#define SIZE 1024
#define TIME_SUB_MS(tv1, tv2)  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)
#include<stddef.h>
#include<stdint.h>
#include"kvstore.h"
int kv_request(char *rbuffer,char *wbuffer,int rlength,int *wlength);
int kv_response(char *msg,int length,result*c);
#endif
