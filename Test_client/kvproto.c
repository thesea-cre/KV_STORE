#include"kvproto.h"
#include<arpa/inet.h>
#include<string.h>
#include<stdio.h>
#include<stdlib.h>
static int kv_header_encode(const kv_header_t header,uint8_t* buf,uint32_t buf_len);
static int kv_header_decode(kv_header_t * header,const uint8_t *buf,uint32_t buf_len);
static int kv_tlv_encode(const kv_tlv_t tlv,uint8_t *buf,uint32_t buf_len);
static int kv_tlv_decode(kv_tlv_t *tlv,const uint8_t *buf,uint32_t buf_len);
static int kv_payload_encode(const kv_payload_t data,uint8_t *buf,uint32_t buf_len);
static int kv_payload_decode(kv_payload_t *data,uint8_t* payload,uint32_t payload_len);
static void kv_payload_t_init(kv_payload_t *req);
static void kv_payload_t_destory(kv_payload_t *req);
char *command[]={
    "SET","GET","DEL","MOD","EXIST","RES","SET CHUNCK","ELSE CMD"
};
char *response[]={
    "OK\r\n","EXIST\r\n","NOT EXIST\r\n","ERROR\r\n"
};
enum{
    KV_RES_START,
    KV_RES_OK=KV_RES_START,
    KV_RES_EXIST,
    KV_RES_NOT_EXIST,
    KV_RES_ERROR,
    KV_RES_COUNT
};
static char* type_[]={
    "start","string","image","url","else"
};
static void* kv_malloc(int size){
    return malloc(size);
}
static void kv_free(void *ptr){
    return free(ptr);
}
static const uint8_t dummy_=0;
static int kv_header_encode(const kv_header_t header,uint8_t* buf,uint32_t buf_len){
    if(buf==NULL||buf_len<KV_HEADER_LEN) return ERROR;//检查边界

    uint16_t magic=htons(header.magic);
    uint8_t version=header.version;
    uint16_t opcode=htons(header.opcode);
    uint32_t len=htonl(header.length);

    int offset=0;
    memcpy(buf+offset,&magic,2),offset+=2;
    memcpy(buf+offset,&version,1),offset+=1;
    memcpy(buf+offset,&opcode,2),offset+=2;
    memcpy(buf+offset,&len,4),offset+=4;
    return offset;
}

static int kv_header_decode(kv_header_t * header,const uint8_t *buf,uint32_t buf_len){
    if(!buf||header==NULL) return ERROR;
    if(buf_len<KV_HEADER_LEN) return WAIT;
    int offset=0;
    uint16_t magic,opcode;
    uint32_t len;
    memcpy(&magic,buf+offset,2),offset+=2;
    header->magic=ntohs(magic);
    if(header->magic!=KV_MAGIC) return ERROR;//第一步判断包是否合法
    memcpy(&header->version,buf+offset,1),offset+=1;

    memcpy(&opcode,buf+offset,2),offset+=2;
    header->opcode=ntohs(opcode);
    memcpy(&len,buf+offset,4),offset+=4;
    header->length=ntohl(len);
    return offset;
}

