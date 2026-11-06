#include"kvstore.h"
#include"kvproto.h"
#include"mempool.h"
#if AOF
#include"aof.h"
extern AOFcontext_ctx* ctx;
#endif
#if SYNC
#include"sync_new.h"
#include<sys/socket.h>
extern MasterMgr *mgr;
void sync(char *op,char *key,uint8_t *value,uint32_t value_size);
void KV_sync(char *rbuffer,int rlength,char *wbuffer,int wlength,int *cons,int *status);
#endif 

#include"adapt.h"
#include"tool.h"
#include"upload.h"
#include"resp.h"

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
static uint8_t* kv_cmd_execute(int fd,char *op,char *key,uint8_t *value,uint32_t value_len,uint32_t *res_len);
static void kv_response(int fd,char *wbuffer,int wlength,kvstore_t *ctx);
static void kv_request(int fd,char *rbuffer,int rlength,kvstore_t *ctx);
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
static uint8_t* kv_cmd_execute(int fd,char *op,char *key,uint8_t *value,uint32_t value_size,uint32_t *res_len){
    if(!op) return NULL;
    uint8_t *res=(uint8_t *)kv_malloc(128);
    memset(res,0,128);
    if(strcmp(op,"SET")==0){
        #if AOF
        aof_append(ctx,op,key,value,value_size,huge_buf,&huge_buf_len,&huge_num);
        #endif
        #if SYNC
        if(s_or_c==0) sync(op,key,value,value_size);
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
        #if SYNC
        if(s_or_c==0) sync(op,key,value,value_size);
        #endif
        (*res_len)=kv_cmd_del(key,res);
    }else if(strcmp(op,"MOD")==0){
        #if AOF
        aof_append(ctx,op,key,value,value_size,huge_buf,&huge_buf_len,&huge_num);
        #endif
        #if SYNC
        if(s_or_c==0) sync(op,key,value,value_size);
        #endif
        (*res_len)=kv_cmd_mod(key,value,value_size,res);
    }else if(strcmp(op,"EXIST")==0){
        (*res_len)=kv_cmd_exist(key,res);
    }
    else if (strcmp(op, "SET_CHUNK") == 0) {
        uint32_t total, offset;

        if (!key || !value || value_size < 9 || value[0] != TYPE_CHUNK) {
            res[0] = TYPE_STRING;
            (*res_len) = (uint32_t)(sprintf((char *)res + 1, "ERROR\r\n") + 2);
            return res;
        }

        memcpy(&total,  value + 1, 4);   /* 不能强转指针：value+1 非 4 字节对齐 */
        memcpy(&offset, value + 5, 4);
        total  = ntohl(total);
        offset = ntohl(offset);

        int r = kv_upload_feed(fd, key, total, offset, value + 9, value_size - 9);

        if (r == KV_UP_MORE) {           /* 中间分片：这一帧不回包 */
            kv_free(res);
            *res_len = 0;
            return NULL;
        }

        res[0] = TYPE_STRING;
        if      (r == KV_UP_OK)    (*res_len) = (uint32_t)(sprintf((char *)res + 1, "OK\r\n")    + 2);
        else if (r == KV_UP_EXIST) (*res_len) = (uint32_t)(sprintf((char *)res + 1, "EXIST\r\n") + 2);
        else                       (*res_len) = (uint32_t)(sprintf((char *)res + 1, "ERROR\r\n") + 2);
        return res;
    }
    else{
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
//处理一个完整帧
static void kv_request(int fd,char *rbuffer,int rlength,kvstore_t *ctx){
    RES res;//保存处理结果
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

    ctx->pending_bytes=0;
    for(int i=0;i<res.num;i++){
        uint32_t len=0;
        uint8_t *r=kv_cmd_execute(fd,res.op,res.key[i],res.value[i],res.value_len[i],&len);
        
        if(r&&len>0&&ctx->pending_bytes+len>KV_RESP_MAX_BYTES){
            /* 单连接待发响应超限：这条降级成小错误回包，避免多个大 value 叠加吃光内存 */
            kv_free(r);
            r=(uint8_t*)kv_malloc(16);
            len=0;
            if(r){
                r[0]=TYPE_STRING;
                len=(uint32_t)(sprintf((char*)r+1,"ERROR\r\n")+2);
            }
        }else if(r){
            ctx->pending_bytes+=len;
        }
        ctx->res[i]=r;
        ctx->res_len[i]=len;
    }
    ctx->num=res.num;
    kv_RES_t_destory(&res);
    ctx->status=OK;
    return;
}

static void kv_response(int fd, char *wbuffer, int wlength, kvstore_t *ctx)
{
    RES      res;
    uint8_t *vals[KEY_VLAUE_NUM_MAX];
    uint32_t vlen[KEY_VLAUE_NUM_MAX];
    uint32_t n = 0;
    uint32_t need;

    memset(&res, 0, sizeof(res));

    for (int i = 0; i < ctx->num && n < KEY_VLAUE_NUM_MAX; i++) {
        if (ctx->res[i] && ctx->res_len[i] > 0) {   /* 跳过中间分片的 NULL */
            vals[n] = ctx->res[i];
            vlen[n] = ctx->res_len[i];
            n++;
        }
    }
    if (n == 0) { ctx->cons_ = 0; return; }          /* 整帧没有回包 */

    /* 一帧装得下就照旧：9 字节头 + 每条 9 字节 TLV 开销 + 数据 */
    need = HEAD_LEN + 9u * n;
    for (uint32_t i = 0; i < n; i++) need += vlen[i];

    if (need <= (uint32_t)wlength) {
        res.op        = "RES";
        res.value     = vals;
        res.value_len = vlen;
        res.num       = n;

        KV_encode((uint8_t *)wbuffer, (uint32_t)wlength, &res);
        if (res.status == ERROR) {
            printf("res.error_info:%s\n", res.error_info);
            ctx->status = res.status;
            return;
        }
        ctx->cons_ = res.cons_;
        return;
    }

    /* 装不下：转响应队列，由网络层逐段拉取（大 value 会被切成 TYPE_CHUNK 分片） */
    if (kv_resp_begin(fd, ctx, (uint32_t)wlength) != 0) {
        ctx->status = ERROR;      /* 队列异常：让上层断开，避免连接半死 */
        return;
    }
    ctx->cons_ = (uint32_t)kv_resp_next(fd, (uint8_t *)wbuffer, (uint32_t)wlength);
}

//处理缓冲区的帧
int KV_server(int fd,char *rbuffer,int rlength,char *wbuffer,int wlength,int *cons,int *send){
    kvstore_t kv;
    memset(&kv,0,sizeof(kvstore_t));
    int r_offset=0;
    int w_offset=0;

    if(kv_resp_pending(fd)){   /* 上一批响应还没发完，网络层不应再调用 */
        (*cons)=0;
        (*send)=0;
        return 0;
    }

    while(r_offset<rlength){
        kv_request(fd,rbuffer+r_offset,rlength-r_offset,&kv);
        if(kv.status==ERROR) goto err;
        if(kv.status==WAIT) {
            (*send)=w_offset;
            (*cons)=r_offset;
            kvstore_t_destory(&kv);
            return 0;
        }
        r_offset+=kv.cons_;
        kv_response(fd,wbuffer+w_offset,wlength-w_offset,&kv);
        if(kv.status==ERROR) goto err;
        w_offset+=kv.cons_;
        kvstore_t_destory(&kv);
        memset(&kv,0,sizeof(kvstore_t));
        if(kv_resp_pending(fd)) break;   /* 响应还没发完，剩下的请求留给网络层下次喂 */
    }
    (*send)=w_offset;
    (*cons)=r_offset;
    return 0;
err:
    kvstore_t_destory(&kv);
    (*cons)=r_offset;   /* 已消费的字节交给网络层；坏包由调用方断开连接 */
    (*send)=w_offset;
    return -1;
    
}
#if SYNC
/* 追加一条记录到每个从节点的缓冲；缓冲放不下就先发掉缓冲，再直接发这条记录。
   记录格式与 AOF 完全一致：[int length][op\0][key\0][value]
   直接发送用 sync_new.c 里的 sync_send_all（内部处理非阻塞 EAGAIN）。 */
void sync(char *op,char *key,uint8_t *value,uint32_t value_size){
    if(!mgr||!mgr->slave_head||!op||!key) return;

    int op_len=strlen(op)+1;
    int key_len=strlen(key)+1;
    int64_t length64=(int64_t)op_len+key_len+(int64_t)value_size;
    if(length64>2147483647LL) return;
    int length=(int)length64;
    int need=(int)sizeof(int)+length;

    Slaveconn *p=mgr->slave_head->next;
    while(p){
        Slaveconn *next=p->next;   /* 可能把当前节点摘掉 */

        if(p->fd>=0){
            if(p->swlength+need<=SYNC_BUF_SIZE){
                char *b=p->sbuf+p->swlength;
                memcpy(b,&length,sizeof(int)); b+=sizeof(int);
                memcpy(b,op,op_len);           b+=op_len;
                memcpy(b,key,key_len);         b+=key_len;
                if(value_size>0) memcpy(b,value,value_size);
                p->swlength+=need;
            }else{
                int bad=0;
                if(p->swlength>0){
                    if(sync_send_all(p->fd,p->sbuf,p->swlength)!=0) bad=1;
                    p->swlength=0;
                }
                if(!bad){
                    if(sync_send_all(p->fd,(char*)&length,sizeof(int))!=0 ||
                       sync_send_all(p->fd,op,op_len)!=0 ||
                       sync_send_all(p->fd,key,key_len)!=0 ||
                       (value_size>0&&sync_send_all(p->fd,(char*)value,(int)value_size)!=0)){
                        bad=1;
                    }
                }
                if(bad){
                    slave_close(mgr,p->fd);   /* 会释放 p；next 已经保存 */
                }
            }
        }
        p=next;
    }
}

/* 缓冲没满时手动把数据推给从节点（退出前也会调用） */
void sync_flush(void){
    if(!mgr||!mgr->slave_head) return;

    Slaveconn *p=mgr->slave_head->next;
    while(p){
        Slaveconn *next=p->next;
        if(p->fd>=0&&p->swlength>0){
            if(sync_send_all(p->fd,p->sbuf,p->swlength)!=0){
                slave_close(mgr,p->fd);
            }else{
                p->swlength=0;
            }
        }
        p=next;
    }
}

/* 从节点解析复制流；大 value 的记录会跨多次 recv，这里做重组。
   一个进程只有一个主节点连接，所以用文件级状态。 */
static char *pending_buf=NULL;
static int   pending_need=0;
static int   pending_got=0;

void KV_sync_reset(void){
    if(pending_buf){ kv_free(pending_buf); pending_buf=NULL; }
    pending_need=0;
    pending_got=0;
}

void KV_sync(char *rbuffer,int rlength,char *wbuffer,int wlength,int *cons,int *status){
    int total=0;

    (void)wbuffer; (void)wlength;
    *status=0;

    while(total<rlength){
        /* 1) 没有待收记录：读 4 字节长度头 */
        if(pending_need==0){
            int length;
            if(rlength-total<(int)sizeof(int)){
                break;                       /* 长度头都还没齐 */
            }
            memcpy(&length,rbuffer+total,sizeof(int));
            total+=sizeof(int);
            if(length<=0||(uint32_t)length>SYNC_MAX_RECORD){
                *cons=total;
                *status=2;                   /* 坏数据，调用方断开 */
                return;
            }
            pending_buf=(char*)kv_malloc(length);
            if(!pending_buf){
                *cons=total;
                *status=2;
                return;
            }
            pending_need=length;
            pending_got=0;
        }

        /* 2) 尽量把记录填满 */
        {
            int avail=rlength-total;
            int want=pending_need-pending_got;
            int take=avail<want?avail:want;
            memcpy(pending_buf+pending_got,rbuffer+total,take);
            pending_got+=take;
            total+=take;
        }
        if(pending_got<pending_need){
            break;                           /* 还没收齐 */
        }

        /* 3) 整条记录到齐：解析并执行 */
        {
            int length=pending_need;
            char *pkt=pending_buf;
            int off=0;
            int op_len=strnlen(pkt+off,length-off);
            if(op_len<length-off){
                char *op=pkt+off;
                off+=op_len+1;
                int key_len=strnlen(pkt+off,length-off);
                if(key_len<length-off){
                    char *key=pkt+off;
                    off+=key_len+1;
                    kv_execute(op,key,(uint8_t*)pkt+off,(uint32_t)(length-off));
                }
            }
            kv_free(pending_buf);
            pending_buf=NULL;
            pending_need=0;
            pending_got=0;
        }
    }

    *cons=total;
    *status=(pending_need>0)?1:0;
}
#endif





