#include<stdlib.h>
#include<stdio.h>
#include<string.h>
#include"aof.h"
#include"adapt.h"
#include<stdint.h>
#include<fcntl.h>
#include <unistd.h>     // write、read、close、lseek、fsync
#include <fcntl.h>      // open、O_* 宏
#include <sys/stat.h>   // ftruncate、文件权限宏
#include <sys/types.h>  // size_t、ssize_t、off_t 等基础类型
#include<sys/uio.h>
static int read_full(int fd,char* buf,uint32_t len);
static int write_full(int fd,char *buf,uint32_t len);
static int aof_st_write(AOFcontext_ctx *ctx);
static void* kv_malloc(int size){
    return malloc(size);
}
static void kv_free(void* ptr){
    return free(ptr);
}
AOFcontext_ctx *aof_init(const char *db_file_path,const char *st_file_path){
    
    if(!db_file_path||!st_file_path) return NULL;
    AOFcontext_ctx * ctx=(AOFcontext_ctx*)kv_malloc(sizeof(AOFcontext_ctx));
    if(!ctx) return NULL;
    memset(ctx,0,sizeof(AOFcontext_ctx));
    ctx->db_fd=-1;
    ctx->st_fd=-1;

    ctx->db_fd=open(db_file_path,O_RDWR|O_CREAT,0664);
    if(ctx->db_fd<0){
        perror("aof_init open failed");
        kv_free(ctx);
        return NULL;
    }
    ctx->st_fd=open(st_file_path,O_RDWR|O_CREAT,0664);
    if(ctx->st_fd<0){
        perror("aof_init open failed");
        close(ctx->db_fd);
        kv_free(ctx);
        return NULL;
    }

    /* st 文件：20 字节=新格式；8 字节=旧格式（迁移）；其他=新建 */
    struct stat st;
    if(fstat(ctx->st_fd,&st)!=0) st.st_size=0;

    if(st.st_size==AOF_ST_LEN){
        unsigned char buf[AOF_ST_LEN];
        if(read_full(ctx->st_fd,(char*)buf,AOF_ST_LEN)==AOF_ST_LEN){
            uint32_t magic,ver,count;
            uint64_t size;
            memcpy(&magic,buf+0,4);
            memcpy(&ver,buf+4,4);
            memcpy(&size,buf+8,8);
            memcpy(&count,buf+16,4);
            if(magic==AOF_ST_MAGIC&&ver==AOF_ST_VERSION){
                ctx->size=(off_t)size;
                ctx->current_count=(int)count;
            }
        }
    }else if(st.st_size==8){
        int size32=0,count32=0;
        if(read_full(ctx->st_fd,(char*)&size32,4)==4&&
           read_full(ctx->st_fd,(char*)&count32,4)==4){
            ctx->size=(off_t)size32;
            ctx->current_count=count32;
        }
        aof_st_write(ctx);   /* 旧格式迁移成新格式 */
    }else{
        ctx->size=0;
        ctx->current_count=0;
        aof_st_write(ctx);
    }

    /* 恢复从文件头顺序读，读完位置自然在末尾 */
    lseek(ctx->db_fd,0,SEEK_SET);
    return ctx;
}

static int write_full(int fd,char *buf,uint32_t len){
    if(!buf||len==0) return ERROR;
    int offset=0;
    while(offset<len){
        int n=write(fd,buf+offset,len-offset);
        if(n<0) return ERROR;
        if(n==0) return offset;
        offset+=n;
    }
    return offset;
}

/* writev 可能部分写（大 value 落盘时尤其要考虑），循环补齐 */
static int writev_full(int fd, struct iovec *iov, int iovcnt){
    int total=0;
    int idx=0;
    while(idx<iovcnt){
        ssize_t n=writev(fd,iov+idx,iovcnt-idx);
        if(n<0) return ERROR;
        if(n==0) break;
        total+=(int)n;
        int remain=(int)n;
        while(idx<iovcnt&&remain>=(int)iov[idx].iov_len){
            remain-=(int)iov[idx].iov_len;
            idx++;
        }
        if(idx<iovcnt&&remain>0){
            iov[idx].iov_base=(char*)iov[idx].iov_base+remain;
            iov[idx].iov_len-=(size_t)remain;
        }
    }
    return total;
}

