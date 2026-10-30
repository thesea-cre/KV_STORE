// mempool.c 单元测试：独立池（create/alloc/free/destory）、Manager（单例/尺寸路由）、多线程压测
// 用法（Linux）：make -C mempool run；ASan 默认开启（double free/越界全靠它盯）
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "mempool.h"

static int g_checks = 0;
static int g_fails = 0;

#define CHECK(cond) do {                                             \
    g_checks++;                                                      \
    if (!(cond)) {                                                   \
        printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
        g_fails++;                                                   \
    }                                                                \
} while (0)

// 从用户指针反推MemBlock头（mempool的内部布局约定）
#define BLOCK_OF(p) ((MemBlock *)((char *)(p) - sizeof(MemBlock)))

// ========== 第一层：独立池 ==========
static void test_pool_basic(void)
{
    printf("== 独立池：参数校验与惰性初始化 ==\n");
    CHECK(Mempool_create(0, 10) == NULL);//非法size
    CHECK(Mempool_create(64, 0) == NULL);//非法块数

    Mempool *pool = Mempool_create(64, 4);
    CHECK(pool != NULL);
    CHECK(pool->nSize == 64 && pool->nBlock == 4);
    CHECK(pool->pbuffer == NULL && pool->header == NULL);//惰性：create时不建池

    CHECK(Mempool_Alloc(pool, 0) == NULL);//非法申请
    Mempool_free(pool, NULL);//不应崩
    Mempool_destory(NULL);//不应崩

    void *a = Mempool_Alloc(pool, 64);
    CHECK(a != NULL);
    CHECK(pool->pbuffer != NULL);//第一次Alloc触发惰性初始化
    CHECK(BLOCK_OF(a)->bBelong == _POOL && BLOCK_OF(a)->bUsed == _USE);

    Mempool_destory(pool);
}

static void test_pool_reuse(void)
{
    printf("== 独立池：free后块复用（LIFO回收） ==\n");
    Mempool *pool = Mempool_create(32, 2);
    void *a = Mempool_Alloc(pool, 32);
    CHECK(a != NULL);
    Mempool_free(pool, a);
    void *b = Mempool_Alloc(pool, 32);
    CHECK(b == a);//刚free的块在链表头，必须复用同一块
    Mempool_free(pool, b);
    Mempool_destory(pool);
}

static void test_pool_exhaust_and_core(void)
{
    printf("== 独立池：耗尽降级core + core释放 ==\n");
    Mempool *pool = Mempool_create(64, 3);
    void *p0 = Mempool_Alloc(pool, 64);
    void *p1 = Mempool_Alloc(pool, 64);
    void *p2 = Mempool_Alloc(pool, 64);
    CHECK(p0 && p1 && p2);
    CHECK(BLOCK_OF(p0)->bBelong == _POOL);

    void *p3 = Mempool_Alloc(pool, 64);//第4个：池耗尽，降级malloc
    CHECK(p3 != NULL);
    CHECK(BLOCK_OF(p3)->bBelong == _CORE);
    memset(p3, 0x5A, 64);//core块数据区=申请尺寸，写满不越界（ASan盯）
    CHECK(((unsigned char *)p3)[63] == 0x5A);

    Mempool_free(pool, p3);//core块：从core_list摘除并真free
    Mempool_free(pool, p0);
    Mempool_free(pool, p1);
    Mempool_free(pool, p2);
    Mempool_destory(pool);//ASan在此验证：无泄漏
}

static void test_pool_double_free(void)
{
    printf("== 独立池：double free防护（修复前ASan会在这里abort） ==\n");
    Mempool *pool = Mempool_create(64, 2);

    // pool块连free两次：第二次必须被bUsed检查忽略
    void *a = Mempool_Alloc(pool, 64);
    Mempool_free(pool, a);
    Mempool_free(pool, a);//double free：防护生效则ASan无报告

    // core块连free两次：第二次必须被core_list摘除检查忽略
    void *b = Mempool_Alloc(pool, 64);
    void *c = Mempool_Alloc(pool, 64);
    void *d = Mempool_Alloc(pool, 64);//CORE
    CHECK(BLOCK_OF(d)->bBelong == _CORE);
    Mempool_free(pool, d);
    Mempool_free(pool, d);//double free core块

    Mempool_free(pool, b);
    Mempool_free(pool, c);
    Mempool_destory(pool);
}

