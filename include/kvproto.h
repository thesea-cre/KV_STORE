#ifndef _KVRROTO_H_
#define _KVPROTO_H_
#include<stdint.h>
#include<stddef.h>
#define KV_MAGIC 0x4B56
#define KV_VERSION 0x01
#define KV_HEADER_LEN 10

#define MSG_TYPE_REQ 0x01
#define MSG_TYPE_RSP 0x02

#define OP_SET 0x0000
#define OP_GET 0x0001
#define OP_DEL 0x0002
#define OP_MOD 0x0003
#define OP_EXIST 0x0004
#define OP_RES 0x0005


#define TAG_KEY 0x0001
#define TAG_VALUE 0x0002
#define TAG_RES 0x0003

#define TYPE_STRING 0x01
#define TYPE_IMGINE 0x02
#define TYPE_URL 0x03

#define MAX_CMD 10
typedef struct{//定义的协议头
    uint16_t magic;
    uint8_t version;
    uint8_t  msg_type;//标识协议属于回应类还是请求类
    uint16_t opcode;//操作码部分先就这样
    uint32_t length;//数据部分长度
}kv_header_t;//2+1+1+2+4=10
typedef struct tlv{
    uint16_t tag;//标识键，值，消息
    uint8_t type;//数据类型
    uint32_t length;//数据长度
    uint8_t *value;//数据部分
}TLV;//定义的协议单元，tag标识是key，还是value,还是回应res
typedef struct{
    char *data;
    uint8_t type;
    uint32_t length;
    uint16_t tag;
}DATA;//Key,value都是一种数据类型，包括数据部分，数据类型，长度,类型
typedef struct{//但都是上层协议的东西，网络层是不知道这些东西的
    DATA *key[MAX_CMD];
    DATA *value[MAX_CMD];
    int key_num;
    int value_num;

    DATA* data[MAX_CMD];//新版感觉只定义一个data就够了，因为DATA新添了tag字段
    int data_num;
}kv_request_t;//解包用的结构体，主要负责解析出key,value;
extern const char *command[];//命令
extern const char *response[];//回应，目前没用到
int kv_header_encode(const kv_header_t* hdr,uint8_t* data,size_t data_len);
int kv_header_decode(const uint8_t* buf,size_t buf_len,kv_header_t *head);
int kv_tlv_append(uint8_t *buf,size_t buf_len,TLV tlv);
int kv_tlv_decode(uint8_t *buf,size_t buf_len,TLV *tlv);
int kv_payload_decode(uint8_t *payload,size_t payload_len,kv_request_t * req);
void kv_request_t_init(kv_request_t* req);
void kv_request_t_destory(kv_request_t*req);
#endif