/* 写状态文件：magic + version + size(u64) + count(u32) */
static int aof_st_write(AOFcontext_ctx *ctx){
    unsigned char buf[AOF_ST_LEN];
    uint32_t magic=AOF_ST_MAGIC;
    uint32_t ver=AOF_ST_VERSION;
    uint64_t size;
    uint32_t count;

    if(!ctx||ctx->st_fd<0) return ERROR;

    size=(uint64_t)ctx->size;
    count=(uint32_t)ctx->current_count;

    memcpy(buf+0,&magic,4);
    memcpy(buf+4,&ver,4);
    memcpy(buf+8,&size,8);
    memcpy(buf+16,&count,4);

    if(lseek(ctx->st_fd,0,SEEK_SET)<0) return ERROR;
    return (write_full(ctx->st_fd,(char*)buf,AOF_ST_LEN)==AOF_ST_LEN)?OK:ERROR;
}

void aof_rewrite(AOFcontext_ctx* ctx){
    if(!ctx) return;

    int count=kv_count();
    if(count<=0){
        /* 空库：清空 db，状态归零（原来 write(fd, 0, ...) 传的是 NULL 指针） */
        if(ftruncate(ctx->db_fd,0)!=0) perror("aof_rewrite ftruncate");
        lseek(ctx->db_fd,0,SEEK_SET);
        ctx->size=0;
        ctx->current_count=0;
        aof_st_write(ctx);
        return;
    }

    char **key=(char **)kv_malloc(count*sizeof(char*));
    char **value=(char **)kv_malloc(count*sizeof(char*));
    uint32_t *value_len=(uint32_t *)kv_malloc(count*sizeof(uint32_t));
    if(!key||!value||!value_len){
        kv_free(key); kv_free(value); kv_free(value_len);
        return;
    }
    memset(key,0,count*sizeof(char*));
    memset(value,0,count*sizeof(char*));
    memset(value_len,0,count*sizeof(uint32_t));

    kv_tra(key,value,value_len);

    if(ftruncate(ctx->db_fd,0)!=0) perror("aof_rewrite ftruncate");
    lseek(ctx->db_fd,0,SEEK_SET);

    char *op="SET";
    int op_len=strlen(op)+1;
    off_t offset=0;
    int written=0;

    for(int i=0;i<count;i++){
        if(!key[i]||!value[i]) continue;   /* 引擎遍历可能留下空洞，跳过 */
        int key_len=strlen(key[i])+1;
        int length=op_len+key_len+(int)value_len[i];
        struct iovec iov[4];
        int iovcnt=0;

        iov[iovcnt].iov_base=&length;
        iov[iovcnt].iov_len=sizeof(int);
        iovcnt++;

        iov[iovcnt].iov_base=op;
        iov[iovcnt].iov_len=op_len;
        iovcnt++;

        iov[iovcnt].iov_base=key[i];
        iov[iovcnt].iov_len=key_len;
        iovcnt++;

        iov[iovcnt].iov_base=value[i];
        iov[iovcnt].iov_len=value_len[i];
        iovcnt++;

        int n=writev_full(ctx->db_fd,iov,iovcnt);
        if(n<0){
            perror("aof_rewrite writev");
            break;
        }
        offset+=n;
        written++;
    }

    ctx->size=offset;
    ctx->current_count=written;
    aof_st_write(ctx);

    kv_free(key);
    kv_free(value);
    kv_free(value_len);
    return;
}