static void test_pool_isolation(void)
{
    printf("== 独立池：块间数据独立 + 写满不越界 ==\n");
    Mempool *pool = Mempool_create(64, 4);
    unsigned char *ptrs[4];
    for (int i = 0; i < 4; i++) {
        ptrs[i] = Mempool_Alloc(pool, 64);
        memset(ptrs[i], 0x10 + i, 64);//每块写满64字节不同pattern，正好等于数据区大小
    }
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 64; j++) {
            CHECK(ptrs[i][j] == 0x10 + i);//任一字节被别块踩过都会FAIL，越界写会被ASan直接抓
        }
    }
    for (int i = 0; i < 4; i++) Mempool_free(pool, ptrs[i]);
    Mempool_destory(pool);
}

// ========== 第二层：Manager ==========
static void test_manager_route(void)
{
    printf("== Manager：单例与尺寸路由 ==\n");
    Mempool_Manager *mgr = Managerpool_GetInstance();
    CHECK(mgr != NULL);
    CHECK(Managerpool_GetInstance() == mgr);//单例

    CHECK(Managerpool_Alloc(mgr, 0) == NULL);//0尺寸路由进池但Alloc拒绝

    // 每个尺寸：验证落到正确的池/降级core，且容量足够写满
    size_t sizes[] = { 1, 32, 64, 65, 100, 128, 129, 200, 256, 257, 512, 1000 };
    for (int i = 0; i < (int)(sizeof(sizes) / sizeof(sizes[0])); i++) {
        size_t s = sizes[i];
        void *p = Managerpool_Alloc(mgr, s);
        CHECK(p != NULL);
        MemBlock *blk = BLOCK_OF(p);
        if (s <= 64) {
            CHECK(blk->bBelong == _POOL && blk->pool == &mgr->Mempool_64);
        } else if (s <= 128) {
            CHECK(blk->bBelong == _POOL && blk->pool == &mgr->Mempool_128);
        } else if (s <= 256) {
            CHECK(blk->bBelong == _POOL && blk->pool == &mgr->Mempool_256);
        } else {
            CHECK(blk->bBelong == _CORE && blk->pool == NULL);//>256走malloc
        }
        memset(p, 0xC3, s);//写满申请尺寸：容量不足或越界立刻现形
        Managerpool_free(mgr, p);
    }
}

// ========== 第三层：多线程压测 ==========
static void *thread_worker(void *arg)
{
    unsigned int seed = (unsigned int)(size_t)arg * 2654435761u;
    for (int i = 0; i < 1000; i++) {
        size_t s = 1 + rand_r(&seed) % 512;//1..512，覆盖三档池+core降级
        void *p = Managerpool_Alloc(Managerpool_GetInstance(), s);
        if (!p) continue;
        memset(p, (int)(s & 0xFF), s);
        Managerpool_free(Managerpool_GetInstance(), p);
    }
    return NULL;
}

static void test_threads(void)
{
    printf("== 多线程压测：8线程×1000次随机尺寸alloc/free ==\n");
    pthread_t tids[8];
    for (int i = 0; i < 8; i++) {
        CHECK(pthread_create(&tids[i], NULL, thread_worker, (void *)(size_t)(i + 1)) == 0);
    }
    for (int i = 0; i < 8; i++) {
        pthread_join(tids[i], NULL);
    }
    // 压测完单例仍正常工作
    void *p = Managerpool_Alloc(Managerpool_GetInstance(), 100);
    CHECK(p != NULL);
    Managerpool_free(Managerpool_GetInstance(), p);
}

int main(void)
{
    test_pool_basic();
    test_pool_reuse();
    test_pool_exhaust_and_core();
    test_pool_double_free();
    test_pool_isolation();
    test_manager_route();
    test_threads();//放最后：锁竞争问题在这里暴露

    // Manager单例按设计永不destory（生命周期=进程），
    // 其内存被static manager持有，属still-reachable，LeakSanitizer默认不报，全绿可达
    printf("========== 总计: %d 项检查, %d 项失败 ==========\n", g_checks, g_fails);
    return g_fails > 0 ? 1 : 0;
}
