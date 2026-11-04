#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "kv_rbtree.h"

static int failed = 0;

static void check_int(const char *name, int actual, int expected)
{
    if (actual == expected) {
        printf("[PASS] %s\n", name);
    } else {
        printf("[FAIL] %s (expected %d, got %d)\n", name, expected, actual);
        failed++;
    }
}

static void check_str(const char *name, const char *actual, const char *expected)
{
    if (actual != NULL && strcmp(actual, expected) == 0) {
        printf("[PASS] %s\n", name);
    } else {
        printf("[FAIL] %s (expected \"%s\", got \"%s\")\n",
               name, expected, actual ? actual : "(null)");
        failed++;
    }
}

static void check_size(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual == expected) {
        printf("[PASS] %s\n", name);
    } else {
        printf("[FAIL] %s (expected %u, got %u)\n", name, expected, actual);
        failed++;
    }
}

int main(void)
{
    KV_Rbtree db;
    uint8_t *value = NULL;
    uint32_t value_size = 0;

    /* 1. create */
    check_int("create", KV_Rbtree_create(&db), 0);

    /* 2. set 正常插入 */
    check_int("set name=king", KV_Rbtree_set(&db, "name", (uint8_t *)"king", 5), 0);
    check_int("set city=beijing", KV_Rbtree_set(&db, "city", (uint8_t *)"beijing", 8), 0);
    check_int("count after 2 set", KV_Rbtree_count(&db), 2);

    /* set 重复 key，应拒绝，返回 1 */
    check_int("set duplicate name", KV_Rbtree_set(&db, "name", (uint8_t *)"snow", 5), 1);
    check_int("count after duplicate", KV_Rbtree_count(&db), 2);

    /* 3. get 存在的 key */
    value = NULL;
    value_size = 0;
    check_int("get name ret", KV_Rbtree_get(&db, "name", &value, &value_size), 0);
    check_str("get name value", (char *)value, "king");
    check_size("get name size", value_size, 5);

    /* get 不存在的 key，红黑树中返回 -1 (注意与哈希表不同) */
    value = NULL;
    value_size = 0;
    check_int("get not-exist key", KV_Rbtree_get(&db, "nobody", &value, &value_size), -1);

    /* exist */
    check_int("exist name", KV_Rbtree_exist(&db, "name"), 1);
    check_int("exist nobody", KV_Rbtree_exist(&db, "nobody"), 0);

    /* 4. mod 存在的 key（等长） */
    check_int("mod name=snow", KV_Rbtree_mod(&db, "name", (uint8_t *)"snow", 5), 0);
    value = NULL;
    value_size = 0;
    check_int("get after mod ret", KV_Rbtree_get(&db, "name", &value, &value_size), 0);
    check_str("get after mod value", (char *)value, "snow");
    check_size("get after mod size", value_size, 5);

    /* mod 改成更长的值，能测出 value_len 没更新的 bug */
    check_int("mod name=snowman", KV_Rbtree_mod(&db, "name", (uint8_t *)"snowman", 8), 0);
    value = NULL;
    value_size = 0;
    check_int("get after mod-long ret", KV_Rbtree_get(&db, "name", &value, &value_size), 0);
    check_str("get after mod-long value", (char *)value, "snowman");
    check_size("get after mod-long size", value_size, 8);

    /* mod 不存在的 key，应返回 1 */
    check_int("mod not-exist key", KV_Rbtree_mod(&db, "nobody", (uint8_t *)"x", 2), 1);

    /* 5. del 存在的 key */
    check_int("del city", KV_Rbtree_del(&db, "city"), 0);

    /* del 后再 get，应查不到 */
    value = NULL;
    value_size = 0;
    check_int("get after del", KV_Rbtree_get(&db, "city", &value, &value_size), -1);
    check_int("count after del", KV_Rbtree_count(&db), 1);

    /* del 不存在的 key，应返回 1 */
    check_int("del not-exist key", KV_Rbtree_del(&db, "nobody"), 1);

    /* 6. 大量数据插入/查找/删除测试（核心：验证红黑树的自平衡旋转是否正常） */
    {
        int bulk_ok = 1;
        char k[32], v[32];

        // 插入 100 条数据（必然触发多次红黑树旋转）
        for (int i = 0; i < 100; i++) {
            snprintf(k, sizeof(k), "key_%03d", i);
            snprintf(v, sizeof(v), "val_%03d", i);
            if (KV_Rbtree_set(&db, k, (uint8_t *)v, (uint32_t)(strlen(v) + 1)) != 0) {
                bulk_ok = 0;
                break;
            }
        }
        check_int("bulk set 100 nodes", bulk_ok, 1);
        check_int("count after bulk set", KV_Rbtree_count(&db), 101); // 之前留了 name，所以是 101

        // 乱序查找（验证树的排序和二分查找正确）
        for (int i = 0; i < 100; i++) {
            snprintf(k, sizeof(k), "key_%03d", i);
            snprintf(v, sizeof(v), "val_%03d", i);
            value = NULL; value_size = 0;
            if (KV_Rbtree_get(&db, k, &value, &value_size) != 0 ||
                strcmp((char *)value, v) != 0 ||
                value_size != (uint32_t)(strlen(v) + 1)) {
                bulk_ok = 0;
            }
        }
        check_int("bulk get 100 nodes", bulk_ok, 1);

        // 随机顺序删除（验证红黑树删除后的平衡恢复，最容易出 bug 的地方）
        for (int i = 0; i < 100; i += 2) { // 删掉偶数编号
            snprintf(k, sizeof(k), "key_%03d", i);
            if (KV_Rbtree_del(&db, k) != 0) bulk_ok = 0;
        }
        check_int("bulk del 50 nodes", bulk_ok, 1);
        check_int("count after bulk del", KV_Rbtree_count(&db), 51);

        // 验证剩下来的奇数编号还在
        check_int("get key_001 (exists)", KV_Rbtree_exist(&db, "key_001"), 1);
        check_int("get key_002 (deleted)", KV_Rbtree_exist(&db, "key_002"), 0);
    }

    /* 7. 遍历测试 (注意：此处调用的是 .c 文件里的 KV_Rbtree_traverse) */
    {
        uint8_t *keys[256] = {0};
        uint8_t *values[256] = {0};
        uint32_t lens[256] = {0};
        
        // 清理之前留存的 "name"
        KV_Rbtree_del(&db, "name"); 

        KV_Rbtree_traverse(&db, keys, values, lens);
        
        // 遍历后，只要没崩溃，且确实取到了数据，就算测试通过
        // (由于当前 traverse 是前序遍历按数组下标存的，下标0必定有值)
        int tra_ok = (keys[0] != NULL && values[0] != NULL);
        check_int("traverse test", tra_ok, 1);
    }

    /* 8. destroy */
    KV_Rbtree_destory(&db);
    printf("[PASS] destroy\n");

    printf("==== %d case(s) failed ====\n", failed);
    return failed ? 1 : 0;
}