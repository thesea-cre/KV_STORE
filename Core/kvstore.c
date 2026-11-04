#include"kvstore.h"
#include"kvproto.h"
#include"mempool.h"
#if AOF
#include"aof.h"
extern AOFcontext_ctx* ctx;
#endif
#if SYNC
#include"sync_new.h"
extern MasterMgr *mgr;
void sync(char *op,char *key,uint8_t *value,uint32_t value_size);
void KV_sync(char *rbuffer,int rlength,char *wbuffer,int wlength,int *cons,int *status);
#endif 

#include"adapt.h"
#include"tool.h"

extern int s_or_c;

#if AOF
char huge_buf[64*1024]={0};
int huge_buf_len=0;
int huge_num=0;
#endif
static int kv_cmd_set(char *key,uint8_t *value,uint32_t value_size,uint8_t *res);
static int kv_cmd_get(char *key,uint8_t *value,uint32_t *value_size,char *res);
static int kv_cmd_mod(char *key,uint8_t *value,uint32_t value_size,char *res);
static int kv_cmd_del(char *key,char *res);
static int kv_cmd_eixst(char *key,char *res);
static uint8_t* kv_cmd_execute(char *op,char *key,uint8_t *value,uint32_t value_len,uint32_t *res_len);
static void kv_response(char *wbuffer,int wlength,kvstore_t *ctx);
static void kv_request(char *rbuffer,int rlength,kvstore_t *ctx);
static void kvstore_t_destory(kvstore_t *ctx);

static int kv_cmd_set(char *key,uint8_t *value,uint32_t value_size,uint8_t *res){
    if(!key||!value||!res) return -2;
    int ret=kv_set(key,value,value_size);
    if(ret==0){
        res[0]=TYPE_STRING;
        ret=sprintf(res+1,"OK\r\n")+2;
    }else if(ret>0){
        res[0]=TYPE_STRING;
        ret=sprintf(res+1,"EXIST\r\n")+2;
    }else{
        res[0]=TYPE_STRING;
        ret=sprintf(res+1,"ERROR\r\n")+2;
    }
    return ret;
}

static int kv_cmd_get(char *key,uint8_t *value,uint32_t *value_size,char *res){
    if(!key) return -2;
    int ret=kv_get(key,value,value_size);
    if(ret==0){
        ret=0;
    }else{
        res[0]=TYPE_STRING;
        ret=sprintf(res+1,"NOT EXIST\r\n")+2;
    }
    return ret;
}
static int kv_cmd_mod(char *key,uint8_t *value,uint32_t value_size,char *res){
    if(!key||!value) return -2;
    int ret=kv_mod(key,value,value_size);
    if(ret==0){
        res[0]=TYPE_STRING;
        ret=sprintf(res+1,"OK\r\n")+2;
    }else if(ret>0){
        res[0]=TYPE_STRING;
        ret=sprintf(res+1,"NOT EXIST\r\n")+2;
    }else{
        res[0]=TYPE_STRING;
        ret=sprintf(res+1,"ERROR\r\n")+2;
    }
    return ret;
}

static int kv_cmd_del(char *key,char *res){
    if(!key) return -2;
    int ret=kv_del(key);
    if(ret==0){
        res[0]=TYPE_STRING;
        ret=sprintf(res+1,"OK\r\n")+2;
    }else if(ret>0){
        res[0]=TYPE_STRING;
        ret=sprintf(res+1,"NOT EXIST\r\n")+2;
    }else{
        res[0]=TYPE_STRING;
        ret=sprintf(res+1,"ERROR\r\n")+2;
    }
    return ret;
}
static int kv_cmd_exist(char *key,char *res){
    if(!key) return -2;
    int ret=kv_exist(key);
    if(ret==0){
        res[0]=TYPE_STRING;
        ret=sprintf(res+1,"NOT EXIST\r\n")+2;
    }else if(ret>0){
        res[0]=TYPE_STRING;
        ret=sprintf(res+1,"EXIST\r\n")+2;
    }else{
        res[0]=TYPE_STRING;
        ret=sprintf(res+1,"ERROR\r\n")+2;
    }
    return ret;
}

