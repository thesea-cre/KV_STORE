#ifndef _AOF_H_
#define _AOF_H_
#define AOF_READ_BUFFER_SIZE 65536
#define ERROR -2
#define OK 0
#define CON 1
#include<stddef.h>
#include<pthread.h>
typedef struct{
    int fd;
    off_t size;
    off_t current_count;
    pthread_mutex_t mutex_;

}AOFcontext_ctx;
AOFcontext_ctx* aof_init(const char *file_path);
int aof_append(const char *pkt,int pkt_len);
int aof_recover(AOFcontext_ctx* ctx,char *buf,int length);
int aof_fsync(void);
void aof_close(AOFcontext_ctx* ctx);

 
#endif