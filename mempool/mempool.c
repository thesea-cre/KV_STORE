#include"mempool.h"
#include<assert.h>
Mempool* Mempool_create(size_t nsize,size_t nblock){//创建一个额外的内存池
    if(nsize<=0||nblock<=0) return NULL;
    Mempool* pool=(Mempool*)malloc(sizeof(Mempool));
    if(!pool) return NULL;
    pool->nSize=nsize;
    pool->nBlock=nblock;
    pool->pbuffer=NULL;
    pool->header=NULL;
    pthread_mutex_init(&pool->mutex,NULL);

    return pool;
}
void Mempool_destory(Mempool* pool){//销毁内存池
    if(!pool) return;
    if(pool->pbuffer) {
        free(pool->pbuffer);
    }
    pthread_mutex_destroy(&pool->mutex);
    free(pool);
}
void Mempool_Init(Mempool* pool){//
    if(!pool) return;
    size_t Blocksize=pool->nSize+sizeof(MemBlock);
    size_t Poolsize=pool->nBlock*Blocksize;
    pool->pbuffer=(char *)malloc(Poolsize);
    if(pool->pbuffer==NULL) return;
    pool->header=(MemBlock*)pool->pbuffer;
    pool->header->bUsed=_NOT_USE;
    pool->header->bBelong=_POOL;
    pool->header->pool=pool;
    pool->header->next=NULL;
    MemBlock * p=pool->header;
    for(int i=1;i<pool->nBlock;i++){
        MemBlock* q=(MemBlock*)(pool->pbuffer+i*Blocksize);
        q->bUsed=_NOT_USE;
        q->bBelong=_POOL;
        q->pool=pool;
        p->next=q;
        p=q;
    }
    p->next=NULL;
}
void *Mempool_Alloc(Mempool* pool,size_t nsize){
    if(pool==NULL||nsize<=0) return NULL;
    pthread_mutex_lock(&pool->mutex);
    if(pool->pbuffer==NULL){
        Mempool_Init(pool);
    }
    MemBlock *ptr=NULL;
    if(pool->header==NULL){//内存池耗尽
        ptr=(MemBlock*)malloc(nsize+sizeof(MemBlock));
        if(ptr){
            ptr->bUsed=_USE;
            ptr->bBelong=_CORE;
            ptr->pool=NULL;
            ptr->next=NULL;
        }
    }else{
        ptr=pool->header;//header就是没有分配的内存池
        pool->header=pool->header->next;
        ptr->bUsed=_USE;
    }
    pthread_mutex_unlock(&pool->mutex);
    if(!ptr) return NULL;
    return (void*)((char*)ptr+sizeof(MemBlock));
}
void Mempool_free(Mempool* pool,void *ptr){
    if(!pool||ptr==NULL) return;
    pthread_mutex_lock(&pool->mutex);
    MemBlock* pBlock=(MemBlock*)((char*) ptr-sizeof(MemBlock));
    if(pBlock->bBelong==_CORE){
        free(pBlock);
        return;
    }
    else{
        pBlock->bUsed=_NOT_USE;
        pBlock->next=pool->header;
        pool->header=pBlock;
    }
    pthread_mutex_unlock(&pool->mutex);
}
static void Managerpool_Init(Mempool_Manager* ms,int begin,int end,Mempool* pool){
    for(int i=begin;i<end;i++){
        ms->_Alloc[i]=pool;
    }
}
Mempool_Manager* Managerpool_GetInstance(void){
    static Mempool_Manager manager;//创建一个管理器
    static int inited=0;
    if(inited==0){


        memset(&manager,0,sizeof(Mempool_Manager));
        manager.Mempool_64.nSize=64;
        manager.Mempool_64.nBlock=1000;
        pthread_mutex_init(&manager.Mempool_64.mutex,NULL);
        

        manager.Mempool_128.nSize=128;
        manager.Mempool_128.nBlock=1000;
        pthread_mutex_init(&manager.Mempool_128.mutex,NULL);

       

        manager.Mempool_256.nSize=256;
        manager.Mempool_256.nBlock=1000;        
        pthread_mutex_init(&manager.Mempool_256.mutex,NULL);

        Managerpool_Init(&manager,1,65,&manager.Mempool_64);
        Managerpool_Init(&manager,65,129,&manager.Mempool_128);
        Managerpool_Init(&manager,129,257,&manager.Mempool_256);

        inited=1;
    }
    return &manager;

}
void *Managerpool_Alloc(Mempool_Manager* manager,size_t nSize){
    if(!manager) return NULL;
    if(nSize<=MAX_MEMPOOL_SIZE){
        return Mempool_Alloc(manager->_Alloc[nSize],nSize);
    }else{//大于最大用malloc
        MemBlock* pre=(MemBlock*)malloc(nSize+sizeof(MemBlock));
        if(pre==NULL) return NULL;
        pre->bBelong=_CORE;
        pre->bUsed=_USE;
        pre->next=NULL;
        pre->pool=NULL;
        return (void*)((char*)pre+sizeof(MemBlock));
    }
}
void Managerpool_free(Mempool_Manager* manager,void *ptr){
    if(manager==NULL) return;
    MemBlock* pre=(MemBlock*)((char*)ptr-sizeof(MemBlock));
    if(pre->bBelong==_POOL){
        Mempool* pool=pre->pool;
        Mempool_free(pool,ptr);
    }else{
        free(pre);
    }
}


