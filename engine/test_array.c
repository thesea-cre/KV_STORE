#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "kv_array.h"

/* 两个简单的检查函数：符合预期打印 PASS，不符合打印 FAIL 并计数 */
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
        printf("[FAIL] %s (expected %zu, got %zu)\n", name, expected, actual);
        failed++;
    }
}

int main(void)
{
    kv_array_t db;
    uint8_t *value = NULL;
    uint32_t value_size = 0;

    /* 1. create */
    check_int("create", kv_array_create(&db), 0);

    /* 2. set 正常插入 */
    check_int("set name=king", kv_array_set(&db, "name", (uint8_t *)"king", 5), 0);
    check_int("set city=beijing", kv_array_set(&db, "city", (uint8_t *)"beijing", 8), 0);

    /* set 重复 key，应拒绝，返回 1 */
    check_int("set duplicate name", kv_array_set(&db, "name", (uint8_t *)"snow", 5), 1);

    /* 3. get 存在的 key，应拿到值和长度 */
    value = NULL;
    value_size = 0;
    check_int("get name ret", kv_array_get(&db, "name", &value, &value_size), 0);
    check_str("get name value", (char *)value, "king");
    check_size("get name size", value_size, 5);

    /* get 不存在的 key，应返回"不存在"（预期 1） */
    value = NULL;
    value_size = 0;
    check_int("get not-exist key", kv_array_get(&db, "nobody", &value, &value_size), 1);

    /* 4. mod 存在的 key */
    check_int("mod name=snow", kv_array_mod(&db, "name", (uint8_t *)"snow", 5), 0);
    value = NULL;
    value_size = 0;
    check_int("get after mod ret", kv_array_get(&db, "name", &value, &value_size), 0);
    check_str("get after mod value", (char *)value, "snow");

    /* mod 不存在的 key，应返回 1 */
    check_int("mod not-exist key", kv_array_mod(&db, "nobody", (uint8_t *)"x", 2), 1);

    /* 5. del 存在的 key */
    check_int("del city", kv_array_del(&db, "city"), 0);

    /* del 后再 get，应查不到 */
    value = NULL;
    value_size = 0;
    check_int("get after del", kv_array_get(&db, "city", &value, &value_size), 1);

    /* del 不存在的 key，应返回 1 */
    check_int("del not-exist key", kv_array_del(&db, "nobody"), 1);

    /* 6. destroy */
    check_int("destory", kv_array_destory(&db), 0);

    printf("==== %d case(s) failed ====\n", failed);
    return failed ? 1 : 0;
}