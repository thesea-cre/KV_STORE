#include"server.h"
#include<stdio.h>
#include<stdlib.h>
#include<arpa/inet.h>
#include<stddef.h>
#include<stdint.h>
#include<string.h>
#include"kvstore.h"
#include"kv_array.h"
#include"kv_hash.h"
#include"kv_rbtree.h"
#include<assert.h>
#define KV_MAX_TOKENS 8
kv_array_t global_array;
KV_Map global_hash;
KV_Rbtree global_rbtree;
const char *command[]={
    "SET","GET","DEL","MOD","EXIST"
};
const char *response[]={
    "OK","ERROR","EXIST","NOT EXIST"
};
void *kv_malloc(size_t size){//用的很多的系统函数最好包装一层，为了修改方便
    return malloc(size);
}
void kv_free(void* ptr){
    return free(ptr);
}
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
    while(offset<payload_len){//边界条件还没检查,如果tlv不完整，会越界吗
        TLV tlv;
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
            default:
                break;
        }
    }
    req->key_num=key_num;
    req->value_num=value_num;
    return offset;
}

/**
 * 十六进制打印二进制数据
 * @param data   数据缓冲区指针
 * @param size   数据长度（字节）
 * @param title  打印标题（可为NULL）
 */
void hex_dump(const void *data, size_t size, const char *title) {
    const uint8_t *buf = (const uint8_t *)data;
    size_t i, j;

    // 打印标题和总长度
    if (title != NULL) {
        printf("--- %s (总长度: %zu 字节) ---\n", title, size);
    }

    // 每行16字节
    for (i = 0; i < size; i += 16) {
        // 1. 打印偏移地址（4位十六进制）
        printf("%04zx: ", i);

        // 2. 打印十六进制部分
        for (j = 0; j < 16; j++) {
            if (i + j < size) {
                printf("%02X ", buf[i + j]);
            } else {
                printf("   "); // 最后一行不足16字节，补空格对齐
            }
            // 第8字节后加一个空格分隔，更易读
            if (j == 7) {
                printf(" ");
            }
        }

        printf(" |");

        // 3. 打印ASCII字符部分
        for (j = 0; j < 16; j++) {
            if (i + j >= size) break;
            uint8_t c = buf[i + j];
            // 只打印可打印ASCII字符，其他用点代替
            if (c >= 0x20 && c <= 0x7E) {
                printf("%c", c);
            } else {
                printf(".");
            }
        }

        printf("|\n");
    }
}
void kv_request_t_init(kv_request_t *req){
    for(int i=0;i<MAX_CMD;i++){
        req->key[i]=(DATA*)kv_malloc(sizeof(DATA));
        req->value[i]=(DATA*)kv_malloc(sizeof(DATA));
    }
}
void print_hex(const uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        printf("%02X ", buf[i]);
    }
    printf("\n");
}
#define PRINT_STRUCT_MEM(ptr, struct_name) \
    print_struct_hex((ptr), sizeof(*(ptr)), (struct_name))

