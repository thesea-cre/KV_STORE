#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<assert.h>
#include"kvstore.h"
#include"server.h"
#include"kv_array.h"
#include"kv_hash.h"
#include"kv_rbtree.h"
#include"mempool.h"
#include<arpa/inet.h>
#define KV_MAX_TOKENS 8
#define BUFFER_SIZE 1024
kv_array_t global_array;
KV_Map global_hash;
KV_Rbtree global_rbtree;
static Mempool_Manager *msg;
void *kv_malloc(size_t size){//用的很多的系统函数最好包装一层，为了修改方便
    #if 0
    return malloc(size);
    #else 
    return Managerpool_Alloc(msg,size);
    #endif
}
void kv_free(void* ptr){
    #if 0
    return free(ptr);
    #else
    return Managerpool_free(msg,ptr);
    #endif
}
int kv_split_token(char *msg,char* tokens[]){
    if(msg==NULL||tokens==NULL) return -1;
    int i=0;
    char *token=strtok(msg," ");
    while(token!=NULL){
        tokens[i++]=token;
        token=strtok(NULL," ");
    }
    return i;
}
int kv_filter_protocol(char **tokens,int count,char *response){
    if(tokens==NULL||count<=0||response==NULL){
      //  printf("kv_filter_protocol\n");
        return -1;
    }
    int flag=0;
    int cmd;
    for(cmd=KV_CMD_START;cmd<KV_CMD_COUNT;cmd++){
        if(strcmp(tokens[0],command[cmd])==0){
            flag=1;
            break;
        }
    }
    if(flag==0){
       // printf("not kv_protocol\n");
        exit(1);
    }
    char*key=tokens[1];
    //printf("key:%s\n",key);
    char *value=tokens[2];
    //printf("value:%s\n",value);
    int ret;
    int length=0;
    switch(cmd){
        case KV_CMD_SET:
            #if ENGINE==ENGINE_ARRAY
            ret=kv_array_set(&global_array,key,value);
            #elif ENGINE==ENGINE_HASH
            ret=KV_Map_set(&global_hash,key,value);
            #elif ENGINE==ENGINE_RBTREE
            ret=KV_Rbtree_set(&global_rbtree,key,value);
            #endif
            if(ret==0){
                length=sprintf(response,"OK\r\n");
            }else if(ret>0){
                length=sprintf(response,"EXIST\r\n");
            }else{
                length=sprintf(response,"ERROR\r\n");
            }
            break;
        case KV_CMD_GET:
            #if ENGINE==ENGINE_ARRAY
            char *v=kv_array_get(&global_array,key);
            #elif ENGINE==ENGINE_HASH
            char *v=KV_Map_get(&global_hash,key);
            #elif ENGINE==ENGINE_RBTREE
            char *v =KV_Rbtree_get(&global_rbtree,key);
            #endif
            if(v==NULL){
                length=sprintf(response,"NO EXIST\r\n");
            }else{
                length=sprintf(response,"%s\r\n",v);
            }
            break;
        case KV_CMD_DEL:
            #if ENGINE==ENGINE_ARRAY
            ret=kv_array_del(&global_array,key);
            #elif ENGINE==ENGINE_HASH
            ret=KV_Map_del(&global_hash,key);
            #elif ENGINE==ENGINE_RBTREE
            ret=KV_Rbtree_del(&global_rbtree,key);
            #endif
            if(ret==0){
                length=sprintf(response,"OK\r\n");
            }else if(ret>0){
                length=sprintf(response,"NO EXIST\r\n");
            }else{
                length=sprintf(response,"ERROR\r\n");
            }
            break;
        case KV_CMD_MOD:
            #if ENGINE==ENGINE_ARRAY
            ret=kv_array_mod(&global_array,key,value);
            #elif ENGINE==ENGINE_HASH
            ret=KV_Map_mod(&global_hash,key,value);
            #elif ENGINE==ENGINE_RBTREE
            ret=KV_Rbtree_mod(&global_rbtree,key,value);
            #endif
            if(ret==0){
                length=sprintf(response,"OK\r\n");
            }else if(ret>0){
                length=sprintf(response,"NO EXIST\r\n");
            }else{
                length=sprintf(response,"ERROR\r\n");
            }
            break;
        case KV_CMD_EXIST:
            #if ENGINE==ENGINE_ARRAY
            ret=kv_array_exist(&global_array,key);
            #elif ENGINE==ENGINE_HASH
            ret=KV_Map_exist(&global_hash,key);
            #elif ENGINE==ENGINE_RBTREE
            ret=KV_Rbtree_exist(&global_rbtree,key);
            #endif
            if(ret>0){
                length=sprintf(response,"EXIST\r\n");
            }else if(ret==0){
                length=sprintf(response,"NO EXIST\r\n");
            }else if(ret<0){
                length=sprintf(response,"ERROR\r\n");
            }
            break;
        default:
            assert(0);
            break;
    }
    return length;
}
#if KV_COMPLEX
int kv_protocol(char *msg,int rlength,result *c){
    kv_header_t header;
    kv_request_t req;
    memset(&header,0,sizeof(kv_header_t));
    // char * msg=c->rbuffer;
    // int rlength=c->rlength;

    int offset=kv_header_decode(msg,rlength,&header);
    uint8_t version=header.version;
    uint8_t msg_type=header.msg_type;
    uint16_t opcode=header.opcode;
    uint32_t len=header.length;
   
    kv_request_t_init(&req);
    kv_payload_decode(msg+offset,rlength-offset,&req);//解析协议，结果处理到req中，里面都是key,value
    for(int i=0;i<req.key_num;i++){
            printf("key:%s,length:%d\n",req.key[i]->data,req.key[i]->length);
            printf("value:%s,length:%d\n",req.value[i]->data,req.value[i]->length);
    }
    // for(int i=0;i<req.data_num;i++){
    //     if(req.data[i]->tag==TAG_KEY){
    //         printf("key:%s,length:%d\n",req.data[i]->data,req.data[i]->length);
    //     }else if(req.data[i]->tag==TAG_VALUE){
    //         printf("value:%s,length:%d\n",req.data[i]->data,req.data[i]->length);
    //     }else if(req.data[i]->tag==TAG_RES){
    //         printf("res:%s,length:%d\n",req.data[i]->data,req.data[i]->length);
    //     }
    // }
    if(version==KV_VERSION){//版本
        if(msg_type==MSG_TYPE_REQ){//协议类型
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
            if(flag==0) {
                printf("命令输入错误\n");
                exit(1);
            }
            c->tlv_num=req.key_num;
            //c->res.tlv_num=req.data_num;
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
                        c->tlv_array=(TLV*)kv_malloc(c->tlv_num*sizeof(TLV));
                        c->tlv_array[i].tag=TAG_RES;
                        c->tlv_array[i].type=TYPE_STRING;
                        char buf[BUFFER_SIZE]={0};
                        if(ret==0){
                            c->tlv_array[i].length=sprintf(buf,"OK\r\n");
                            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
                            memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
                        }else if(ret>0){
                            c->tlv_array[i].length=sprintf(buf,"EXIST\r\n");
                            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
                            memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
                        }else{
                            c->tlv_array[i].length=sprintf(buf,"ERROR\r\n");
                            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
                            memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
                        }
                    }
                    // for(int i=0;i<req.data_num;i++){
                    //     if(req.data[i]->tag==TAG_KEY){
                    //         char *key=req.data[i]->data;
                    //     }else if(req.data[i]->tag==TAG_VALUE){
                    //         char *value=req.data[i]->data;
                    //     }else if(req.data[i]->tag==TAG_RES){
                    //         char *res=req.data[i]->data;
                    //     }

                    // }
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
                        c->tlv_array=(TLV*)kv_malloc(c->tlv_num*sizeof(TLV));
                        c->tlv_array[i].tag=TAG_VALUE;
                        c->tlv_array[i].type=TYPE_STRING;
                        char buf[BUFFER_SIZE]={0};
                        if(value){
                            c->tlv_array[i].length=strlen(value)+1;
                            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
                            memcpy(c->tlv_array[i].value,value,c->tlv_array[i].length);
                        }else if(value==NULL){
                            c->tlv_array[i].length=sprintf(buf,"NOT EXIST\r\n");
                            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
                            memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
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
                        c->tlv_array=(TLV*)kv_malloc(c->tlv_num*sizeof(TLV));
                        c->tlv_array[i].tag=TAG_RES;
                        c->tlv_array[i].type=TYPE_STRING;
                        char buf[BUFFER_SIZE]={0};
                        if(ret==0){
                            c->tlv_array[i].length=sprintf(buf,"OK\r\n");
                            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
                            memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
                        }else if(ret>0){
                            c->tlv_array[i].length=sprintf(buf,"NOT EXIST\r\n");
                            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
                            memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
                        }else{
                            c->tlv_array[i].length=sprintf(buf,"ERROR\r\n");
                            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
                            memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
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
                        c->tlv_array=(TLV*)kv_malloc(c->tlv_num*sizeof(TLV));
                        c->tlv_array[i].tag=TAG_RES;
                        c->tlv_array[i].type=TYPE_STRING;
                        char buf[BUFFER_SIZE]={0};
                        if(ret==0){
                            c->tlv_array[i].length=sprintf(buf,"OK\r\n");
                            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
                            memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
                        }else if(ret>0){
                            c->tlv_array[i].length=sprintf(buf,"NOT EXIST\r\n");
                            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
                            memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
                        }else{
                            c->tlv_array[i].length=sprintf(buf,"ERROR\r\n");
                            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
                            memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
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
                        c->tlv_array=(TLV*)kv_malloc(c->tlv_num*sizeof(TLV));
                        c->tlv_array[i].tag=TAG_RES;
                        c->tlv_array[i].type=TYPE_STRING;
                        char buf[BUFFER_SIZE]={0};
                        if(ret==0){
                            c->tlv_array[i].length=sprintf(buf,"NOT EXIST\r\n");
                            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
                            memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
                        }else if(ret>0){
                            c->tlv_array[i].length=sprintf(buf,"EXIST\r\n");
                            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
                            memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
                        }else{
                            c->tlv_array[i].length=sprintf(buf,"ERROR\r\n");
                            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
                            memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
                        }
                    }
                    break;
                default:
                    break;
            }
            kv_request_t_destory(&req);
            return len;
        }
    }
    return 0;
}
#else
int kv_protocol(char *msg,int rlength,char *request){
    if(msg==NULL||rlength<=0||request==NULL) {
        return -1;
    }
    char *tokens
    int count=kv_split_token(msg,tokens);
    return kv_filter_protocol(tokens,count,request);
}
#endif
int kv_request(char *msg,int len,result*c){
   return kv_protocol(msg,len,c);
}
int kv_response(char *wbuffer,int len,result *c){
    int offset=0;
    int ret=0;
    memset(wbuffer,0,len);
        kv_header_t h;
        h.magic=KV_MAGIC;
        h.version=KV_VERSION;
        h.msg_type=MSG_TYPE_RSP;
        uint32_t data_len=0;
        for(int i=0;i<c->tlv_num;i++){
            data_len+=c->tlv_array[i].length;
        }
        h.length=data_len+10+c->tlv_num*7;//头部长度+data_len+7*tlv_num;
        h.opcode=OP_RES;
        ret=kv_header_encode(&h,wbuffer,len);
        offset+=ret;
        printf("tlv_num:%d\n",c->tlv_num);
        for(int i=0;i<c->tlv_num;i++){
            ret=kv_tlv_append(wbuffer+offset,len-offset,c->tlv_array[i]);
            offset+=ret;
        }
    return offset;
}
void init_kvengine(void){
    #if ENGINE==ENGINE_ARRAY
    memset(&global_array,0,sizeof(kv_array_t));
    kv_array_create(&global_array);
    #elif ENGINE==ENGINE_HASH
    memset(&global_hash,0,sizeof(KV_Map));
    KV_Map_create(&global_hash);
    #elif ENGINE==ENGINE_RBTREE
    memset(&global_rbtree,0,sizeof(KV_Rbtree));
    KV_Rbtree_create(&global_rbtree);
    #endif
}
void dest_kvengine(void){
    #if ENGINE==ENGINE_ARRAY
    kv_array_destory(&global_array);
    #elif ENGINE==ENGINE_HASH
    KV_Map_destory(&global_hash);
    #elif ENGINE==ENGINE_RBTREE
    KV_Rbtree_destory(&global_rbtree);
    #endif
}
int main(int argc,char *argv[]){
    if(argc!=2) return -1;
    #if 1
    msg=Managerpool_GetInstance();
    #endif
    unsigned int port=atoi(argv[1]);
    init_kvengine();
    #if NETWORK_SELECT == ENABLE_REACTOR
    reactor_entry(port);
    #elif NETWORK_SELECT == ENABLE_PROACTOR
    uring_start(port);
    #elif NETWORK_SELECT == ENABLE_NTYCO
    nytco(port);
    #endif
    dest_kvengine();
}