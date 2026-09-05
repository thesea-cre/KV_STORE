#include"kvstore.h"
kv_array_t global_array;
KV_Map global_hash;
KV_Rbtree global_rbtree;
static Mempool_Manager *msg;
void *kv_malloc(size_t size){//用的很多的系统函数最好包装一层，为了修改方便
    #if 1
    return malloc(size);
    #else 
    return Managerpool_Alloc(msg,size);
    #endif
}
void kv_free(void* ptr){
    #if 1
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
int kv_cmd_set(kv_request_t req,result*c){
    c->tlv_array=(TLV*)kv_malloc(c->tlv_num*sizeof(TLV));
    int ret;
    for(int i=0;i<req.key_num;i++){
        char *key=req.key[i]->data;
        char *value=req.value[i]->data;
        uint8_t value_type=req.value[i]->type;
        if(value_type==TYPE_STRING){
            //不同类型执行不同的函数，最后将真的value值存入值中
        }
        #if ENGINE==ENGINE_ARRAY
        ret=kv_array_set(&global_array,key,value);
        #elif ENGINE==ENGINE_HASH
        ret=KV_Map_set(&global_hash,key,value);
        #elif ENGINE==ENGINE_RBTREE
        ret=KV_Rbtree_set(&global_rbtree,key,value);
        #endif
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
}
int kv_cmd_get(kv_request_t req,result*c){
    c->tlv_array=(TLV*)kv_malloc(c->tlv_num*sizeof(TLV));
    for(int i=0;i<req.key_num;i++){
        char *key=req.key[i]->data;//存入的时候没有存入值的类型，直接存的是字节流，get出来怎么解析呢
        char *value;
        #if ENGINE==ENGINE_ARRAY
        value=kv_array_get(&global_array,key);
        #elif ENGINE==ENGINE_HASH
        value=KV_Map_get(&global_hash,key);
        #elif ENGINE==ENGINE_RBTREE
        value=KV_Rbtree_get(&global_rbtree,key);
        #endif
        
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
}
int kv_cmd_mod(kv_request_t req,result*c){
    int ret;
    c->tlv_array=(TLV*)kv_malloc(c->tlv_num*sizeof(TLV));
    for(int i=0;i<req.key_num;i++){
        char *key=req.key[i]->data;
        char *value=req.value[i]->data;
        uint8_t value_type=req.value[i]->type;
        if(value_type==TYPE_STRING){
            //不同类型执行不同的函数，最后将真的value值存入值中
        }
        #if ENGINE==ENGINE_ARRAY
        ret=kv_array_mod(&global_array,key,value);
        #elif ENGINE==ENGINE_HASH
        ret=KV_Map_mod(&global_hash,key,value);
        #elif ENGINE==ENGINE_RBTREE
        ret=KV_Rbtree_mod(&global_rbtree,key,value);
        #endif
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
}
int kv_cmd_del(kv_request_t req,result*c){
    int ret;
    c->tlv_array=(TLV*)kv_malloc(c->tlv_num*sizeof(TLV));
    for(int i=0;i<req.key_num;i++){
        char *key=req.key[i]->data;
        #if ENGINE==ENGINE_ARRAY
        ret=kv_array_del(&global_array,key);
        #elif ENGINE==ENGINE_HASH
        ret=KV_Map_del(&global_hash,key);
        #elif ENGINE==ENGINE_RBTREE
        ret=KV_Rbtree_del(&global_rbtree,key);
        #endif
        
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
}
int kv_cmd_exist(kv_request_t req,result*c){
    int ret;
    c->tlv_array=(TLV*)kv_malloc(c->tlv_num*sizeof(TLV));
    for(int i=0;i<req.key_num;i++){
        char *key=req.key[i]->data;
        #if ENGINE==ENGINE_ARRAY
        ret=kv_array_exist(&global_array,key);
        #elif ENGINE==ENGINE_HASH
        ret=KV_Map_exist(&global_hash,key);
        #elif ENGINE==ENGINE_RBTREE
        ret=KV_Rbtree_exist(&global_rbtree,key);
        #endif
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
}
//>0表示处理了多少字节，<0 非法包，=0 包不完整
//rlength这个包的长度
int kv_protocol(char *msg,int rlength,result *c){//一个包一个包的处理
    if(rlength<10){
        return 0;
    }
    int offset=0;
    int ret=0;
    kv_header_t header;
    kv_request_t req;
    memset(&header,0,sizeof(kv_header_t));
    ret=kv_header_decode(msg,rlength,&header);
    offset+=ret;
    uint8_t version=header.version;
    uint8_t msg_type=header.msg_type;
    uint16_t opcode=header.opcode;
    uint32_t len=header.length;//一条命令的数据长度,也是这个payload消息的消息边界
    // printf("rlength:%d\n",rlength);
    // printf("header.length=%d\n",len);
    if(header.magic!=KV_MAGIC){//数据是脏数据
        return -1;
    }
    if(rlength-10<len){//解析出头部，等包完整后才继续处理
        return 0;
    }
    kv_request_t_init(&req);
    ret=kv_payload_decode(msg+offset,len,&req);//解析协议，结果处理到req中，里面都是key,value
    offset+=ret;

    for(int i=0;i<req.key_num;i++){
            printf("key:%s,length:%d\n",req.key[i]->data,req.key[i]->length);
            printf("value:%s,length:%d\n",req.value[i]->data,req.value[i]->length);
    }
    if(version==KV_VERSION){//版本
        if(msg_type==MSG_TYPE_REQ){//协议类型
            int flag=0;
            int cmd;
            for(cmd=KV_CMD_START;cmd<KV_CMD_COUNT;cmd++){
                if(strcmp(command[opcode],command[cmd])==0){
                    flag=1;
                    printf("command:%s\n",command[cmd]);
                    break;
                    }
            }
            if(flag==0) {
                printf("命令输入错误\n");
                char buf[BUFFER_SIZE]="命令输入错误";
                c->tlv_num=1;
                c->tlv_array=(TLV*)kv_malloc(c->tlv_num*sizeof(TLV));
                c->tlv_array[0].tag=TAG_RES;
                c->tlv_array[0].type=TYPE_STRING;
                c->tlv_array[0].value=(char *)kv_malloc(strlen(buf)+1);
                c->tlv_array[0].length=strlen(buf)+1;
                memcpy(c->tlv_array[0].value,buf,c->tlv_array[0].length);
                exit(1);
            }
            c->tlv_num=req.key_num;
            //c->res.tlv_num=req.data_num;
            switch(cmd){
                case KV_CMD_SET:
                    kv_cmd_set(req,c);
                    break;
                case KV_CMD_GET:
                    kv_cmd_get(req,c);
                    break;
                case KV_CMD_MOD:
                    kv_cmd_mod(req,c);
                    break;
                case KV_CMD_DEL:
                    kv_cmd_del(req,c);
                    break;
                case KV_CMD_EXIST:
                    kv_cmd_exist(req,c);
                    break;
                default:
                    break;
            }
            kv_request_t_destory(&req);
            return offset;//处理完了一个包，一个包一个包的处理
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
int kv_request(char *rbuffer,char *wbuffer,int *rlength,int *wlength){//解包，协议编码，响应
    if((*rlength)<10){
        return 0;
    }
    int total_handled=0;
     while(1){//循环解包
        int ret;
        struct result res;
        memset(&res,0,sizeof(struct result));
        ret=kv_protocol(rbuffer,(*rlength),&res);
        printf("kv_protocol return %d\n",ret);
        if(ret==0){//非法包，包不完整
            break;
        }
        if(ret<0){
            return -1;
        }
        char tmp_buf[BUFFER_SIZE]={0};
        int res_ret=kv_response(tmp_buf,BUFFER_SIZE,&res);//响应先写到暂时的缓冲区中
        printf("kv_response return %d\n",res_ret);
        if((*wlength)+res_ret>BUFFER_SIZE){//发送缓冲区写不下了就不把新的响应加进来
            break;
        }
        memcpy(wbuffer+(*wlength),tmp_buf,res_ret);//然后追加到发送缓冲区中
        (*wlength)+=res_ret;
        for(int i=0;i<res.tlv_num;i++){
            kv_free(res.tlv_array[i].value);
        }
        kv_free(res.tlv_array);
        int remain=(*rlength)-ret;
        if(remain>=0){
            memmove(rbuffer,rbuffer+ret,remain);//缓冲区前移
            (*rlength)=remain;//二进制协议数据靠长度变量标记数据边界
        }
        total_handled+=ret;
    }
    return total_handled;
}
int kv_response(char *wbuffer,int len,result *c){
    int offset=0;
    int ret=0;
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
