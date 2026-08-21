#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<assert.h>
#include"kvstore.h"
#include"server.h"
#include"kv_array.h"
#define KV_MAX_TOKENS 8
kv_array_t global_array;
const char *command[]={
    "SET","GET","DEL","MOD","EXIST"
};
const char *response[]={

};
enum{
    KV_CMD_START,
    KV_CMD_SET=KV_CMD_START,
    KV_CMD_GET,
    KV_CMD_DEL,
    KV_CMD_MOD,
    KV_CMD_EXIST,
    
    KV_CMD_COUNT
};
void *kv_malloc(size_t size){//用的很多的系统函数最好包装一层，为了修改方便
    return malloc(size);
}
void kv_free(void* ptr){
    return free(ptr);
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
            ret=kv_array_set(&global_array,key,value);
            if(ret==0){
                length=sprintf(response,"OK\r\n");
            }else if(ret>0){
                length=sprintf(response,"EXIST\r\n");
            }else{
                length=sprintf(response,"ERROR\r\n");
            }
            break;
        case KV_CMD_GET:
            char *v=kv_array_get(&global_array,key);
            if(v==NULL){
                length=sprintf(response,"NO EXIST\r\n");
            }else{
                length=sprintf(response,"%s\r\n",v);
            }
            break;
        case KV_CMD_DEL:
            ret=kv_array_del(&global_array,key);
            if(ret==0){
                length=sprintf(response,"OK\r\n");
            }else if(ret>0){
                length=sprintf(response,"NO EXIST\r\n");
            }else{
                length=sprintf(response,"ERROR\r\n");
            }
            break;
        case KV_CMD_MOD:
            ret=kv_array_mod(&global_array,key,value);
            if(ret==0){
                length=sprintf(response,"OK\r\n");
            }else if(ret>0){
                length=sprintf(response,"NO EXIST\r\n");
            }else{
                length=sprintf(response,"ERROR\r\n");
            }
            break;
        case KV_CMD_EXIST:
            ret=kv_array_exist(&global_array,key);
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
int kv_protocol(char *msg,int rlength,char *request){
    if(msg==NULL||rlength<=0||request==NULL) {
       // printf("kv_protocol\n");
        return -1;
    }
    char *tokens[KV_MAX_TOKENS]={0};
    int count=kv_split_token(msg,tokens);
   // printf("count:%d\n",count);
    return kv_filter_protocol(tokens,count,request);
}
int kv_request(struct conn*c){
   c->wlength=kv_protocol(c->rbuffer,c->rlength,c->wbuffer);
   return c->wlength;
}
int kv_response(struct conn*c){
    c->status=0;
}
void init_kvengine(void){
    memset(&global_array,0,sizeof(kv_array_t));
    kv_array_create(&global_array);
}
void dest_kvengine(void){
    kv_array_destory(&global_array);
}
int main(int argc,char *argv[]){
    if(argc!=2) return -1;
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