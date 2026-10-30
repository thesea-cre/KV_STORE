#ifndef _AOF_H_
#define _AOF_H_
#define AOF_READ_BUFFER_SIZE 65536
#define ERROR -2
#define OK 0
#define CON 1
#define MAX_COUNT 100
#define MAX_TASK 100
#include<stddef.h>
#include<pthread.h>
#include<stdint.h>

typedef struct{
    int db_fd;
    int st_fd;
    int size;//aof文件大小
    int current_count;//命令的个数
}AOFcontext_ctx;
typedef struct{
    char *op;
    char *key;
    char *value;
    uint32_t value_len;
}AOF_task;
typedef struct{
    pthread_mutex_t task_mutex;
    AOF_task *head;
    AOF_task *tail;
    uint32_t task_num;
    pthread_cond_t cond_;
    // bool flag;
}Task_;

// void Task_queue_init(Task_* queue);
// void Task_queue_destory(Task_* queue);
// void AOF_Worker();
// AOF_task *pop_();
// void push(AOF_task* task);
AOFcontext_ctx *aof_init(const char *db_file_path,const char *st_file_path);
int aof_append(AOFcontext_ctx* ctx,char *op,char *key,char *value,
uint32_t value_len,char *huge_buf,int *huge_len,int *current_num);
int aof_recover(AOFcontext_ctx* ctx);
void aof_close(AOFcontext_ctx* ctx);
void force_flush(AOFcontext_ctx *ctx, char *huge_buf, int *huge_len, int *current_num);
void aof_rewrite(AOFcontext_ctx* ctx);
#endif