int aof_append(AOFcontext_ctx* ctx,char *op,char *key,char *value,uint32_t value_len,char *huge_buf,int *huge_len,int *current_num){
    if(!ctx||!op||!key){
        return -1;
    }
    /* 重写策略：命令数超过阈值，且超过活 key 数的 2 倍才重写。
       否则 key 数 >100 时，current_count 被重写成 key 数，会变成每条命令都全库重写 */
    if(ctx->current_count>MAX_COUNT){
        int live=kv_count();
        if(live<=0||ctx->current_count>live*2){
            aof_rewrite(ctx);
        }
    }
    int offset = 0;
    int op_len = strlen(op) + 1;
    int key_len = strlen(key) + 1;
    int64_t length64 = (int64_t)op_len + key_len + (int64_t)value_len;
    if(length64 > 2147483647LL) return -1;   /* 单条记录长度溢出 */
    int length = (int)length64;
    if((length+sizeof(int))<(4096 - (*huge_len))){
        memcpy(huge_buf+(*huge_len),&length,sizeof(int));
        offset+=sizeof(int);
        memcpy(huge_buf+ (*huge_len) +offset,op,op_len);
        offset+=op_len;
        memcpy(huge_buf+(*huge_len) +offset,key,key_len);
        offset+=key_len;
        if(value){
            memcpy(huge_buf+(*huge_len) + offset,value,value_len);
            offset+=value_len;
        }
        (*current_num)++;
        (*huge_len)+=offset;
        return 0;
    }
    if(*huge_len>0){
        int n=write_full(ctx->db_fd,huge_buf,(*huge_len));//先写旧数据
        if(n!=*huge_len){
            perror("aof_append flush");
            return -1;
        }
        ctx->size+=n;
    }
    struct iovec iov[4];
    int iovcnt = 0;
    iov [ iovcnt ].iov_base = &length;
    iov [ iovcnt ].iov_len = sizeof(int);
    iovcnt++;
    
    iov [ iovcnt ].iov_base = op;
    iov [ iovcnt ].iov_len = op_len;
    iovcnt++;

    iov [ iovcnt ].iov_base = key;
    iov [ iovcnt ].iov_len = key_len;
    iovcnt++;

    if(value&&value_len>0){
        iov [ iovcnt ].iov_base = value;
        iov [ iovcnt ].iov_len = value_len;
        iovcnt++;
    }
    int n=writev_full(ctx->db_fd,iov,iovcnt);
    if(n<0){
        perror("aof_append writev");
        return -1;
    }
    ctx->size+=n;

    (*huge_len)= 0;
    ctx->current_count+=((*current_num)+1);
    (*current_num)=0;

    aof_st_write(ctx);
    return 0;

    // int  total_write=strlen(op)+1+strlen(key)+1+value_len;
    // struct iovec iov[4];
    // int iovcnt = 0;
    // iov [ iovcnt ].iov_base = &total_write;
    // iov [ iovcnt ].iov_len = sizeof(int);
    // iovcnt++;
     
    // iov [ iovcnt ].iov_base = op;
    // iov [ iovcnt ].iov_len = strlen(op) + 1;
    // iovcnt++;

    // iov [ iovcnt ].iov_base = key;
    // iov [ iovcnt ].iov_len = strlen(key) + 1;
    // iovcnt++;

    // if(value&&value_len>0){
    //     iov [ iovcnt ].iov_base = value;
    //     iov [ iovcnt ].iov_len = value_len;
    //     iovcnt++;
    // }

    // int  w=write(ctx->db_fd,&total_write,sizeof(int));
    // if(w<0){
    //     perror("aof_append write failed");
    //     return -1;
    // }
    // w=write(ctx->db_fd,op,strlen(op)+1);
    // if(w<0){
    //     perror("aof_append write failed");
    //     return -1;
    // }
    // w=write(ctx->db_fd,key,strlen(key)+1);
    // if(w<0){
    //     perror("aof_append write failed");
    //     return -1;
    // }
    // if(value&&value_len>0){
    //     w=write(ctx->db_fd,value,value_len);
    //     if(w<0){
    //         perror("aof_append write failed");
    //         return -1;
    //     }
    // }
    // writev(ctx->db_fd,iov,iovcnt);
    
}