#define RES_LEN_MAX 128
//数据从buf收上来到去到引擎层，经历两次copy，一次是从buf到RES中，然后第二次是从RES copy到引擎层中
static void kv_execute(char *op,char *key,uint8_t *value,uint32_t value_size){
    if(!op) return;
    
    if(strcmp(op,"SET")==0){
        #if AOF
        aof_append(ctx,op,key,value,value_size,huge_buf,&huge_buf_len,&huge_num);
        #endif
        kv_set(key,value,value_size);
    }else if(strcmp(op,"GET")==0){
        uint8_t  *v=NULL;
        uint32_t v_len=0;
        kv_get(key,&v,&v_len);
    }else if(strcmp(op,"DEL")==0){
        #if AOF
        aof_append(ctx,op,key,value,value_size,huge_buf,&huge_buf_len,&huge_num);
        #endif
        kv_del(key);
    }else if(strcmp(op,"MOD")==0){
        #if AOF
        aof_append(ctx,op,key,value,value_size,huge_buf,&huge_buf_len,&huge_num);
        #endif
        kv_mod(key,value,value_size);
    }else if(strcmp(op,"EXIST")==0){
        kv_exist(key);
    }else{
        return;
    }
    return;
}
static uint8_t* kv_cmd_execute(char *op,char *key,uint8_t *value,uint32_t value_size,uint32_t *res_len){
    if(!op) return NULL;
    uint8_t *res=(uint8_t *)kv_malloc(128);
    memset(res,0,128);
    if(strcmp(op,"SET")==0){
        #if AOF
        aof_append(ctx,op,key,value,value_size,huge_buf,&huge_buf_len,&huge_num);
        #endif
        #if (SYNC&&s_or_c==0)
        sync(op,key,value,value_size);
        #endif
        (*res_len)=kv_cmd_set(key,value,value_size,res);
    }else if(strcmp(op,"GET")==0){
        uint8_t  *v=NULL;
        uint32_t v_len=0;
        int ret=kv_cmd_get(key,&v,&v_len,res);
        if(ret==0){
            kv_free(res);
            uint8_t *value_=(uint8_t*)kv_malloc(v_len);
            memcpy(value_,v,v_len);
            (*res_len)=v_len;
            return value_;
        }
        (*res_len)=ret;
    }else if(strcmp(op,"DEL")==0){
        #if AOF
        aof_append(ctx,op,key,value,value_size,huge_buf,&huge_buf_len,&huge_num);
        #endif
        #if (SYNC&&s_or_c==0)
        sync(op,key,value,value_size);
        #endif
        (*res_len)=kv_cmd_del(key,res);
    }else if(strcmp(op,"MOD")==0){
        #if AOF
        aof_append(ctx,op,key,value,value_size,huge_buf,&huge_buf_len,&huge_num);
        #endif
        #if (SYNC&&s_or_c==0)
        sync(op,key,value,value_size);
        #endif
        (*res_len)=kv_cmd_mod(key,value,value_size,res);
    }else if(strcmp(op,"EXIST")==0){
        (*res_len)=kv_cmd_exist(key,res);
    }else{
        res[0]=TYPE_STRING;
        (*res_len)=sprintf(res+1,"CMD ERROR\r\n")+2;
    }
    return res;
}
static void kvstore_t_destory(kvstore_t *ctx){
    for(int i=0;i<ctx->num;i++){
        kv_free(ctx->res[i]);
    }
    kv_free(ctx->res);
    kv_free(ctx->res_len);
}