//编码tlv只检查是否越界
static int kv_tlv_encode(const kv_tlv_t tlv,uint8_t *buf,uint32_t buf_len){
    if(buf==NULL||buf_len==0) return ERROR;

    uint8_t tlv_type=tlv.type;
    uint32_t tlv_key_len=tlv.key_length;
    uint8_t *tlv_key_data=tlv.key;
    uint32_t tlv_value_len=tlv.value_length;
    uint8_t *tlv_value_data=tlv.value;//
    uint32_t tlv_len=1+4+tlv_key_len+4+tlv_value_len;

    if(tlv_len>buf_len) return ERROR;//检查越界

    int offset=0;
    uint8_t type=tlv_type;
    uint32_t key_len=htonl(tlv_key_len);
    uint32_t value_len=htonl(tlv_value_len);
    //深拷贝
    memcpy(buf+offset,&type,1),offset+=1;//type
    memcpy(buf+offset,&key_len,4),offset+=4;//key_len
    memcpy(buf+offset,tlv_key_data,tlv_key_len),offset+=tlv_key_len;//key
    memcpy(buf+offset,&value_len,4),offset+=4;//value_len
    memcpy(buf+offset,tlv_value_data,tlv_value_len),offset+=tlv_value_len;
    return offset;
}
//上层确保buf和buf_len是对应的就可以了，协议层负责解码编码，检查是否是一个正常tlv
static int kv_tlv_decode(kv_tlv_t *tlv,const uint8_t *buf,uint32_t buf_len){
    if(buf==NULL||buf_len<TLV_LEN_MIN||!tlv) return ERROR;

    int offset=0;
    uint32_t key_len;
    uint32_t value_len;
    memcpy(&tlv->type,buf+offset,1),offset+=1;
    memcpy(&key_len,buf+offset,4),offset+=4;
    tlv->key_length=ntohl(key_len);
    if(tlv->key_length>DATA_LEN_MAX) return ERROR;//不能超出最大数据长度
    if(tlv->key_length+offset>buf_len) return ERROR;//确保memcpy不出错
    tlv->key=(uint8_t*)kv_malloc(tlv->key_length);//在KV_decode中释放
    memcpy(tlv->key,buf+offset,tlv->key_length),offset+=tlv->key_length;//做了一次深拷贝
    if(offset+4>buf_len){
        kv_free(tlv->key);
        return ERROR;
    }
    memcpy(&value_len,buf+offset,4),offset+=4;
    tlv->value_length=ntohl(value_len)+1;//加一个字节
    if(tlv->value_length-1>DATA_LEN_MAX){
        kv_free(tlv->key);
        return ERROR;
    }
    if(tlv->value_length-1+offset>buf_len){
        kv_free(tlv->key);
        return ERROR;
    }
    tlv->value=(uint8_t *)kv_malloc(tlv->value_length);//解码的时候多申请一个字节
    memcpy(tlv->value,&tlv->type,1);//这里不加一
    memcpy(tlv->value+1,buf+offset,tlv->value_length-1),offset+=(tlv->value_length-1);

    return offset;
}

static int kv_payload_encode(const kv_payload_t data,uint8_t *buf,uint32_t buf_len){
    if(!buf||buf_len<=0) return ERROR;

    int total_encode=0;
    for(int i=0;i<data.num;i++){
        int n=kv_tlv_encode(data.array[i],buf+total_encode,buf_len-total_encode);
        if(n==ERROR) return ERROR;
        total_encode+=n;

    }
    return total_encode;
}


static int kv_payload_decode(kv_payload_t *data,uint8_t* payload,uint32_t payload_len){
    
    if(payload==NULL||data==NULL||payload_len<TLV_LEN_MIN) return ERROR;
    
    int num=0;
    uint32_t offset=0;
    while(offset<payload_len){//逐条TLV解析，全部检查齐全

        kv_tlv_t tlv;//while循环一条一条解析，这个tlv只是一个临时变量
        memset(&tlv,0,sizeof(kv_tlv_t));
        int n=kv_tlv_decode(&tlv,payload+offset,payload_len-offset);//剩余长度传进去做边界检查
        //kv_tlv_decode做了一次深拷贝，数据复制到了tlv中
        if(n==ERROR){
            return ERROR;
        }
        offset+=n;
        data->array[num]=tlv;
        num++;
        if(num>KEY_VLAUE_NUM_MAX){
            return ERROR;
        }
    }
    data->num=num;
    return offset;
}

static void kv_payload_t_init(kv_payload_t *req){
    if(!req) return;
    req->num=KEY_VLAUE_NUM_MAX;
    req->array=(kv_tlv_t*)kv_malloc(req->num*sizeof(kv_tlv_t));
    memset(req->array,0,sizeof(req->num*sizeof(kv_tlv_t)));
    if(!req->array){
        perror("kv_malloc");
        kv_free(req->array);
    }
    return;
}
static void kv_payload_t_destory(kv_payload_t *req){
    if(req==NULL) return;
    kv_free(req->array);
    req->num=0;
}

//对外提供的接口，编码
//上层只关心op，key,value,上层要负责参数对齐
void kv_RES_t_destory(RES *res){
    for(int i=0;i<res->num;i++){
        kv_free(res->key[i]);
        kv_free(res->value[i]);
    }
    kv_free(res->key);
    kv_free(res->value);
    kv_free(res->value_len);
    res->op=NULL;
    return;
}
// void kv_RES_t_create(RES* res,char *op,char **key,char **value,char **type,int *value_len,int num){
//     if(!res||!op||num==0||!type) return;
//     res->op=op;
//     res->key=key;
//     res->num=(uint32_t)num;
//     res->value=(char **)kv_malloc(num*sizeof(char*));
//     for(int i=0;i<num;i++){
//         res->value[i]=(char *)kv_malloc(value_len[i]+1);
//         for(int j=KV_TYPE_START;j<KV_TYPE_COUNT;j++){
//             if(strcmp(type[i],type_[i])==0){
//                 res->value[i][0]=(uint8_t )j;
//                 break;
//             }
//         }
//         memcpy(res->value[i]+1,value[i],value_len[i]);
//     }
//     res->value_len=(uint32_t*)kv_malloc(num*sizeof(uint32_t));
//     for(int i=0;i<num;i++){
//         res->value_len[i]=value_len[i]+1;
//     }
//     return;
    