// 底层实现函数
void print_struct_hex(const void *ptr, size_t size, const char *name) {
    const uint8_t *buf = (const uint8_t *)ptr;
    printf("===== 结构体 %s (共 %zu 字节) =====\n", name, size);
    for (size_t i = 0; i < size; i++) {
        if (i % 16 == 0) {
            if (i != 0) printf("\n");
            printf("%04zx: ", i);
        }
        printf("%02X ", buf[i]);
        if (i % 8 == 7) printf(" ");
    }
    printf("\n");
}
struct conn c;
int kv_protocol(struct conn*c){
    kv_header_t header;
    kv_request_t req;
    memset(&header,0,sizeof(kv_header_t));
    char * msg=c->rbuffer;
    int rlength=c->rlength;

    int offset=kv_header_decode(msg,rlength,&header);
    PRINT_STRUCT_MEM(&header, "kv_header_t");
    uint8_t version=header.version;
    uint8_t msg_type=header.msg_type;
    uint16_t opcode=header.opcode;
    uint32_t len=header.length;
   
    kv_request_t_init(&req);
    kv_payload_decode(msg+offset,rlength-offset,&req);
    for(int i=0;i<req.key_num;i++){
            printf("key:%s,length:%d\n",req.key[i]->data,req.key[i]->length);
            printf("value:%s,length:%d\n",req.value[i]->data,req.value[i]->length);
    }
    if(version==KV_VERSION){
        if(msg_type==MSG_TYPE_REQ){
            int flag=0;
            int cmd;
            int ret;
            for(cmd=KV_CMD_START;cmd<KV_CMD_COUNT;cmd++){
                if(strcmp(command[opcode],command[cmd])==0){
                    flag=1;
                    printf("command:%s\n",command[cmd]);
                    break;
                    }
            }
            if(flag==0) exit(1);
            c->res.tlv_num=req.key_num;
            switch(cmd){
                case KV_CMD_SET:
                    for(int i=0;i<req.key_num;i++){
                        
                        char *key=req.key[i]->data;
                        char *value=req.value[i]->data;
                        #if ENGINE==ENGINE_ARRAY
                        ret=kv_array_set(&global_array,key,value);
                        #elif ENGINE==ENGINE_HASH
                        ret=KV_Map_set(&global_hash,key,value);
                        #elif ENGINE==ENGINE_RBTREE
                        ret=KV_Rbtree_set(&global_rbtree,key,value);
                        #endif
                        c->res.tlv_array=(TLV*)kv_malloc(c->res.tlv_num*sizeof(TLV));
                        c->res.tlv_array[i].tag=TAG_RES;
                        c->res.tlv_array[i].type=TYPE_STRING;
                        char buf[BUFFER_SIZE]={0};
                        if(ret==0){
                            c->res.tlv_array[i].length=sprintf(buf,"OK\r\n");
                            c->res.tlv_array[i].value=(char *)kv_malloc(c->res.tlv_array[i].length);
                            memcpy(c->res.tlv_array[i].value,buf,c->res.tlv_array[i].length);
                        }else if(ret>0){
                            c->res.tlv_array[i].length=sprintf(buf,"EXIST\r\n");
                            c->res.tlv_array[i].value=(char *)kv_malloc(c->res.tlv_array[i].length);
                            memcpy(c->res.tlv_array[i].value,buf,c->res.tlv_array[i].length);
                        }else{
                            c->res.tlv_array[i].length=sprintf(buf,"ERROR\r\n");
                            c->res.tlv_array[i].value=(char *)kv_malloc(c->res.tlv_array[i].length);
                            memcpy(c->res.tlv_array[i].value,buf,c->res.tlv_array[i].length);
                        }
                    }
                    break;
                case KV_CMD_GET:
                    for(int i=0;i<req.key_num;i++){
                        char *key=req.key[i]->data;
                        char *value;
                        #if ENGINE==ENGINE_ARRAY
                        value=kv_array_get(&global_array,key);
                        #elif ENGINE==ENGINE_HASH
                        value=KV_Map_get(&global_hash,key);
                        #elif ENGINE==ENGINE_RBTREE
                        value=KV_Rbtree_get(&global_rbtree,key);
                        #endif
                        c->res.tlv_array=(TLV*)kv_malloc(c->res.tlv_num*sizeof(TLV));
                        c->res.tlv_array[i].tag=TAG_VALUE;
                        c->res.tlv_array[i].type=TYPE_STRING;
                        char buf[BUFFER_SIZE]={0};
                        if(value){
                            c->res.tlv_array[i].length=strlen(value)+1;
                            c->res.tlv_array[i].value=(char *)kv_malloc(c->res.tlv_array[i].length);
                            memcpy(c->res.tlv_array[i].value,value,c->res.tlv_array[i].length);
                        }else if(value==NULL){
                            c->res.tlv_array[i].length=sprintf(buf,"NOT EXIST\r\n");
                            c->res.tlv_array[i].value=(char *)kv_malloc(c->res.tlv_array[i].length);
                            memcpy(c->res.tlv_array[i].value,buf,c->res.tlv_array[i].length);
                        }
                    }
                    break;
                case KV_CMD_MOD:
                    for(int i=0;i<req.key_num;i++){
                        char *key=req.key[i]->data;
                        char *value=req.value[i]->data;
                        #if ENGINE==ENGINE_ARRAY
                        ret=kv_array_mod(&global_array,key,value);
                        #elif ENGINE==ENGINE_HASH
                        ret=KV_Map_mod(&global_hash,key,value);
                        #elif ENGINE==ENGINE_RBTREE
                        ret=KV_Rbtree_mod(&global_rbtree,key,value);
                        #endif
                        c->res.tlv_array=(TLV*)kv_malloc(c->res.tlv_num*sizeof(TLV));
                        c->res.tlv_array[i].tag=TAG_RES;
                        c->res.tlv_array[i].type=TYPE_STRING;
                        char buf[BUFFER_SIZE]={0};
                        if(ret==0){
                            c->res.tlv_array[i].length=sprintf(buf,"OK\r\n");
                            c->res.tlv_array[i].value=(char *)kv_malloc(c->res.tlv_array[i].length);
                            memcpy(c->res.tlv_array[i].value,buf,c->res.tlv_array[i].length);
                        }else if(ret>0){
                            c->res.tlv_array[i].length=sprintf(buf,"NOT EXIST\r\n");
                            c->res.tlv_array[i].value=(char *)kv_malloc(c->res.tlv_array[i].length);
                            memcpy(c->res.tlv_array[i].value,buf,c->res.tlv_array[i].length);
                        }else{
                            c->res.tlv_array[i].length=sprintf(buf,"ERROR\r\n");
                            c->res.tlv_array[i].value=(char *)kv_malloc(c->res.tlv_array[i].length);
                            memcpy(c->res.tlv_array[i].value,buf,c->res.tlv_array[i].length);
                        }
                    }
                    break;
                case KV_CMD_DEL:
                    for(int i=0;i<req.key_num;i++){
                        char *key=req.key[i]->data;
                        #if ENGINE==ENGINE_ARRAY
                        ret=kv_array_del(&global_array,key);
                        #elif ENGINE==ENGINE_HASH
                        ret=KV_Map_del(&global_hash,key);
                        #elif ENGINE==ENGINE_RBTREE
                        ret=KV_Rbtree_del(&global_rbtree,key);
                        #endif
                        c->res.tlv_array=(TLV*)kv_malloc(c->res.tlv_num*sizeof(TLV));
                        c->res.tlv_array[i].tag=TAG_RES;
                        c->res.tlv_array[i].type=TYPE_STRING;
                        char buf[BUFFER_SIZE]={0};
                        if(ret==0){
                            c->res.tlv_array[i].length=sprintf(buf,"OK\r\n");
                            c->res.tlv_array[i].value=(char *)kv_malloc(c->res.tlv_array[i].length);
                            memcpy(c->res.tlv_array[i].value,buf,c->res.tlv_array[i].length);
                        }else if(ret>0){
                            c->res.tlv_array[i].length=sprintf(buf,"NOT EXIST\r\n");
                            c->res.tlv_array[i].value=(char *)kv_malloc(c->res.tlv_array[i].length);
                            memcpy(c->res.tlv_array[i].value,buf,c->res.tlv_array[i].length);
                        }else{
                            c->res.tlv_array[i].length=sprintf(buf,"ERROR\r\n");
                            c->res.tlv_array[i].value=(char *)kv_malloc(c->res.tlv_array[i].length);
                            memcpy(c->res.tlv_array[i].value,buf,c->res.tlv_array[i].length);
                        }
                    }
                    break;
                case KV_CMD_EXIST:
                    for(int i=0;i<req.key_num;i++){
                        char *key=req.key[i]->data;
                        #if ENGINE==ENGINE_ARRAY
                        ret=kv_array_exist(&global_array,key);
                        #elif ENGINE==ENGINE_HASH
                        ret=KV_Map_exist(&global_hash,key);
                        #elif ENGINE==ENGINE_RBTREE
                        ret=KV_Rbtree_exist(&global_rbtree,key);
                        #endif
                        c->res.tlv_array=(TLV*)kv_malloc(c->res.tlv_num*sizeof(TLV));
                        c->res.tlv_array[i].tag=TAG_RES;
                        c->res.tlv_array[i].type=TYPE_STRING;
                        char buf[BUFFER_SIZE]={0};
                        if(ret==0){
                            c->res.tlv_array[i].length=sprintf(buf,"NOT EXIST\r\n");
                            c->res.tlv_array[i].value=(char *)kv_malloc(c->res.tlv_array[i].length);
                            memcpy(c->res.tlv_array[i].value,buf,c->res.tlv_array[i].length);
                        }else if(ret>0){
                            c->res.tlv_array[i].length=sprintf(buf,"EXIST\r\n");
                            c->res.tlv_array[i].value=(char *)kv_malloc(c->res.tlv_array[i].length);
                            memcpy(c->res.tlv_array[i].value,buf,c->res.tlv_array[i].length);
                        }else{
                            c->res.tlv_array[i].length=sprintf(buf,"ERROR\r\n");
                            c->res.tlv_array[i].value=(char *)kv_malloc(c->res.tlv_array[i].length);
                            memcpy(c->res.tlv_array[i].value,buf,c->res.tlv_array[i].length);
                        }
                    }
                    break;
                default:
                    break;
            }
            return len;
        }
    }
    return 0;
}
// int kv_request(struct conn*c){
//    c->wlength=kv_protocol(c->rbuffer,c->rlength,c->wbuffer,c->arg);
//    return c->wlength;
// }
int kv_response(struct conn*c){
    int offset=0;
    int ret=0;
    memset(c->wbuffer,0,BUFFER_SIZE);
        kv_header_t h;
        h.magic=KV_MAGIC;
        h.version=KV_VERSION;
        h.msg_type=MSG_TYPE_RSP;
        uint32_t data_len=0;
        for(int i=0;i<c->res.tlv_num;i++){
            data_len+=c->res.tlv_array[i].length;
        }
        h.length=data_len+10+c->res.tlv_num*7;//头部长度+data_len+7*tlv_num;
        h.opcode=OP_RES;
        ret=kv_header_encode(&h,c->wbuffer,c->wlength);
        offset+=ret;
        printf("tlv_num:%d\n",c->res.tlv_num);
        for(int i=0;i<c->res.tlv_num;i++){
            ret=kv_tlv_append(c->wbuffer+offset,c->wlength-offset,c->res.tlv_array[i]);
            offset+=ret;
        }
    return offset;
}
int send_backage(uint16_t opcode,char *wbuffer){//模拟发包，数据弄到wbuffer中
    kv_header_t header;
        header.magic=KV_MAGIC;
        header.version=KV_VERSION;
        header.msg_type=MSG_TYPE_REQ;
        header.opcode=opcode;
    int ret=0;
    int offset=0;
    int wlength=BUFFER_SIZE;
    //offset=kv_header_encode(&header,wbuffer+offset,c->wlength);
    if(opcode==OP_SET){
        printf("输入key:");
        char key[BUFFER_SIZE]={0};
        fgets(key,BUFFER_SIZE,stdin);
        key[strcspn(key,"\n")]='\0';
        printf("\n");
        printf("key:%s\n",key);
        printf("输入value:");
        char value[BUFFER_SIZE]={0};
        fgets(value,BUFFER_SIZE,stdin);
        value[strcspn(value,"\n")]='\0';
        printf("\n");
        printf("value:%s\n",value);
        //memset(wbuffer,0,BUFFER_SIZE);
        header.length=10+7*2+strlen(key)+1+strlen(value)+1;
        offset=kv_header_encode(&header,wbuffer+offset,wlength);
        TLV tlv;
        tlv.tag=TAG_KEY,tlv.type=TYPE_STRING,tlv.length=strlen(key)+1,tlv.value=key;
        ret=kv_tlv_append(wbuffer+offset,wlength-offset,tlv);
        offset+=ret;
        tlv.tag=TAG_VALUE,tlv.length=strlen(value)+1,tlv.value=value;
        ret=kv_tlv_append(wbuffer+offset,wlength-offset,tlv);
        offset+=ret;
        return offset;
    }else if(opcode==OP_GET){
        printf("输入key:");
        char key[BUFFER_SIZE]={0};
        fgets(key,BUFFER_SIZE,stdin);
        key[strcspn(key,"\n")]='\0';
        printf("\n");
        printf("key:%s\n",key);
        header.length=10+7+strlen(key)+1;
        offset=kv_header_encode(&header,wbuffer+offset,wlength);
        TLV tlv;
        tlv.tag=TAG_KEY,tlv.type=TYPE_STRING,tlv.length=strlen(key)+1,tlv.value=key;
        ret=kv_tlv_append(wbuffer+offset,wlength-offset,tlv);
        offset+=ret;
        return offset;
    }else if(opcode==OP_DEL){
        printf("输入key:");
        char key[BUFFER_SIZE]={0};
        fgets(key,BUFFER_SIZE,stdin);
        key[strcspn(key,"\n")]='\0';
        printf("\n");
        printf("key:%s\n",key);
        header.length=10+7+strlen(key)+1;
        offset=kv_header_encode(&header,wbuffer+offset,wlength);
        TLV tlv;
        tlv.tag=TAG_KEY,tlv.type=TYPE_STRING,tlv.length=strlen(key)+1,tlv.value=key;
        ret=kv_tlv_append(wbuffer+offset,wlength-offset,tlv);
        offset+=ret;
        return offset;
    }else if(opcode==OP_MOD){
        printf("输入key:");
        char key[BUFFER_SIZE]={0};
        fgets(key,BUFFER_SIZE,stdin);
        key[strcspn(key,"\n")]='\0';
        printf("\n");
        printf("key:%s\n",key);
        printf("输入value:");
        char value[BUFFER_SIZE]={0};
        fgets(value,BUFFER_SIZE,stdin);
        value[strcspn(value,"\n")]='\0';
        printf("\n");
        printf("value:%s\n",value);
        header.length=10+7*2+strlen(key)+1+strlen(value)+1;
        offset=kv_header_encode(&header,wbuffer+offset,wlength);
        TLV tlv;
        tlv.tag=TAG_KEY,tlv.type=TYPE_STRING,tlv.length=strlen(key)+1,tlv.value=key;
        ret=kv_tlv_append(wbuffer+offset,wlength-offset,tlv);
        offset+=ret;
        tlv.tag=TAG_VALUE,tlv.length=strlen(value)+1,tlv.value=value;
        ret=kv_tlv_append(wbuffer+offset,wlength-offset,tlv);
        offset+=ret;
        return offset;
    }else if(opcode==OP_EXIST){
        printf("输入key:");
        char key[BUFFER_SIZE]={0};
        fgets(key,BUFFER_SIZE,stdin);
        key[strcspn(key,"\n")]='\0';
        printf("\n");
        printf("key:%s\n",key);
        header.length=10+7+strlen(key)+1;
        offset=kv_header_encode(&header,wbuffer+offset,wlength);
        TLV tlv;
        tlv.tag=TAG_KEY,tlv.type=TYPE_STRING,tlv.length=strlen(key)+1,tlv.value=key;
        ret=kv_tlv_append(wbuffer+offset,wlength-offset,tlv);
        offset+=ret;
        return offset;
    }else if(opcode==OP_RES){
        return offset;
    }else{
        return -1;
    }
}

