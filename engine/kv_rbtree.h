#ifndef KV_RBTREE_H
#define KV_RBTREE_H

#include "rbtree.h"
#include <stddef.h>
#include<stdint.h>  

// 红黑树KV节点
typedef struct kv_rb_node {
    struct rb_node node;  // 红黑树内核节点
    char *key;
    uint8_t *value;
    uint32_t value_len;
    // struct kv_rb_node* pre;
    // struct kv_rb_node* next;
} kv_rb_node_t;

// 红黑树KV引擎主结构体
typedef struct {
    struct rb_root root;  // 红黑树根
    int count;            // 键值对总数
} KV_Rbtree;

// 接口声明（与数组、哈希引擎完全对齐）
int  KV_Rbtree_create(KV_Rbtree *tree);
void KV_Rbtree_destory(KV_Rbtree *tree);
int  KV_Rbtree_set(KV_Rbtree *tree, char *key, uint8_t *value,uint32_t value_len);
int  KV_Rbtree_get(KV_Rbtree *tree, char *key,uint8_t **value,uint32_t *value_lne);
int  KV_Rbtree_del(KV_Rbtree *tree, char *key);
int  KV_Rbtree_mod(KV_Rbtree *tree, char *key, uint8_t *value,uint32_t value_len);
int  KV_Rbtree_exist(KV_Rbtree *tree, char *key);
void KV_Rbtree_tra(KV_Rbtree *tree);
int  KV_Rbtree_count(KV_Rbtree *tree);


#endif