// }
//内存检查：res由外部产生，外部释放,本身只会malloc一个外壳，内部已经释放
void KV_encode(uint8_t *buf,uint32_t buf_len,RES *res){
    if(!res->op||!buf||buf_len==0||res->num<=0){
        sprintf(res->error_info,"res op||buf||buf_len||res->num");
        res->status=ERROR;
        return;
    }
    kv_header_t head;
    memset(&head,0,sizeof(kv_header_t));
    kv_payload_t payload;
    memset(&payload,0,sizeof(kv_payload_t));
    head.opcode=KV_CMD_OTHER;//先假设不合法
    for(int cmd=KV_CMD_START;cmd<KV_CMD_COUNT;cmd++){
        if(strcmp(res->op,command[cmd])==0){
            head.opcode=cmd;
            break;
        }
    }
    uint32_t payload_len=0;
    payload.num=res->num;
    payload.array=(kv_tlv_t*)kv_malloc(res->num*sizeof(kv_tlv_t));
    if(!payload.array){
        res->status=ERROR;
        sprintf(res->error_info,"payload.array malloc failed");
        return;
    }
    switch(head.opcode){
        case KV_CMD_SET:
        case KV_CMD_MOD:
            if(!res->key||!res->value||!res->value_len){
                res->status=ERROR;
                sprintf(res->error_info,"!res->key||!res->value||!res->value_len");
                goto err;
            }
            for(int i=0;i<res->num;i++){
                kv_tlv_t tlv;
                tlv.key=res->key[i];
                tlv.value=res->value[i]+1;
                tlv.key_length=strlen(res->key[i])+1;
                payload_len+=tlv.key_length;
                tlv.value_length=res->value_len[i]-1;//上一层传过来的值是类型加value
                payload_len+=tlv.value_length;

                tlv.type=res->value[i][0];
                payload.array[i]=tlv;
            }
            break;
        case KV_CMD_DEL:
        case KV_CMD_GET:
        case KV_CMD_EXIST:
            if(!res->key){
                res->status=ERROR;
                sprintf(res->error_info,"!res->key");
                goto err;
            }
            for(int i=0;i<res->num;i++){
                kv_tlv_t tlv;
                tlv.key=res->key[i];
                tlv.value=&dummy_;
                tlv.key_length=strlen(res->key[i])+1;
                payload_len+=tlv.key_length;
                tlv.value_length=1;
                payload_len+=tlv.value_length;
                tlv.type=0;
                payload.array[i]=tlv;
            }
            break;
        case KV_CMD_RES:
            if(!res->value||!res->value_len){
                sprintf(res->error_info,"!value||!res->value_len");
                res->status=ERROR;
                goto err;
            }
            for(int i=0;i<res->num;i++){
                kv_tlv_t tlv;
                tlv.key=&dummy_;
                tlv.value=res->value[i]+1;
                tlv.key_length=1;
                payload_len+=tlv.key_length;
                tlv.value_length=res->value_len[i]-1;
                payload_len+=tlv.value_length;
                tlv.type=res->value[i][0];
                payload.array[i]=tlv;
            }
            break;
        default:
            sprintf(res->error_info,"命令不存在");
            res->status=ERROR;
            goto err;
    }
    head.magic=KV_MAGIC;
    head.version=KV_VERSION;
    head.length=payload_len+res->num*9;
    // (*pkt_len)=head.length+HEAD_LEN;
    uint32_t offset=0;
    int ret=kv_header_encode(head,buf+offset,buf_len-offset);
    if(ret==ERROR){
        sprintf(res->error_info,"kv_header_encode");
        res->status=ERROR;
        goto err;
    }
    offset+=ret;
    ret=kv_payload_encode(payload,buf+offset,buf_len-offset);
    if(ret==ERROR){
        sprintf(res->error_info,"kv_payload_encode");
        res->status=ERROR;
        goto err;
    }

    kv_payload_t_destory(&payload);
    offset+=ret;
    res->cons_=offset;
    res->status=OK;
    return;
    err:
        kv_payload_t_destory(&payload);
        return;
}
//上层只关心从buf中解析出op,Key,value,type,num
//底层是做了深拷贝的，然后协议层封装好了所有权，最后把所有权交给res,所以decode把所有权(具体就是key,value)交给res后
//就可以释放句柄了，就是payload->arrary
void KV_decode(uint8_t *buf,uint32_t buf_len,RES *res){
    if(!buf){
        printf("buf,buf_len error\n");
        res->status=ERROR;
        sprintf(res->error_info,"buf,buf_len error\n");
        return;
    }
    if(buf_len<PACKAGE_LEN_MIN){
        res->status=WAIT;
        sprintf(res->error_info,"buf_len<pkt_len_min");
        return;
    }
    uint32_t offset=0;
    kv_header_t head;
    memset(&head,0,sizeof(kv_header_t));
    kv_payload_t payload;
    kv_payload_t_init(&payload);
    // memset(&payload,0,sizeof(kv_payload_t));
    
    int ret=kv_header_decode(&head,buf,HEAD_LEN);
    if(ret==ERROR){
        perror("kv_header_decode");
        res->status=ERROR;
        sprintf(res->error_info,"kv_header_decode");
        goto err;
    }
    if(ret==WAIT){
        res->status=WAIT;
        sprintf(res->error_info,"length<head_len");
        goto err;
    }
    if(head.magic!=KV_MAGIC){
        res->cons_=0;
        res->status=ERROR;
        sprintf(res->error_info,"not kvproto");
        goto err;
    }
    uint32_t pkt_len=head.length+HEAD_LEN;
    if(pkt_len>buf_len){//处理分包
        res->cons_=0;
        res->status=WAIT;
        sprintf(res->error_info,"length<pkt_len");
        goto err;
    }
    offset+=ret;
    ret=kv_payload_decode(&payload,buf+offset,head.length);
    if(ret==ERROR){
        perror("kv_payload_decode");
        res->status=ERROR;
        sprintf(res->error_info,"kv_payload_decode");
        goto err;
    }
    offset+=ret;
    res->num=payload.num;
    res->key=(uint8_t**)kv_malloc(res->num*sizeof(uint8_t *));
    memset(res->key,0,sizeof(res->num*sizeof(uint8_t*)));
    res->value=(uint8_t **)kv_malloc(res->num*sizeof(uint8_t*));
    memset(res->value,0,sizeof(res->num*sizeof(uint8_t*)));
    res->value_len=(size_t *)kv_malloc(res->num*sizeof(size_t));
    memset(res->value_len,0,res->num*sizeof(size_t));
    // switch(head.opcode){
    //     case KV_CMD_SET:
    //     case KV_CMD_DEL:
    //     case KV_CMD_MOD:
    //         for(int i=0;i<res->num;i++){
    //             res->key[i]=payload.array[i].key;//这里只是做了浅拷贝
    //             res->value[i]=payload.array[i].value;
    //             res->value_len[i]=payload.array[i].value_length;
    //         }
    //         break;
    //     case KV_CMD_GET:
    //     case KV_CMD_EXIST:
    //         for(int i=0;i<res->num;i++){
    //             res->key[i]=payload.array[i].key;
    //             res->value[i]=;
    //             res->value_len[i]=payload.array[i].value_length;
    //         }
    //         break;
    //     case KV_CMD_RES:
    //         for(int i=0;i<res->num;i++){
    //             res->key[i]=NULL;
    //             res->value[i]=payload.array[i].value;
    //             res->value_len[i]=payload.array[i].value_length;
    //         }
    //         break;
    //     default:
    //         printf("不认识的命令\n");
    //         break;
    // }
    for(int i=0;i<res->num;i++){
        res->key[i]=payload.array[i].key;//这里只是做了浅拷贝
        res->value[i]=payload.array[i].value;
        res->value_len[i]=payload.array[i].value_length;
    }
    res->op=command[KV_CMD_OTHER];
    for(int cmd=KV_CMD_START;cmd<KV_CMD_COUNT;cmd++){
        if(head.opcode==cmd){
            res->op=command[cmd];
            break;
        }
    }
    res->cons_=offset;
    res->status=OK;
    err:
        kv_payload_t_destory(&payload);
        return;
}   
//encode，数据在key,value中，经过一次深拷贝，在kv_tlv_encode中，拷贝到了buf中，payload,tlv等结构体都是浅拷贝，指向数据
//decode,数据在buf中，经过深拷贝到结构体payload中，然后所有权转给key,value,释放payload->array
