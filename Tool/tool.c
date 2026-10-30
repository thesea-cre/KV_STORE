#include"tool.h"
#include"mempool.h"
Mempool_Manager mmp;
long current_alloc=0;
long current_free=0;
void *kv_malloc(size_t size){//用的很多的系统函数最好包装一层，为了修改方便
    current_alloc++;
    #if 0
    return malloc(size);
    #elif 1
    mmp=Managerpool_GetInstance();
    
    return Managerpool_Alloc(&mmp,size);
    #elif 0
    return je_malloc(size);
    #endif
}
void kv_free(void* ptr){
    if (ptr == NULL) return; // 加上这句，遇到 NULL 直接返回
    current_free++;
    #if 0
    return free(ptr);
    #elif 1
    return Managerpool_free(&mmp,ptr);
    #elif 0
    return je_free(ptr);
    #endif
}