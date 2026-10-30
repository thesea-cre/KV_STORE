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
    pool->core_list = NULL;
    pthread_mutex_init(&pool->mutex,NULL);

    return pool;
}
void Mempool_destory(Mempool* pool){//销毁内存池
    if(!pool) return;
    pthread_mutex_lock(&pool->mutex);
    MemBlock* core = pool->core_list;
    while (core) {
        MemBlock* next = core->next;
        free(core);
        core = next;
    }
    if(pool->pbuffer) {
        free(pool->pbuffer);
    }
    pthread_mutex_unlock(&pool->mutex);
    pthread_mutex_destroy(&pool->mutex);
    free(pool);
}
static void Mempool_Init(Mempool* pool){//仅在Mempool_Alloc内惰性调用；外部勿调，重复调用会泄漏pbuffer
    if(!pool) return;
    size_t Blocksize=pool->nSize+sizeof(MemBlock);
    size_t Poolsize=pool->nBlock*Blocksize;
    pool->pbuffer=(char *)malloc(Poolsize);
    // if(pool->pbuffer==NULL) return;
    // pool->header=(MemBlock*)pool->pbuffer;
    // pool->header->bUsed=_NOT_USE;
    // pool->header->bBelong=_POOL;
    // pool->header->pool=pool;
    // pool->header->next=NULL;
    // pool->header->pre=NULL;
    MemBlock * p=pool->header;
    for(int i=0;i<pool->nBlock;i++){
        MemBlock* q=(MemBlock*)(pool->pbuffer+i*Blocksize);
        q->bUsed=_NOT_USE;
        q->bBelong=_POOL;
        q->pool=pool;
        q->size=pool->nSize;
        p->next=q;
        q->pre=p;
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
    if(pool->header->next==NULL){//内存池耗尽：优先复用core_list里回收的块，没有才malloc新的
        ptr=pool->core_list->next;
        while(ptr){
            if(ptr->size>=nsize){
                break;
            }
            ptr=ptr->next;
        }
        if(ptr){
            ptr->pre->next=ptr->next;
            if(ptr->next){
                ptr->next->pre=ptr->pre;
            }
            ptr->pre=NULL;
            ptr->next=NULL;
            ptr->bUsed=_USE;
        }else{
            ptr=(MemBlock*)malloc(nsize+sizeof(MemBlock));
            ptr->bUsed=_USE;
            ptr->bBelong=_CORE;
            ptr->pool=pool;
            ptr->size=nsize;
            ptr->next=NULL;
            ptr->pre=NULL;
        }
    }else{
        ptr=pool->header->next;//header就是没有分配的内存池
        pool->header->next=ptr->next;
        if(ptr->next){
            ptr->next->pre=pool->header;
        }
        ptr->next=NULL;
        ptr->pre=NULL;
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
        // 不立即free：置空闲挂回core_list复用，由destory统一释放。
        // double free时读的是活着的块，bUsed==_NOT_USE直接忽略；
        // 若malloc后立刻free，第二次再读块头就是use-after-free（ASan可证），原理上防不住
        if(pBlock->bUsed==_NOT_USE){//已经释放过，忽略，防double free
            pthread_mutex_unlock(&pool->mutex);
            return;
        }
        pBlock->bUsed=_NOT_USE;
        pBlock->pre=pool->core_list;
        pBlock->next=pool->core_list->next;
        if(pool->core_list->next){
            pool->core_list->next->pre=pBlock;
        }
        pool->core_list->next=pBlock;
        pthread_mutex_unlock(&pool->mutex);
        return;
    }
    else{
        if(pBlock->bUsed==_NOT_USE){//已经释放过，忽略，防double free
            pthread_mutex_unlock(&pool->mutex);
            return;
        }
        pBlock->bUsed=_NOT_USE;
        pBlock->pre=pool->header;
        pBlock->next=pool->header->next;
        if(pBlock->next){
            pBlock->next->pre=pBlock;
        }
        pool->header->next=pBlock;
    }
    pthread_mutex_unlock(&pool->mutex);
}

static pthread_once_t manager_once = PTHREAD_ONCE_INIT;
static Mempool_Manager manager;
static void init_manager(void) {
    memset(&manager, 0, sizeof(Mempool_Manager));
    //直接在静态存储上初始化三个池，不走Mempool_create——它malloc的80字节结构体
    //按值拷贝进manager后原指针即丢，三个池白泄漏240字节（LSan实测上报）
    Mempool *pools[3] = { &manager.Mempool_64, &manager.Mempool_128, &manager.Mempool_256 };
    size_t   sizes[3] = { 64, 128, 256 };
    for (int i = 0; i < 3; i++) {
        pools[i]->nSize  = sizes[i];
        pools[i]->nBlock = 1000;
        pthread_mutex_init(&pools[i]->mutex, NULL);
        pools[i]->core_list=(MemBlock*)malloc(sizeof(MemBlock));
        memset(pools[i]->core_list,0,sizeof(MemBlock));
        pools[i]->core_list->bBelong=_CORE;
        pools[i]->core_list->bUsed=_USE;
        pools[i]->core_list->size=0;
        pools[i]->core_list->pool=pools[i];
        pools[i]->core_list->next=NULL;
        pools[i]->core_list->pre=NULL;


        pools[i]->header=(MemBlock*)malloc(sizeof(MemBlock));
        memset(pools[i]->header,0,sizeof(MemBlock));
        pools[i]->header->bBelong=_POOL;
        pools[i]->header->bUsed=_USE;
        pools[i]->header->size=0;
        pools[i]->header->pool=pools[i];
        pools[i]->header->next=NULL;
        pools[i]->header->pre=NULL;

        //pbuffer/header/core_list已被上面的memset归零，惰性初始化语义不变
    }
    for (int i = 1; i <= 64; i++) manager._Alloc[i] = &manager.Mempool_64;
    for (int i = 65; i <= 128; i++) manager._Alloc[i] = &manager.Mempool_128;
    for (int i = 129; i <= 256; i++) manager._Alloc[i] = &manager.Mempool_256;

}
Mempool_Manager Managerpool_GetInstance(void){
    // static Mempool_Manager manager;//创建一个管理器
    // static int inited=0;
    // if(inited==0){


    //     memset(&manager,0,sizeof(Mempool_Manager));
    //     manager.Mempool_64.nSize=64;
    //     manager.Mempool_64.nBlock=1000;
    //     pthread_mutex_init(&manager.Mempool_64.mutex,NULL);
        

    //     manager.Mempool_128.nSize=128;
    //     manager.Mempool_128.nBlock=1000;
    //     pthread_mutex_init(&manager.Mempool_128.mutex,NULL);

       

    //     manager.Mempool_256.nSize=256;
    //     manager.Mempool_256.nBlock=1000;        
    //     pthread_mutex_init(&manager.Mempool_256.mutex,NULL);

    //     Managerpool_Init(&manager,1,65,&manager.Mempool_64);
    //     Managerpool_Init(&manager,65,129,&manager.Mempool_128);
    //     Managerpool_Init(&manager,129,257,&manager.Mempool_256);

    //     inited=1;
    // }
    pthread_once(&manager_once, init_manager);
    return manager;
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
    if(manager==NULL||ptr==NULL) return;
    MemBlock* pre=(MemBlock*)((char*)ptr-sizeof(MemBlock));
    if(pre->bBelong==_POOL){
        Mempool* pool=pre->pool;
        Mempool_free(pool,ptr);
    }else{
        free(pre);
    }
}