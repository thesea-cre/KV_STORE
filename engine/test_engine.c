// 引擎层单元测试：一份用例、函数指针表驱动三个引擎（数组/哈希/红黑树）
// 统一验证语义一致性：set重复→1拒绝、del/mod不存在→1、exist 1/0
// 用法（Linux）：make -C engine run；ASan 默认开启，越界/泄漏会直接报
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "kv_array.h"
#include "kv_hash.h"
#include "kv_rbtree.h"

// 引擎内部依赖的内存分配包装（kvstore.c 里也是转发 malloc/free），
// 测试自己提供一份，避免把 protocol 层及其全局变量依赖拖进链接
void *kv_malloc(uint32_t size) { return malloc(size); }
void kv_free(void *ptr) { free(ptr); }

static const char *g_engine_name = "?";
static int g_checks = 0;
static int g_fails = 0;

#define CHECK(cond) do {                                             \
    g_checks++;                                                      \
    if (!(cond)) {                                                   \
        printf("[FAIL] 引擎[%s] %s:%d: %s\n",                        \
               g_engine_name, __FILE__, __LINE__, #cond);            \
        g_fails++;                                                   \
    }                                                                \
} while (0)

// 三个引擎的函数签名文本上一致（仅结构体类型名不同），用函数指针表统一驱动
typedef struct {
    const char *name;
    int  max_cap;        // >0：容量有上限（kv_array=128）；0：无上限
    int  collision_test; // 1：跑哈希冲突用例（多个key挤同一个桶）
    int  large_test;     // 1：跑大规模插入/删除用例
    int  (*create)(void *);
    void (*destory)(void *);
    int  (*set)(void *, char *, char *);
    char *(*get)(void *, char *);
    int  (*del)(void *, char *);
    int  (*mod)(void *, char *, char *);
    int  (*exist)(void *, char *);
} EngineOps;

static void run_tests(const EngineOps *ops)
{
    printf("===== 测试引擎: %s =====\n", ops->name);
    g_engine_name = ops->name;

    void *e = malloc(512);//三个引擎的结构体都远小于512
    memset(e, 0, 512);

    char k[64], val[64];
    char *v;

    // 1. create
    CHECK(ops->create(e) == 0);

    // 2. 基本 set/get/exist
    CHECK(ops->set(e, "foo", "bar") == 0);
    CHECK(ops->exist(e, "foo") == 1);
    v = ops->get(e, "foo");
    CHECK(v != NULL && strcmp(v, "bar") == 0);

    // 3. 重复 set：三个引擎都应拒绝(返回1)且保留旧值
    CHECK(ops->set(e, "foo", "newbar") == 1);
    v = ops->get(e, "foo");
    CHECK(v != NULL && strcmp(v, "bar") == 0);

    // 4. 不存在的 key：get→NULL，exist→0，del/mod→1
    CHECK(ops->get(e, "nope") == NULL);
    CHECK(ops->exist(e, "nope") == 0);
    CHECK(ops->del(e, "nope") == 1);
    CHECK(ops->mod(e, "nope", "x") == 1);

    // 5. mod 覆盖已存在的 key
    CHECK(ops->mod(e, "foo", "baz") == 0);
    v = ops->get(e, "foo");
    CHECK(v != NULL && strcmp(v, "baz") == 0);

    // 5b. mod 成更长的值（KV_Map 的 mod 直接 strncpy 不重新分配，ASan 会抓堆溢出）
    CHECK(ops->mod(e, "foo", "a_much_longer_value_than_before") == 0);
    v = ops->get(e, "foo");
    CHECK(v != NULL && strcmp(v, "a_much_longer_value_than_before") == 0);

    // 6. del
    CHECK(ops->del(e, "foo") == 0);
    CHECK(ops->get(e, "foo") == NULL);
    CHECK(ops->exist(e, "foo") == 0);
    CHECK(ops->del(e, "foo") == 1);//再删应报不存在

    // 7. 批量 100 个，抽查 get
    for (int i = 0; i < 100; i++) {
        snprintf(k, sizeof(k), "bulk%03d", i);
        snprintf(val, sizeof(val), "value-%d", i);
        CHECK(ops->set(e, k, val) == 0);
    }
    for (int i = 0; i < 100; i += 7) {
        snprintf(k, sizeof(k), "bulk%03d", i);
        snprintf(val, sizeof(val), "value-%d", i);
        v = ops->get(e, k);
        CHECK(v != NULL && strcmp(v, val) == 0);
    }

    // 8. 容量上限：塞满 max_cap 个后，再塞必须失败
    if (ops->max_cap > 0) {
        for (int i = 100; i < ops->max_cap; i++) {
            snprintf(k, sizeof(k), "fill%03d", i);
            CHECK(ops->set(e, k, "v") == 0);
        }
        snprintf(k, sizeof(k), "fill%03d", ops->max_cap);//第 max_cap+1 个
        CHECK(ops->set(e, k, "v") != 0);
    }

    // 9. 哈希冲突：两字符 key，('a'+i)+('z'-i) 的 ASCII 和恒定，必落同一个桶
    if (ops->collision_test) {
        for (int i = 0; i < 10; i++) {
            k[0] = 'a' + i; k[1] = 'z' - i; k[2] = 0;
            val[0] = 'A' + i; val[1] = 0;
            CHECK(ops->set(e, k, val) == 0);
        }
        for (int i = 0; i < 10; i++) {
            k[0] = 'a' + i; k[1] = 'z' - i; k[2] = 0;
            val[0] = 'A' + i; val[1] = 0;
            v = ops->get(e, k);
            CHECK(v != NULL && strcmp(v, val) == 0);
        }
        // "KKO"=75+75+79=219，与上面10个key同桶但不存在：专治KV_Map_get不前进的死循环
        CHECK(ops->get(e, "KKO") == NULL);
        CHECK(ops->exist(e, "KKO") == 0);
    }

    // 10. 大规模：2000 个插入 → 抽查 → 删一半 → exist 逐个核对
    if (ops->large_test) {
        for (int i = 0; i < 2000; i++) {
            snprintf(k, sizeof(k), "rb%05d", i);
            snprintf(val, sizeof(val), "rv%d", i);
            CHECK(ops->set(e, k, val) == 0);
        }
        for (int i = 0; i < 2000; i += 13) {
            snprintf(k, sizeof(k), "rb%05d", i);
            snprintf(val, sizeof(val), "rv%d", i);
            v = ops->get(e, k);
            CHECK(v != NULL && strcmp(v, val) == 0);
        }
        for (int i = 0; i < 2000; i += 2) {
            snprintf(k, sizeof(k), "rb%05d", i);
            CHECK(ops->del(e, k) == 0);
        }
        for (int i = 0; i < 2000; i++) {
            snprintf(k, sizeof(k), "rb%05d", i);
            CHECK(ops->exist(e, k) == (i % 2 == 0 ? 0 : 1));
        }
    }

    // 11. destory 后重建：状态必须干净
    ops->destory(e);
    memset(e, 0, 512);
    CHECK(ops->create(e) == 0);
    CHECK(ops->get(e, "bulk000") == NULL);
    CHECK(ops->exist(e, "bulk000") == 0);
    ops->destory(e);

    free(e);
    printf("===== %s 结束 =====\n\n", ops->name);
}

