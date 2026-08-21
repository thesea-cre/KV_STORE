#ifndef KV_RBTREE_H
#define KV_RBTREE_H

#include "rbtree.h"
#include <stddef.h>

// 红黑树KV节点
typedef struct kv_rb_node {
    struct rb_node node;  // 红黑树内核节点
    char *key;
    char *value;
} kv_rb_node_t;

// 红黑树KV引擎主结构体
typedef struct {
    struct rb_root root;  // 红黑树根
    int count;            // 键值对总数
} KV_Rbtree;

// 接口声明（与数组、哈希引擎完全对齐）
int  KV_Rbtree_create(KV_Rbtree *tree);
void KV_Rbtree_destory(KV_Rbtree *tree);
int  KV_Rbtree_set(KV_Rbtree *tree, char *key, char *value);
char* KV_Rbtree_get(KV_Rbtree *tree, char *key);
int  KV_Rbtree_del(KV_Rbtree *tree, char *key);
int  KV_Rbtree_mod(KV_Rbtree *tree, char *key, char *value);
int  KV_Rbtree_exist(KV_Rbtree *tree, char *key);
void KV_Rbtree_traverse(KV_Rbtree *tree);

#endif