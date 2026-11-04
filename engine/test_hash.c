#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "kv_hash.h"
#include"log.h"

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
    log_init("server.log", LOG_INFO); 
    KV_Map db;
    uint8_t *value = NULL;
    uint32_t value_size = 0;

    /* 1. create */
    check_int("create", KV_Map_create(&db), 0);

    /* 2. set 正常插入 */
    check_int("set name=king", KV_Map_set(&db, "name", (uint8_t *)"king", 5), 0);
    check_int("set city=beijing", KV_Map_set(&db, "city", (uint8_t *)"beijing", 8), 0);
    check_int("count after 2 set", KV_Map_count(&db), 2);

    /* set 重复 key，应拒绝，返回 1 */
    check_int("set duplicate name", KV_Map_set(&db, "name", (uint8_t *)"snow", 5), 1);
    check_int("count after duplicate", KV_Map_count(&db), 2);

    /* 3. get 存在的 key */
    value = NULL;
    value_size = 0;
    check_int("get name ret", KV_Map_get(&db, "name", &value, &value_size), 0);
    check_str("get name value", (char *)value, "king");
    check_size("get name size", value_size, 5);

    /* get 不存在的 key，应返回 1 */
    value = NULL;
    value_size = 0;
    check_int("get not-exist key", KV_Map_get(&db, "nobody", &value, &value_size), 1);

    /* exist */
    check_int("exist name", KV_Map_exist(&db, "name"), 1);
    check_int("exist nobody", KV_Map_exist(&db, "nobody"), 0);

    /* 4. mod 存在的 key（等长） */
    check_int("mod name=snow", KV_Map_mod(&db, "name", (uint8_t *)"snow", 5), 0);
    value = NULL;
    value_size = 0;
    check_int("get after mod ret", KV_Map_get(&db, "name", &value, &value_size), 0);
    check_str("get after mod value", (char *)value, "snow");
    check_size("get after mod size", value_size, 5);

    /* mod 改成更长的值，能测出 value_len 没更新的 bug */
    check_int("mod name=snowman", KV_Map_mod(&db, "name", (uint8_t *)"snowman", 8), 0);
    value = NULL;
    value_size = 0;
    check_int("get after mod-long ret", KV_Map_get(&db, "name", &value, &value_size), 0);
    check_str("get after mod-long value", (char *)value, "snowman");
    check_size("get after mod-long size", value_size, 8);

    /* mod 不存在的 key，应返回 1 */
    check_int("mod not-exist key", KV_Map_mod(&db, "nobody", (uint8_t *)"x", 2), 1);

    /* 5. del 存在的 key */
    check_int("del city", KV_Map_del(&db, "city"), 0);

    /* del 后再 get，应查不到 */
    value = NULL;
    value_size = 0;
    check_int("get after del", KV_Map_get(&db, "city", &value, &value_size), 1);
    check_int("count after del", KV_Map_count(&db), 1);

    /* del 不存在的 key，应返回 1 */
    check_int("del not-exist key", KV_Map_del(&db, "nobody"), 1);

    /* 6. 强制哈希冲突（ab/ba 字符和相同）验证链上查找/删除 */
    check_int("set ab", KV_Map_set(&db, "ab", (uint8_t *)"1", 2), 0);
    check_int("set ba", KV_Map_set(&db, "ba", (uint8_t *)"2", 2), 0);
    value = NULL; value_size = 0;
    check_int("get ab ret", KV_Map_get(&db, "ab", &value, &value_size), 0);
    check_str("get ab value", (char *)value, "1");
    value = NULL; value_size = 0;
    check_int("get ba ret", KV_Map_get(&db, "ba", &value, &value_size), 0);
    check_str("get ba value", (char *)value, "2");
    check_int("del ab", KV_Map_del(&db, "ab"), 0);
    value = NULL; value_size = 0;
    check_int("get ab after del", KV_Map_get(&db, "ab", &value, &value_size), 1);
    value = NULL; value_size = 0;
    check_int("get ba still", KV_Map_get(&db, "ba", &value, &value_size), 0);
    check_str("get ba still value", (char *)value, "2");
    check_int("del ba", KV_Map_del(&db, "ba"), 0);

    /* 7. 批量 set/get/del 覆盖更多桶 */
    {
        int bulk_set_ok = 1;
        int bulk_get_ok = 1;
        int bulk_del_ok = 1;
        char k[16], v[16];

        for (int i = 0; i < 20; i++) {
            snprintf(k, sizeof(k), "key%d", i);
            snprintf(v, sizeof(v), "val%d", i);
            if (KV_Map_set(&db, k, (uint8_t *)v, (uint32_t)(strlen(v) + 1)) != 0)
                bulk_set_ok = 0;
        }
        check_int("bulk set all", bulk_set_ok, 1);
        check_int("count after bulk set", KV_Map_count(&db), 21);

        for (int i = 0; i < 20; i++) {
            snprintf(k, sizeof(k), "key%d", i);
            snprintf(v, sizeof(v), "val%d", i);
            value = NULL;
            value_size = 0;
            if (KV_Map_get(&db, k, &value, &value_size) != 0 ||
                strcmp((char *)value, v) != 0 ||
                value_size != (uint32_t)(strlen(v) + 1)) {
                bulk_get_ok = 0;
            }
        }
        check_int("bulk get all", bulk_get_ok, 1);

        for (int i = 0; i < 20; i++) {
            snprintf(k, sizeof(k), "key%d", i);
            if (KV_Map_del(&db, k) != 0)
                bulk_del_ok = 0;
        }
        check_int("bulk del all", bulk_del_ok, 1);
        check_int("count after bulk del", KV_Map_count(&db), 1);
    }

    /* 8. destroy（返回 void，只能保证不崩溃） */
    KV_Map_destory(&db);
    printf("[PASS] destroy\n");

    printf("==== %d case(s) failed ====\n", failed);
    return failed ? 1 : 0;
}