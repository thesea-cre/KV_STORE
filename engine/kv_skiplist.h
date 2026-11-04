#ifndef _KV_SKIPLIST_H_
#define _KV_SKIPLIST_H_

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<stdint.h>
#include<stddef.h>

#define KV_MAX_LEVEL 32 // 跳表最大层数，通常 32 层足够支撑千万级别数据

// 跳表节点结构
typedef struct kv_skiplist_node_s {
    char *key;
    uint8_t *value;
    uint32_t value_len;
    struct kv_skiplist_node_s **forward; // 柔性数组：指向每一层的下一个节点
} kv_skiplist_node_t;

// 跳表管理结构 (完全对称 kv_array_t)
typedef struct kv_skiplist_s {
    kv_skiplist_node_t *header; // 头节点（不存有效数据，只做引路）
    int level;                  // 当前跳表的最大层数
    int idx;                    // 元素个数 (对应 array 的 idx)
} kv_skiplist_t;

int kv_skiplist_create(kv_skiplist_t *init);
int kv_skiplist_destory(kv_skiplist_t *init);

int kv_skiplist_set(kv_skiplist_t *init, char *key, uint8_t *value, uint32_t value_len);
int kv_skiplist_get(kv_skiplist_t *init, char *key, uint8_t **value, uint32_t *value_size);
int kv_skiplist_del(kv_skiplist_t *init, char *key);
int kv_skiplist_mod(kv_skiplist_t *init, char *key, uint8_t *value, uint32_t value_size);
int kv_skiplist_exist(kv_skiplist_t *init, char *key);
void kv_skiplist_tra(kv_skiplist_t *init, char **key, uint8_t **value, uint32_t *value_size);
int kv_skiplist_count(kv_skiplist_t *init);

#endif