int main(int argc,char *argv[]){
    // printf("#####测试编码######\n");
    // kv_header_t header;
    //     header.magic=KV_MAGIC;
    //     header.version=KV_VERSION;
    //     header.msg_type=MSG_TYPE_REQ;
    //     header.length=BUFFER_SIZE;
    //     header.opcode=OP_SET;
    // int len=sizeof(header);
    // int offset=0;
    // char wbuffer[BUFFER_SIZE]={0};
    // int wlength=BUFFER_SIZE;
    // char rbuffer[BUFFER_SIZE]={0};
    // int rlength=BUFFER_SIZE;
    // char key[BUFFER_SIZE]={0};
    // char value[BUFFER_SIZE]={0};
    // printf("输入key:");
    // scanf("%s",key);
    // printf("\n");
    // printf("输入value:");
    // scanf("%s",value);
    // printf("\n");
    
    // memset(wbuffer,0,BUFFER_SIZE);
    // int ret=0;
    // offset=kv_header_encode(&header,wbuffer,BUFFER_SIZE);
    // printf("offset:%d,len=%d\n",offset,len);
    // //printf("header write into wbuffer:%s\n",wbuffer);
    // // 组包完成后打印整个缓冲区
    // hex_dump(wbuffer, offset, "组包结果");
    // TLV tlv;
    // memset(&tlv,0,sizeof(TLV));
    // //memset(wbuffer,0,BUFFER_SIZE);
    // tlv.tag=TAG_KEY,tlv.type=TYPE_STRING,tlv.length=strlen(key)+1,tlv.value=key;
    // ret=kv_tlv_append(wbuffer+offset,wlength,tlv);
    // offset+=ret;
    // printf("offset:%d\n",offset);
    // //printf("key write into wbuffer:%s\n",wbuffer);
    // hex_dump(wbuffer, offset, "组包结果");

    // //memset(wbuffer,0,BUFFER_SIZE);
    // tlv.tag=TAG_VALUE,tlv.type=TYPE_STRING,tlv.length=strlen(value)+1,tlv.value=value;
    // ret=kv_tlv_append(wbuffer+offset,wlength,tlv);
    // offset+=ret;
    // printf("offset:%d\n",offset);
    // //printf("key write into wbuffer:%s\n",wbuffer);
    // hex_dump(wbuffer, offset, "组包结果");



    // printf("#####测试解包####\n");
    // offset=0;
    // kv_header_t res_header;
    // kv_request_t req;
    // kv_request_t_init(&req);
    // offset=kv_header_decode(wbuffer,BUFFER_SIZE,&res_header);
    // // printf("%04X    ",res_header.magic);
    // // printf("%02X    ",res_header.version);
    // // printf("%02X    ",res_header.msg_type);
    // // printf("%04X    ",res_header.opcode);
    // // printf("%08X    ",res_header.length);
    // // printf("\n");
    // PRINT_STRUCT_MEM(&res_header, "kv_header_t");

    // ret=kv_payload_decode(wbuffer+offset,BUFFER_SIZE,&req);
    // PRINT_STRUCT_MEM(&req, "kv_request_t");
    // printf("\n");
    // for(int i=0;i<req.key_num;i++){
    //     printf("key:%s,length:%d\n",req.key[i]->data,req.key[i]->length);
    //     printf("value:%s,length:%d\n",req.value[i]->data,req.value[i]->length);
    // }



   // memset(wbuffer,0,BUFFER_SIZE);
   c.wlength=BUFFER_SIZE;
   c.rlength=BUFFER_SIZE;
    printf("#########测试核心协议层##########\n");
    uint16_t cmd;
    uint16_t opcode;
   
    int offset;
    int ret=0;
    while(1){
        offset=0;
        printf("输入命令：");
        char CMD[BUFFER_SIZE]={0};
        scanf("%s",CMD);
        getchar();
        for(cmd=0;cmd<KV_CMD_COUNT;cmd++){
            if(strcmp(command[cmd],CMD)==0){
                printf("输入的命令是：%s\n",command[cmd]);
                opcode=cmd;
                break;
            }
        }
       // memset(&c,0,sizeof(struct conn));
        offset=send_backage(opcode,c.rbuffer);//模拟客户端发包过来
        c.rlength=offset;
        printf("发包\n");
        hex_dump(c.rbuffer,offset,"组包结果");
        offset=0;
        
        
        
        printf("#############服务器端处理回包#################\n");
        kv_protocol(&c);//服务端处理,处理结果->res
        int flag;
        ret=kv_response(&c);
        offset+=ret;
        hex_dump(c.wbuffer, offset, "组包结果");
    
        offset=0;
        kv_header_t res_header;
        kv_request_t req;
        kv_request_t_init(&req);
        offset=kv_header_decode(c.wbuffer,BUFFER_SIZE,&res_header);
        PRINT_STRUCT_MEM(&res_header, "kv_header_t");

        ret=kv_payload_decode(c.wbuffer+offset,BUFFER_SIZE,&req);
       // PRINT_STRUCT_MEM(&req, "kv_request_t");
        printf("\n");
        for(int i=0;i<req.key_num;i++){
            printf("key:%s,length:%d\n",req.key[i]->data,req.key[i]->length);
            printf("value:%s,length:%d\n",req.value[i]->data,req.value[i]->length);
        }
    }
    return 0;
}