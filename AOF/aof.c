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

    ctx->db_fd=open(db_file_path,O_RDWR|O_CREAT,0664);
    if(ctx->db_fd<0){
        perror("aof_init open failed");
        return NULL;
    }
    ctx->st_fd=open(st_file_path,O_RDWR|O_CREAT,0664);
    if(ctx->st_fd<0){
        perror("aof_init open failed");
        return NULL;
    }
    int n = read(ctx->st_fd, &ctx->size, sizeof(int));
    if (n != sizeof(int)) {
        ctx->size = 0; // 新文件或读取失败
        // 如果是新文件，为了保证后续 lseek 写入正常，最好把初始状态写进去
        lseek(ctx->st_fd, 0, SEEK_SET);
        write(ctx->st_fd, &ctx->size, sizeof(int));
        write(ctx->st_fd, &ctx->current_count, sizeof(int));
    } else {
        read(ctx->st_fd, &ctx->current_count, sizeof(int));
    }
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

void aof_rewrite(AOFcontext_ctx* ctx){
    if(!ctx) return;
    ctx->current_count=kv_count();
    if(ctx->current_count==0){
        lseek(ctx->st_fd, 0, SEEK_SET);
        write(ctx->st_fd, 0, sizeof(int));
        write(ctx->st_fd, 0, sizeof(int));

        ftruncate(ctx->db_fd,0);
        lseek(ctx->db_fd,0,SEEK_SET);
        return;

    }
    //printf("触发重写！当前引擎里的 key 数量为: %d\n", ctx->current_count);
    char **key=(char **)kv_malloc(ctx->current_count*sizeof(char*));
    char **value=(char **)kv_malloc(ctx->current_count*sizeof(char *));
    uint32_t *value_len=(uint32_t *)kv_malloc(ctx->current_count*sizeof(uint32_t));

    kv_tra(key,value,value_len);
    char *op="SET";
    ftruncate(ctx->db_fd,0);
    lseek(ctx->db_fd,0,SEEK_SET);

    int offset=0;
    for(int i=0;i<ctx->current_count;i++){
        int length=strlen(op)+1+strlen(key[i])+1+value_len[i];
        struct iovec iov[4];
        int iovcnt = 0;
        iov [ iovcnt ].iov_base = &length;
        iov [ iovcnt ].iov_len = sizeof(int);
        iovcnt++;
        
        iov [ iovcnt ].iov_base = op;
        iov [ iovcnt ].iov_len = strlen(op) + 1;
        iovcnt++;

        iov [ iovcnt ].iov_base = key[i];
        iov [ iovcnt ].iov_len = strlen(key[i]) + 1;
        iovcnt++;

        
        iov [ iovcnt ].iov_base = value[i];
        iov [ iovcnt ].iov_len = value_len[i];
        iovcnt++;
        int n=writev(ctx->db_fd,iov,iovcnt);
        offset+=n;
        // int n=write_full(ctx->db_fd,&length,sizeof(int));
        // offset+=n;
        // n=write_full(ctx->db_fd,op,strlen(op)+1);
        // offset+=n;
        // n=write_full(ctx->db_fd,key[i],strlen(key[i])+1);
        // offset+=n;
        // n=write_full(ctx->db_fd,value[i],value_len[i]);
        // offset+=n;

    }
    ctx->size=offset;
    lseek(ctx->st_fd, 0, SEEK_SET);
    write(ctx->st_fd, &ctx->size, sizeof(int));
    write(ctx->st_fd, &ctx->current_count, sizeof(int));
    kv_free(key);
    kv_free(value);
    kv_free(value_len);
    return;
}

int aof_append(AOFcontext_ctx* ctx,char *op,char *key,char *value,uint32_t value_len,char *huge_buf,int *huge_len,int *current_num){
    if(!ctx||!op||!key){
        return -1;
    }
    if(ctx->current_count>MAX_COUNT){//进来先判断要不要重写旧数据
        aof_rewrite(ctx);
    }
    int offset = 0;
    int op_len = strlen(op) + 1;
    int key_len = strlen(key) + 1;
    int length = op_len + key_len + value_len;
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
    int n = write_full(ctx->db_fd,huge_buf,(*huge_len));//先写旧数据
    ctx->size+=n;
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
    n=writev(ctx->db_fd,iov,iovcnt);
    ctx->size+=n;

    (*huge_len)= 0;
    ctx->current_count+=((*current_num)+1);
    (*current_num)=0;
    
    lseek(ctx->st_fd,0,SEEK_SET);
    write(ctx->st_fd,&ctx->size,sizeof(int));
    write(ctx->st_fd,&ctx->current_count,sizeof(int));

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
    int total_size=0;
    if(ctx->size==0) return OK;
    while(total_size<ctx->size){
        int length=0;
        int n=read_full(ctx->db_fd,&length,sizeof(int));
        
        total_size+=n;

        char *pkt=(char *)kv_malloc(length);
        n=read_full(ctx->db_fd,pkt,length);
        if (n != length) {
            kv_free(pkt);
            return ERROR; // 读不到完整的包，说明文件损坏或状态对不上
        }
        int offset=0;
        int op_len=strnlen(pkt+offset,length-offset)+1;
        char *op=pkt+offset;
        offset+=op_len;
        int key_len=strnlen(pkt+offset,length-offset)+1;
        char *key=pkt+offset;
        offset+=key_len;

        uint8_t *value=pkt+offset;
        uint32_t value_len=length-offset;
        offset+=value_len;
        kv_aof_execute(op,key,value,value_len);
        total_size+=offset;
        kv_free(pkt);
    }
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
    if(*huge_len==0) return;
    if (*huge_len > 0) {
        int n = write(ctx->db_fd, huge_buf, *huge_len);
        ctx->size += n;
        ctx->current_count += *current_num;
        
        // 同步状态文件
        lseek(ctx->st_fd, 0, SEEK_SET);
        write(ctx->st_fd, &ctx->size, sizeof(int));
        write(ctx->st_fd, &ctx->current_count, sizeof(int));
        
        // 重置缓冲区
        *huge_len = 0;
        *current_num = 0;
    }
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
