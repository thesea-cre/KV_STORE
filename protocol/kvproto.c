#include"kvproto.h"
#include<arpa/inet.h>
#include<string.h>
//#include"kvstore.h"
const char *command[]={
    "SET","GET","DEL","MOD","EXIST"
};
const char *response[]={
    "OK","ERROR","EXIST","NOT EXIST"
};
extern void *kv_malloc(size_t size);
extern void  kv_free(void *ptr);
int kv_header_encode(const kv_header_t* header,uint8_t* buf,size_t buf_len){
    if(buf_len<KV_HEADER_LEN) return -1;
    uint16_t magic=htons(header->magic);
    uint8_t version=header->version;
    uint8_t msg_type=header->msg_type;
    uint16_t opcode=htons(header->opcode);
    uint32_t len=htonl(header->length);
    int offset=0;
    memcpy(buf+offset,&magic,2),offset+=2;
    memcpy(buf+offset,&version,1),offset+=1;
    memcpy(buf+offset,&msg_type,1),offset+=1;
    memcpy(buf+offset,&opcode,2),offset+=2;
    memcpy(buf+offset,&len,4),offset+=4;
    return offset;
}
int kv_header_decode(const uint8_t *buf,size_t buf_len,kv_header_t * header){
    if(!buf||buf_len<KV_HEADER_LEN||header==NULL) return -1;
    int offset=0;
    uint16_t magic,opcode;
    uint32_t len;
    memcpy(&magic,buf+offset,2),offset+=2;
    header->magic=ntohs(magic);

    memcpy(&header->version,buf+offset,1),offset+=1;
    memcpy(&header->msg_type,buf+offset,1),offset+=1;
    memcpy(&opcode,buf+offset,2),offset+=2;
    header->opcode=ntohs(opcode);
    memcpy(&len,buf+offset,4),offset+=4;
    header->length=ntohl(len);
    return offset;
}
int kv_tlv_append(uint8_t *buf,size_t buf_len,TLV tlv){
    uint32_t tlv_data_len=tlv.length;
    uint16_t tlv_tag=tlv.tag;
    uint8_t tlv_type=tlv.type;
    uint8_t *tlv_data=tlv.value;

    size_t tlv_len=7+tlv_data_len;//1+2+4+tlv_len;
    if(buf_len<tlv_len) return -1;
    int offset=0;
    uint16_t Tag=htons(tlv_tag);
    size_t Value_len=htonl(tlv_data_len);
    memcpy(buf+offset,&Tag,2),offset+=2;//tag
    memcpy(buf+offset,&tlv_type,1),offset+=1;//type
    memcpy(buf+offset,&Value_len,4),offset+=4;//len
    memcpy(buf+offset,tlv_data,tlv_data_len),offset+=tlv_data_len;//value
    return (int)tlv_len;
}
int kv_tlv_decode(uint8_t *buf,size_t buf_len,TLV *tlv){
    if(buf==NULL||tlv==NULL) return -1;
    int offset=0;
    uint16_t tag_net;
    uint8_t type_net;
    uint32_t length_net;
    memcpy(&tag_net,buf+offset,2),offset+=2;
    tlv->tag=ntohs(tag_net);
    memcpy(&type_net,buf+offset,1),offset+=1;
    tlv->type=type_net;
    memcpy(&length_net,buf+offset,4),offset+=4;
    tlv->length=ntohl(length_net);
    tlv->value=buf+offset,offset+=tlv->length;
    return offset;
}
int kv_payload_decode(uint8_t* payload,size_t payload_len,kv_request_t * req){
    size_t offset=0;
    int ret;
    //memset(req,0,sizeof(kv_request_t));
    if(payload==NULL||req==NULL) return -1;
    int key_num=0;
    int value_num=0;
    //int data_num=0;
    while(offset<payload_len){//边界条件还没检查,如果tlv不完整，会越界吗
        TLV tlv;//while循环一条一条解析
        memset(&tlv,0,sizeof(TLV));
        ret=kv_tlv_decode(payload+offset,payload_len,&tlv);
        offset+=ret;
        switch(tlv.tag){
            case TAG_KEY:
                req->key[key_num]->data=(char*)kv_malloc(tlv.length);
                req->key[key_num]->type=tlv.type;
                req->key[key_num]->length=tlv.length;
                memcpy(req->key[key_num]->data,tlv.value,tlv.length);
                key_num++;
                break;
            case TAG_VALUE:
                req->value[value_num]->data=(char*)kv_malloc(tlv.length);
                req->value[value_num]->type=tlv.type;
                req->value[value_num]->length=tlv.length;
                memcpy(req->value[value_num]->data,tlv.value,tlv.length);
                value_num++;
                break;
            case TAG_RES:
                req->value[value_num]->data=(char*)kv_malloc(tlv.length);
                req->value[value_num]->type=tlv.type;
                req->value[value_num]->length=tlv.length;
                memcpy(req->value[value_num]->data,tlv.value,tlv.length);
                value_num++;
                break;
            default:
                break;
        }
        // req->data[data_num]->data=(char *)kv_malloc(tlv.length);
        // req->data[data_num]->type=tlv.type;
        // req->data[data_num]->length=tlv.length;
        // req->data[data_num]->tag=tlv.tag;
        // data_num++;
    }
    req->key_num=key_num;
    req->value_num=value_num;
    // req->data_num=data_num;
    return offset;
}
void kv_request_t_init(kv_request_t *req){
    memset(req, 0, sizeof(kv_request_t));
    req->key_num = 0;
    req->value_num = 0;
    for(int i=0;i<MAX_CMD;i++){
        req->key[i]=(DATA*)kv_malloc(sizeof(DATA));
        req->value[i]=(DATA*)kv_malloc(sizeof(DATA));
    }
}
void kv_request_t_destory(kv_request_t *req){
    for(int i=0;i<MAX_CMD;i++){
        kv_free(req->key[i]);
        kv_free(req->value[i]);
    }
}