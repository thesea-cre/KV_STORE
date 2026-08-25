#ifndef _MEMPOOL_H_
#define _MEMPOOL_H_
#define MAX_MEMPOOL_SIZE 256
#include<stdio.h>
#include<stdlib.h>
#include<pthread.h>
#include<string.h>
// typedef struct Mempool Mempool;
// typedef struct Mempool_Manager Mempool_Manager;
// typedef struct MemBlock MemBlock;

typedef struct MemBlock{
    struct MemBlock *next;//下一个内存块
    int bUsed;//是否使用
    int bBelong;//是否属于内存池
    void *pool;//
}MemBlock;
typedef struct Mempool{
    char *pbuffer;//整个内存池的指针
    MemBlock *header;//第一个内存块
    size_t nSize;//每个块大小
    size_t nBlock;//块数量
   // Mempool_Manager* manager;//内存池的管理员
    pthread_mutex_t mutex;//一个锁
}Mempool;
typedef struct Mempool_Manager{
    Mempool Mempool_64;
    Mempool Mempool_128;
    Mempool Mempool_256;
    Mempool * _Alloc[MAX_MEMPOOL_SIZE+1];

}Mempool_Manager;
typedef enum{
    _USE,
    _NOT_USE
}Use_Status;
typedef enum{
    _POOL,
    _CORE
}Belong_Status;
Mempool * Mempool_create(size_t size,size_t block);
void Mempool_destory(Mempool* pool);
void * Mempool_Alloc(Mempool*pool,size_t size);
void Mempool_free(Mempool* pool,void *ptr);
void Mempool_Init(Mempool* pool);

Mempool_Manager* Managerpool_GetInstance(void);
void * Managerpool_Alloc(Mempool_Manager* msg,size_t nsize);
void Managerpool_free(Mempool_Manager*msg,void *ptr);



#endif