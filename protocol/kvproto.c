#include"kvproto.h"
#include<arpa/inet.h>
#include<string.h>
#include<stdio.h>
#include<stdlib.h>
#include"tool.h"
#include"log.h"
static int kv_header_encode(const kv_header_t header,uint8_t* buf,uint32_t buf_len);
static int kv_header_decode(kv_header_t * header,const uint8_t *buf,uint32_t buf_len);
static int kv_tlv_encode(const kv_tlv_t tlv,uint8_t *buf,uint32_t buf_len);
static int kv_tlv_decode(kv_tlv_t *tlv,const uint8_t *buf,uint32_t buf_len);
static int kv_payload_encode(const kv_payload_t data,uint8_t *buf,uint32_t buf_len);
static int kv_payload_decode(kv_payload_t *data,uint8_t* payload,uint32_t payload_len);
static void kv_payload_t_init(kv_payload_t *req);
static void kv_payload_t_destory(kv_payload_t *req);
//  char *command[]={
//     "SET","GET","DEL","MOD","EXIST","RES","ELSE CMD"
// };
const char *response[]={
    "OK","ERROR","EXIST","NOT EXIST"
};
const char *command[]={
    "SET","GET","DEL","MOD","EXIST","RES","SET_CHUNK","ELSE CMD"
};
const char* type_[]={
    "start","string","image","url","else"
};
// static char* type_[]={
//     "start","string","image","url","else"
// };

static const uint8_t dummy_=0;
static int kv_header_encode(const kv_header_t header,uint8_t* buf,uint32_t buf_len){
    if(buf==NULL||buf_len<HEAD_LEN){
        LOG_ERROR("头部写不进去 kv_header_encode buf_len<LEN\n");
        return ERROR;
    }
    

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
    if(!buf||header==NULL){
        LOG_ERROR("kv_header_decode 参数不合法 buf/header");
        return ERROR;
    }
    int offset=0;
    uint16_t magic,opcode;
    uint32_t len;
    memcpy(&magic,buf+offset,2),offset+=2;
    header->magic=ntohs(magic);
    if(header->magic!=KV_MAGIC) {//简单的头部协议验证
        LOG_ERROR("魔数校验失败");
        return ERROR;
    }
    memcpy(&header->version,buf+offset,1),offset+=1;

    memcpy(&opcode,buf+offset,2),offset+=2;
    header->opcode=ntohs(opcode);
    memcpy(&len,buf+offset,4),offset+=4;
    header->length=ntohl(len);
    return offset;
}