static void kv_request(char *rbuffer,int rlength,kvstore_t *ctx){
    RES res;
    memset(&res,0,sizeof(RES));
    KV_decode((uint8_t*)rbuffer,rlength,&res);
    ctx->cons_=res.cons_; 
    if(res.status==ERROR){//包不合法
        printf("error_info:%s\n",res.error_info);
        ctx->status=ERROR;
        return;
    }
    if(res.status==WAIT){
        ctx->status=WAIT;
        return;
    }
    ctx->status=res.status;
    ctx->res=(uint8_t **)kv_malloc(res.num*sizeof(uint8_t *));
    ctx->res_len=(uint32_t*)kv_malloc(res.num*sizeof(uint32_t));

    for(int i=0;i<res.num;i++){
        ctx->res[i]=kv_cmd_execute(res.op,res.key[i],res.value[i],res.value_len[i],&ctx->res_len[i]);//浅拷贝，外层只需要free(res);
    }
    ctx->num=res.num;
    kv_RES_t_destory(&res);
    ctx->status=OK;
    return;
}
static void kv_response(char *wbuffer,int wlength,kvstore_t *ctx){
    RES res;
    memset(&res,0,sizeof(RES));
    res.op="RES";
    res.value=ctx->res;
    res.num=ctx->num;
    res.value_len=ctx->res_len;
    KV_encode((uint8_t*)wbuffer,(uint32_t)wlength,&res);
    if(res.status==ERROR){
        printf("res.error_info:%s\n",res.error_info);
        ctx->status=res.status;
        return;
    }
    ctx->cons_=res.cons_;
    // kv_RES_t_destory(&res);
    return;
}
//网络层只关心消费了几个字节，数据要不要，下一步干什么
//
void KV_server(char *rbuffer,int rlength,char *wbuffer,int wlength,int *cons,int *send){
    kvstore_t kv;
    memset(&kv,0,sizeof(kvstore_t));
    int r_offset=0;
    int w_offset=0;
    while(r_offset<rlength){
        kv_request(rbuffer+r_offset,rlength-r_offset,&kv);
        if(kv.status==ERROR) goto err;
        if(kv.status==WAIT) {
            (*send)=w_offset;
            (*cons)=r_offset;
            kvstore_t_destory(&kv);
            return;
        }
        r_offset+=kv.cons_;
        kv_response(wbuffer+w_offset,wlength-w_offset,&kv);
        if(kv.status==ERROR) goto err;
        w_offset+=kv.cons_;
        kvstore_t_destory(&kv);
        memset(&kv,0,sizeof(kvstore_t));
    }
    (*send)=w_offset;
    (*cons)=r_offset;
    return;
err:
    kvstore_t_destory(&kv);
    return;
    
}
#if SYNC
void sync(char *op,char *key,uint8_t *value,uint32_t value_size){
    Slaveconn* p=mgr->slave_head->next;
    if(!p) return;
    while(p){
        int offset=0;
        int op_len=strlen(op)+1;
        int key_len=strlen(key)+1;
        int length=op_len+key_len+value_size;
        if(BUFFER_SIZE-(*p->wlength)<length){
            int set=send(p->fd,p->wbuffer,*p->wlength,0);
            *p->wlength-=set;
        }
        memcpy(p->wbuffer+offset+(*p->wlength),&length,sizeof(int));
        offset+=sizeof(int);
        memcpy(p->wbuffer+offset+(*p->wlength),op,op_len);
        offset+=op_len;
        memcpy(p->wbuffer+offset+(*p->wlength),key,key_len);
        offset+=key_len;
        memcpy(p->wbuffer+offset+(*p->wlength),value,value_size);
        offset+=value_size;
        (*(p->wlength))+=offset;
        //notify(p->fd);
        
        p=p->next;
    }
    return;
}
void KV_sync(char *rbuffer,int rlength,char *wbuffer,int wlength,int *cons,int *status){
    int total_cons=0;
    while(total_cons<rlength){
        char *op=NULL;
        char *key=NULL;
        char *value=NULL;
        uint32_t value_len=0;
        int length=0;
        int offset=0;
        if(rlength-total_cons<sizeof(int)){
            (*cons)=total_cons;
            (*status)=1;
            return;
        } 
        memcpy(&length,rbuffer+total_cons+offset,sizeof(int));
        offset+=sizeof(int);
        if(rlength-total_cons<length){
            (*cons)=total_cons;
            (*status)=1;
            return;
        }
        int op_len=strnlen(rbuffer+total_cons+offset,rlength-offset)+1;
        op=(char *)kv_malloc(op_len);
        memcpy(op,rbuffer+total_cons+offset,op_len);
        offset+=op_len;
        int key_len=strnlen(rbuffer+total_cons+offset,rlength-offset)+1;
        key=(char *)kv_malloc(key_len);
        memcpy(key,rbuffer+total_cons+offset,key_len);
        offset+=key_len;
        value_len=length-op_len-key_len;
        if(value_len>0){
            value=(char *)kv_malloc(value_len);
            memcpy(value,rbuffer+total_cons+offset,value_len);
            offset+=value_len;
        }
        kv_execute(op,key,value,value_len);
        kv_free(op);
        kv_free(key);
        kv_free(value);
        total_cons+=offset;
    }
    (*cons)=total_cons;
    (*status)=0;
    return;
}
#endif





