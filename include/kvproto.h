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


#define TYPE_STRING 0x01
#define TYPE_IMGINE 0x02
#define TYPE_URL 0x03


#define MAX_CMD 10
#define DATA_LEN_MAX 100
#define TLV_LEN_MIN 11

#define ERROR -2
#define OK 0
typedef struct{//定义的协议头
    uint16_t magic;
    uint8_t version;
    uint8_t  msg_type;//标识协议属于回应类还是请求类
    uint16_t opcode;//操作码部分先就这样
    uint32_t length;//数据部分长度
}kv_header_t;//2+1+1+2+4=10

typedef struct tlv{
    // uint16_t tag;//标识键，值，消息
    uint8_t type;//数据类型
    uint32_t key_length;//数据长度
    uint8_t *key;//数据部分
    uint32_t value_length;
    uint8_t *value;
}TLV;//定义的协议单元，tag标识是key，还是value,还是回应res

// typedef struct{//但都是上层协议的东西，网络层是不知道这些东西的
//     uint8_t*key;
//     uint8_t*value;
//     uint8_t type;
// }kv_data_t;//解包用的结构体，主要负责解析出key,value;

typedef struct result{//存储上层处理包后要回应的协议tlv
    TLV *array;
    uint32_t num;
}result;//定义的一个贯穿fd生命周期的用于存储上层协议处理结果的结构体，不然上层处理完接受的包后，后续不知道怎么发包

extern const char *command[];//命令
extern const char *response[];//回应，目前没用到
int kv_header_encode(const kv_header_t* hdr,uint8_t* data,size_t data_len);
int kv_header_decode(const uint8_t* buf,size_t buf_len,kv_header_t *head);
int kv_tlv_append(uint8_t *buf,size_t buf_len,TLV tlv);
int kv_tlv_decode(uint8_t *buf,size_t buf_len,TLV *tlv);
int kv_payload_decode(uint8_t *payload,size_t payload_len,kv_data_t * req);
void kv_data_t_init(kv_data_t* req);
void kv_data_t_destory(kv_data_t*req);
#endif