//编码tlv只检查是否越界
static int kv_tlv_encode(const kv_tlv_t tlv,uint8_t *buf,uint32_t buf_len){
    if(buf==NULL||buf_len==0){
        LOG_ERROR("参数不合法 kv_tlv_encode buf/buf_len");
        return ERROR;
    }
    uint8_t tlv_type=tlv.type;
    uint32_t tlv_key_len=tlv.key_length;
    uint8_t *tlv_key_data=tlv.key;
    uint32_t tlv_value_len=tlv.value_length;
    uint8_t *tlv_value_data=tlv.value;//
    uint32_t tlv_len=1+4+tlv_key_len+4+tlv_value_len;

    if(tlv_len>buf_len){
        LOG_ERROR("缓冲区即将溢出 kv_tlv_encode");
        return ERROR;
    }

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
//检查一个命令是否完整
static int kv_tlv_check(uint8_t *buf,uint32_t buf_len){
    if(buf_len<TLV_LEN_MIN||!buf){
        return ERROR;
    }
    uint8_t type;
    uint32_t key_len;
    uint32_t value_len;
    memcpy(&type,buf,1);
    memcpy(&key_len,buf+1,4),key_len=ntohl(key_len);
    memcpy(&value_len,buf+6,4),value_len=ntohl(value_len);
    uint32_t op_len=1+4+key_len+4+value_len;
    if(op_len>buf_len){
        return ERROR;
    }
    return OK;

}
//解析一个命令，结果放到tlv中
//类型一起放到value中
static int kv_tlv_decode(kv_tlv_t *tlv,const uint8_t *buf,uint32_t buf_len){
    if(buf==NULL||buf_len<TLV_LEN_MIN||!tlv) {
        LOG_ERROR("kv_tlv_decode params wrong");
        return ERROR;
    }

    int ret=kv_tlv_check(buf,buf_len);
    if(ret==ERROR){
        return ret;
    }

    int offset=0;
    uint32_t key_len;
    uint32_t value_len;
    memcpy(&tlv->type,buf+offset,1),offset+=1;
    memcpy(&key_len,buf+offset,4),offset+=4;
    tlv->key_length=ntohl(key_len);
    tlv->key=(uint8_t*)kv_malloc(tlv->key_length);//在KV_decode中释放
    if(!tlv->key){
        LOG_ERROR("tlv->key NULL kv_malloc failed");
        return ERROR;
    }
    memcpy(tlv->key,buf+offset,tlv->key_length),offset+=tlv->key_length;//做了一次深拷贝
    memcpy(&value_len,buf+offset,4),offset+=4;
    tlv->value_length=ntohl(value_len)+1;//加一个字节
    tlv->value=(uint8_t *)kv_malloc(tlv->value_length);//解码的时候多申请一个字节
    if(!tlv->value){
        LOG_ERROR("tlv->value NULL kv_malloc failed");
        kv_free(tlv->key);//key 已分配，这里失败要回收，否则泄漏
        tlv->key=NULL;
        return ERROR;
    }
    memcpy(tlv->value,&tlv->type,1);//这里不加一
    memcpy(tlv->value+1,buf+offset,tlv->value_length-1),offset+=(tlv->value_length-1);
    return offset;
}

static int kv_payload_encode(const kv_payload_t data,uint8_t *buf,uint32_t buf_len){
    if(!buf||buf_len<=0) {
        LOG_ERROR("kv_payload_encode 参数不合法 buf/buf_len");
        return ERROR;
    }
    int total_encode=0;
    for(int i=0;i<data.num;i++){
        int n=kv_tlv_encode(data.array[i],buf+total_encode,buf_len-total_encode);
        if(n==ERROR){
            LOG_ERROR("kv_tlv_encode 失败");
            return ERROR;
        }
        total_encode+=n;

    }
    return total_encode;
}


/* 解码失败时释放已经解出来的 TLV，避免坏包/超长包造成内存泄漏 */
static void kv_payload_cancel(kv_payload_t *data,int num){
    for(int i=0;i<num;i++){
        if(data->array[i].key)   kv_free(data->array[i].key);
        if(data->array[i].value) kv_free(data->array[i].value);
        data->array[i].key=NULL;
        data->array[i].value=NULL;
    }
    data->num=0;
}

static int kv_payload_decode(kv_payload_t *data,uint8_t* payload,uint32_t payload_len){
    
    if(payload==NULL||data==NULL||payload_len<TLV_LEN_MIN){
        LOG_ERROR("参数不合法或者缓冲区不够 kv_payload_decode");
        return ERROR;
    }
    
    int num=0;
    uint32_t offset=0;
    while(offset<payload_len){//逐条TLV解析，全部检查齐全

        if(num>=KEY_VLAUE_NUM_MAX){//先判上限再写数组：原来在写之后判，第11条会越界
            LOG_ERROR("TLV 条数超过上限 %d", KEY_VLAUE_NUM_MAX);
            kv_payload_cancel(data,num);
            return ERROR;
        }
        kv_tlv_t tlv;//while循环一条一条解析，这个tlv只是一个临时变量
        memset(&tlv,0,sizeof(kv_tlv_t));
        int n=kv_tlv_decode(&tlv,payload+offset,payload_len-offset);//剩余长度传进去做边界检查
        //kv_tlv_decode做了一次深拷贝，数据复制到了tlv中
        if(n==ERROR){
            LOG_ERROR("kv_tlv_decode 失败");
            kv_payload_cancel(data,num);
            return ERROR;
        }
        offset+=n;
        data->array[num]=tlv;
        num++;
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
        LOG_ERROR("kv_payload_t_init失败，req->array 的mallloc失败");
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
//face to client easy to create pkt
void kv_RES_t_create(RES* res,char *op,char **key,char **value,char **type,int *value_len,int num){
    if(!res||!op||num==0||!type) return;
    res->op=op;
    res->key=key;
    res->num=(uint32_t)num;
    res->value=(char **)kv_malloc(num*sizeof(char*));
    for(int i=0;i<num;i++){
        res->value[i]=(char *)kv_malloc(value_len[i]+1);
        for(int j=KV_TYPE_START;j<KV_TYPE_COUNT;j++){
            if(strcmp(type[i],type_[i])==0){
                res->value[i][0]=(uint8_t )j;
                break;
            }
        }
        memcpy(res->value[i]+1,value[i],value_len[i]);
    }
    res->value_len=(uint32_t*)kv_malloc(num*sizeof(uint32_t));
    for(int i=0;i<num;i++){
        res->value_len[i]=value_len[i]+1;
    }
    return;
    
}
//内存检查：res由外部产生，外部释放,本身只会malloc一个外壳，内部已经释放
//类型需要自己和value组装好
void KV_encode(uint8_t *buf,uint32_t buf_len,RES *res){
    if(!res->op||!buf||buf_len==0||res->num<=0){
        sprintf(res->error_info,"res op||buf||buf_len||res->num");
        LOG_ERROR("KV_encode 传参错误，参数不合法");
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
        LOG_ERROR("KV_encode kv_malloc 失败，payload_array");
        sprintf(res->error_info,"payload.array malloc failed");
        return;
    }

    switch(head.opcode){
        case KV_CMD_SET:
        case KV_CMD_SET_CHUNK://大包分片：编码格式与 SET 相同，值里是 [TYPE_CHUNK][total][offset][data]
        case KV_CMD_MOD:
            if(!res->key||!res->value||!res->value_len){
                res->status=ERROR;
                LOG_ERROR("KV_encode 编码set/mod命令 key/value/value_len为NULL");
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
                LOG_ERROR("KV_enode 编码del/get/exist命令 key为NULL");
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
                LOG_ERROR("KV_enode 编码res命令 value/value_len 为NULL");
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
            LOG_ERROR("KV_enode 命令不存在");
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
        LOG_ERROR("kv_header_encode 调用失败");
        res->status=ERROR;
        goto err;
    }
    offset+=ret;
    ret=kv_payload_encode(payload,buf+offset,buf_len-offset);
    if(ret==ERROR){
        sprintf(res->error_info,"kv_payload_encode");
        LOG_ERROR("kv_payload_encode 调用失败");
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


//真正处理一个完整帧，按格式解析协议，先头后体
//res字段 status返回处理结果：ERROR表示帧不合法 WAIT表示帧不齐 OK表示处理完一个帧
void KV_decode(uint8_t *buf,uint32_t buf_len,RES *res){
    if(!buf){
        LOG_ERROR("KV_decode failed 参数buf不合法为空");
        res->status=ERROR;
        sprintf(res->error_info,"buf,buf_len error\n");
        return;
    }
    //小于最小帧长
    if(buf_len<PACKAGE_LEN_MIN){
        res->status=WAIT;
        sprintf(res->error_info,"buf_len<pkt_len_min");
        LOG_DEBUG("KV_decode 缓冲区长度不够，buf_len小于最小帧长");
        return;
    }
    uint32_t offset=0;
    kv_header_t head;
    memset(&head,0,sizeof(kv_header_t));
   
    
    int ret=kv_header_decode(&head,buf,HEAD_LEN);
    if(ret==ERROR){
        perror("kv_header_decode");
        res->status=ERROR;
        sprintf(res->error_info,"kv_header_decode");
        goto err;
    }

    uint32_t pkt_len=head.length+HEAD_LEN;//解析出帧长
    if(pkt_len>KV_FRAME_MAX){//单帧超过服务端上限：永远收不齐，直接判坏包
        res->cons_=0;
        res->status=ERROR;
        sprintf((char*)res->error_info,"frame too large");
        goto err;
    }

    if(pkt_len>buf_len){//处理分包
        res->cons_=0;
        res->status=WAIT;
        sprintf(res->error_info,"length<pkt_len");
        goto err;
    }

    offset+=ret;

    kv_payload_t payload;//解析命令存放结果的结构体，有存放上限，
    kv_payload_t_init(&payload);
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

