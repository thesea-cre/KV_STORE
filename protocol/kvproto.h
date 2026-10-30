#ifndef _KVRROTO_H_
#define _KVPROTO_H_
#include<stdint.h>
#include<stddef.h>
#define HEAD_LEN 9 //固定头部长度
#define TLV_LEN_MIN 11  //1+4+1+4+1 最小tlv长度
#define PACKAGE_LEN_MIN HEAD_LEN+TLV_LEN_MIN //最小包长
#define KEY_VLAUE_NUM_MAX 10 //同构批处理最大命令数
#define DATA_LEN_MAX 100 //值最大限度
#define FRAGMENT 4*1024

#define KV_MAGIC 0x4B56
#define KV_VERSION 0x01

#define OP_SET 0x0000
#define OP_GET 0x0001
#define OP_DEL 0x0002
#define OP_MOD 0x0003
#define OP_EXIST 0x0004
#define OP_RES 0x0005
#define OP_SET_CHUNK 0x007
#define OP_OPTHER 0x0008
enum{
    KV_CMD_START,
    KV_CMD_SET=KV_CMD_START,
    KV_CMD_GET,
    KV_CMD_DEL,
    KV_CMD_MOD,
    KV_CMD_EXIST,
    KV_CMD_RES,
    KV_CMD_COUNT,
    KV_CMD_OTHER=KV_CMD_COUNT
};
const char *command[]={
    "SET","GET","DEL","MOD","EXIST","RES","SET_CHUNK","ELSE CMD"
};


#define TYPE_STRING 0x01
#define TYPE_IMGINE 0x02
#define TYPE_URL 0x03
#define TYPE_ELSE 0x04

enum{
    KV_TYPE_START,
    KV_TYPE_STRING,
    KV_TYPE_IMAGE,
    KV_TYPE_URL,
    KV_TYPE_COUNT,
    KV_TYPE_ELSE=KV_TYPE_COUNT
};
const char* type_[]={
    "start","string","image","url","else"
};





#define ERROR -2
#define OK 1
#define WAIT -1

typedef struct kv_header_s{//定义的协议头
    uint16_t magic;//
    uint8_t version;//
    uint16_t opcode;//操作码部分先就这样
    uint32_t length;//数据部分长度
}kv_header_t;//2+1+2+4=9

typedef struct kv_tlv_s{
    uint8_t type;//数据类型
    uint32_t key_length;//数据长度
    uint8_t *key;//数据部分
    uint32_t value_length;
    uint8_t *value;
}kv_tlv_t;




typedef struct kv_payload_s{
    kv_tlv_t *array;
    uint32_t num;
}kv_payload_t;

typedef struct RES_s{
    char *op;
    uint32_t num;
    uint8_t **key;
    uint8_t **value;
    uint32_t *value_len;//填充部分

    uint32_t status;
    uint8_t error_info[128];
    uint32_t cons_;
}RES;

//编码函数，上层只需保证buf_len足够大
// void KV_encode(uint8_t *buf,uint32_t buf_len,char *op,uint8_t **key,uint8_t **value,char **type,uint32_t num,uint32_t *pkt_len);
//编码是深拷贝进去的，key,value,除了op,type是浅拷贝
//所以上层也是回应的是type+value类型，然而编码函数
void KV_encode(uint8_t *buf,uint32_t buf_len,RES *res);
//解码函数，上层只需记得free
//key,value是经过深拷贝的，但是op和type_res只是浅拷贝
//解码函数将value改为了type+value直接交给引擎层存
void KV_decode(uint8_t *buf,uint32_t buf_len,RES* res);
// void KV_decode(uint8_t *buf,uint32_t buf_len,char**op,uint8_t ***key_res,uint8_t ***value_res,char ***type_res,uint32_t *num,uint32_t *cons_len);
void kv_RES_t_destory(RES *res);
void kv_RES_t_create(RES* res,char *op,char **key,char **value,char **type,int *value_len,int num);
#endif