static int read_full(int fd,char* buf,uint32_t len){
    uint32_t offset=0;
    while(offset<len){
        int n=read(fd,buf+offset,len-offset);
        if(n<0){
            return -1;
        }
        if(n==0) return offset;
        offset+=n;
    }
    return offset;
}
static void kv_aof_execute(char *op,char *key,uint8_t *value,uint32_t value_len){
    if(!op||!key) return;
    if(strcmp(op,"SET")==0){
        if(!value||value_len==0) return;
        kv_set(key,value,value_len);
    }else if(strcmp(op,"DEL")==0){
        kv_del(key);
    }else if(strcmp(op,"MOD")==0){
        if(!value||value_len==0) return;
        kv_mod(key,value,value_len);
    }else{
        return;
    }
    return;
}
int aof_recover(AOFcontext_ctx* ctx){
    if(!ctx||ctx->db_fd<0) return ERROR;

    if(lseek(ctx->db_fd,0,SEEK_SET)<0) return ERROR;
    ctx->size=0;
    ctx->current_count=0;

    for(;;){
        int length=0;
        int n=read_full(ctx->db_fd,(char*)&length,sizeof(int));
        if(n==0) break;                     /* 正常读完 */
        if(n!=(int)sizeof(int)) break;      /* 尾部残缺的包头：丢掉 */
        if(length<=0||(uint32_t)length>AOF_MAX_RECORD){
            fprintf(stderr,"aof_recover: bad record length %d, stop\n",length);
            break;
        }

        char *pkt=(char *)kv_malloc(length);
        if(!pkt) break;
        n=read_full(ctx->db_fd,pkt,length);
        if(n!=length){                      /* 尾包没写完：丢掉 */
            kv_free(pkt);
            break;
        }

        int offset=0;
        int rem=length-offset;
        int op_len=strnlen(pkt+offset,rem);
        if(op_len>=rem){ kv_free(pkt); break; }
        op_len+=1;
        char *op=pkt+offset;
        offset+=op_len;

        rem=length-offset;
        int key_len=strnlen(pkt+offset,rem);
        if(key_len>=rem){ kv_free(pkt); break; }
        key_len+=1;
        char *key=pkt+offset;
        offset+=key_len;

        uint8_t *value=(uint8_t*)pkt+offset;
        uint32_t value_len=(uint32_t)(length-offset);

        kv_aof_execute(op,key,value,value_len);

        ctx->size += (off_t)(sizeof(int)+length);
        ctx->current_count++;
        kv_free(pkt);
    }

    /* 尾部有残缺记录：把文件截断到已恢复位置，避免下次又读到 */
    if(lseek(ctx->db_fd,0,SEEK_END)!=ctx->size){
        if(ftruncate(ctx->db_fd,ctx->size)==0) lseek(ctx->db_fd,0,SEEK_END);
    }
    lseek(ctx->db_fd,0,SEEK_END);
    aof_st_write(ctx);
    return OK;
}
void aof_close(AOFcontext_ctx* ctx){
    if (ctx->db_fd >= 0) {
        fsync(ctx->db_fd); // 关闭前强制刷盘
        close(ctx->db_fd);
        ctx->db_fd = -1;
        fsync(ctx->st_fd); // 关闭前强制刷盘
        close(ctx->st_fd);
        ctx->st_fd = -1;
    }
    kv_free(ctx);
}
void force_flush(AOFcontext_ctx *ctx, char *huge_buf, int *huge_len, int *current_num) {
    if(!ctx||!huge_buf||!huge_len||!current_num) return;
    if(*huge_len<=0) return;

    int n=write_full(ctx->db_fd,huge_buf,*huge_len);
    if(n!=*huge_len){
        perror("force_flush");
        return;
    }
    ctx->size+=n;
    ctx->current_count+=*current_num;

    aof_st_write(ctx);

    *huge_len=0;
    *current_num=0;
}


// void Task_queue_init(Task_* queue){
//     pthread_mutex_init(&queue->task_mutex);
//     queue->head=(AOF_task*)malloc(sizeof(AOF_task));
//     memset(queue->head,0,sizeof(AOF_task));
//     queue -> tail = (AOF_task*) malloc(sizeof(AOF_task));
//     memset(queue->tail,0,sizeof(AOF_task));
//     queue->task_num = 0;
//     pthread_cond_init(&queue->cond_,NULL);
// }
// void Task_queue_destory(Task_* queue){
//     pthread_mutexattr_destroy(&queue->task_mutex);
//     free(queue->head);
//     free(queue->tail);
//     queue->task_num=0;
//     pthread_cond_destroy(&queue->cond_);
// }
// AOF_task* pop_(Task_* queue){
//     if(queue->task_num==0){
//         pthread_cond_wait()
//     }
// }