// int main() {
//     printf("========== 内存池功能测试开始 ==========\n\n");

//     // 1. 获取管理器单例
//     Mempool_Manager* mgr = Managerpool_GetInstance();
//     assert(mgr != NULL);
//     printf("✓ 内存池管理器初始化成功\n\n");

//     // ---------- 测试1：小内存分配（池内） ----------
//     printf("----- 测试1：32字节分配（64字节池） -----\n");
//     void* p1 = Managerpool_Alloc(mgr, 32);
//     assert(p1 != NULL);
//     MemBlock* blk1 = (MemBlock*)((char*)p1 - sizeof(MemBlock));
//     assert(blk1->bBelong == _POOL && blk1->bUsed == _USE);
//     printf("✓ 分配成功，内存来自池内\n");

//     // 读写验证
//     memset(p1, 'A', 32);
//     int check = 1;
//     for (int i = 0; i < 32; i++) {
//         if (((char*)p1)[i] != 'A') { check = 0; break; }
//     }
//     assert(check);
//     printf("✓ 内存读写验证通过\n\n");

//     // ---------- 测试2：边界值64字节 ----------
//     printf("----- 测试2：边界值64字节 -----\n");
//     void* p2 = Managerpool_Alloc(mgr, 64);
//     assert(p2 != NULL);
//     MemBlock* blk2 = (MemBlock*)((char*)p2 - sizeof(MemBlock));
//     assert(blk2->bBelong == _POOL);
//     printf("✓ 64字节分配成功，命中64字节池\n\n");

//     // ---------- 测试3：128字节 ----------
//     printf("----- 测试3：128字节分配 -----\n");
//     void* p3 = Managerpool_Alloc(mgr, 128);
//     assert(p3 != NULL);
//     MemBlock* blk3 = (MemBlock*)((char*)p3 - sizeof(MemBlock));
//     assert(blk3->bBelong == _POOL);
//     printf("✓ 128字节分配成功，命中128字节池\n\n");

//     // ---------- 测试4：256字节（最大池规格） ----------
//     printf("----- 测试4：256字节分配（最大池） -----\n");
//     void* p4 = Managerpool_Alloc(mgr, 256);
//     assert(p4 != NULL);
//     MemBlock* blk4 = (MemBlock*)((char*)p4 - sizeof(MemBlock));
//     assert(blk4->bBelong == _POOL);
//     printf("✓ 256字节分配成功，命中256字节池\n\n");

//     // ---------- 测试5：超大小分配（系统malloc降级） ----------
//     printf("----- 测试5：512字节分配（系统分配） -----\n");
//     void* p5 = Managerpool_Alloc(mgr, 512);
//     assert(p5 != NULL);
//     MemBlock* blk5 = (MemBlock*)((char*)p5 - sizeof(MemBlock));
//     assert(blk5->bBelong == _CORE);
//     printf("✓ 512字节分配成功，走系统malloc\n");

//     memset(p5, 'B', 512);
//     check = 1;
//     for (int i = 0; i < 512; i++) {
//         if (((char*)p5)[i] != 'B') { check = 0; break; }
//     }
//     assert(check);
//     printf("✓ 大内存读写验证通过\n\n");

//     // ---------- 测试6：释放与地址复用 ----------
//     printf("----- 测试6：内存释放与复用 -----\n");
//     void* old_addr = p1;
//     Managerpool_free(mgr, p1);
//     printf("✓ 释放32字节内存\n");

//     void* p1_new = Managerpool_Alloc(mgr, 32);
//     assert(p1_new != NULL);
//     if (p1_new == old_addr) {
//         printf("✓ 重新分配地址相同，内存块复用成功\n");
//     } else {
//         printf("⚠  地址不同（链表顺序影响，属于正常现象）\n");
//     }
//     Managerpool_free(mgr, p1_new);
//     printf("\n");

//     // ---------- 测试7：批量分配释放 ----------
//     printf("----- 测试7：批量分配释放（10个100字节） -----\n");
//     void* ptrs[10];
//     for (int i = 0; i < 10; i++) {
//         ptrs[i] = Managerpool_Alloc(mgr, 100);
//         assert(ptrs[i] != NULL);
//         *(int*)ptrs[i] = i; // 写入编号
//     }
//     printf("✓ 10个100字节分配完成\n");

//     // 验证数据不串位
//     check = 1;
//     for (int i = 0; i < 10; i++) {
//         if (*(int*)ptrs[i] != i) { check = 0; break; }
//     }
//     assert(check);
//     printf("✓ 批量数据读写验证通过\n");

//     for (int i = 0; i < 10; i++) {
//         Managerpool_free(mgr, ptrs[i]);
//     }
//     printf("✓ 批量释放完成\n\n");

//     // 释放剩余测试内存
//     Managerpool_free(mgr, p2);
//     Managerpool_free(mgr, p3);
//     Managerpool_free(mgr, p4);
//     Managerpool_free(mgr, p5);

//     printf("========== 全部测试用例通过！ ==========\n");
//     return 0;
// }