int main(void)
{
    // 三个引擎签名一致，仅结构体类型不同，这里做函数指针转换（-Wno-cast-function-type）
    static const EngineOps ops_array = {
        "kv_array", 128, 0, 0,
        (int (*)(void *))kv_array_create,
        (void (*)(void *))kv_array_destory,
        (int (*)(void *, char *, char *))kv_array_set,
        (char *(*)(void *, char *))kv_array_get,
        (int (*)(void *, char *))kv_array_del,
        (int (*)(void *, char *, char *))kv_array_mod,
        (int (*)(void *, char *))kv_array_exist,
    };
    static const EngineOps ops_hash = {
        "kv_hash", 0, 1, 0,
        (int (*)(void *))KV_Map_create,
        (void (*)(void *))KV_Map_destory,
        (int (*)(void *, char *, char *))KV_Map_set,
        (char *(*)(void *, char *))KV_Map_get,
        (int (*)(void *, char *))KV_Map_del,
        (int (*)(void *, char *, char *))KV_Map_mod,
        (int (*)(void *, char *))KV_Map_exist,
    };
    static const EngineOps ops_rbtree = {
        "kv_rbtree", 0, 0, 1,
        (int (*)(void *))KV_Rbtree_create,
        (void (*)(void *))KV_Rbtree_destory,
        (int (*)(void *, char *, char *))KV_Rbtree_set,
        (char *(*)(void *, char *))KV_Rbtree_get,
        (int (*)(void *, char *))KV_Rbtree_del,
        (int (*)(void *, char *, char *))KV_Rbtree_mod,
        (int (*)(void *, char *))KV_Rbtree_exist,
    };

    run_tests(&ops_array);
    run_tests(&ops_rbtree);//放第二位：即使 hash 卡死，前两个引擎的结果也已产出
    run_tests(&ops_hash);  //已知 KV_Map_get 对不存在的 key 死循环，会卡在这里

    printf("========== 总计: %d 项检查, %d 项失败 ==========\n", g_checks, g_fails);
    return g_fails > 0 ? 1 : 0;
}
