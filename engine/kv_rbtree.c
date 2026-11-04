#include "kv_rbtree.h"
#include"tool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include"log.h"
// typedef struct queue{
//     struct kv_rb_node* head;
//     struct kv_rb_node* tail;
//     int num;
// }queue_;
// void queue_init(queue_* que){
//     que->head=(kv_rb_node_t*)kv_malloc(sizeof(kv_rb_node_t));
//     que->tail=(kv_rb_node_t*)kv_malloc(sizeof(kv_rb_node_t));
//     que->head->next=que->tail;
//     que->tail->pre=que->head;
//     que->head->pre=NULL;
//     que->tail->next=NULL;
//     que->num=0;
// }
// void queue_destory(queue_ *que){
//     kv_free(que->head);
//     kv_free(que->tail);
//     que->num=0;
// }
// void push_(queue_* que,kv_rb_node_t* ex){
//     ex->next=que->tail;
//     ex->pre=que->tail->pre;
//     que->tail->pre->next=ex;
//     que->tail->next=NULL;
//     que->tail->pre=ex;
//     que->num++;
// }
// kv_rb_node_t* pull_(queue_* que){
//     kv_rb_node_t* res=que->head->next;
//     que->head->next=res->next;
//     res->next->pre=que->head;
//     res->next=NULL;
//     res->pre=NULL;
//     que->num--;
//     return res;
// }
// ========== 内部辅助函数 ==========

// 节点比较函数：用于红黑树插入排序
static int _kv_rb_node_cmp(const struct rb_node *a, const struct rb_node *b)
{
    kv_rb_node_t *na = rb_entry(a, kv_rb_node_t, node);
    kv_rb_node_t *nb = rb_entry(b, kv_rb_node_t, node);
    return strcmp(na->key, nb->key);
}

// 键比较函数：用于按键查找
static int _kv_rb_cmp_key(const struct rb_node *node, const void *key)
{
    kv_rb_node_t *n = rb_entry(node, kv_rb_node_t, node);
    return strcmp(n->key, (const char *)key);
}

// 创建新KV节点（深拷贝键值）
static kv_rb_node_t* _kv_rb_create_node(char *key, uint8_t *value,uint32_t value_len)
{
    if (!key || !value) return NULL;

    kv_rb_node_t *node = (kv_rb_node_t *)kv_malloc(sizeof(kv_rb_node_t));
    if (!node) return NULL;

    // 拷贝键
    uint32_t klen = strlen(key) + 1;
    node->key = (char *)kv_malloc(klen);
    if (!node->key) {
        kv_free(node);
        return NULL;
    }
    memset(node->key, 0, klen);
    strncpy(node->key, key, klen - 1);

    // 拷贝值
    uint32_t vlen = value_len;
    node->value = (char *)kv_malloc(vlen);
    if (!node->value) {
        kv_free(node->key);
        kv_free(node);
        return NULL;
    }
    memset(node->value, 0, vlen);
    memcpy(node->value, value, vlen);
    node->value_len=vlen;


    return node;
}

// 释放单个KV节点的所有内存
static void _kv_rb_free_node(kv_rb_node_t *node)
{
    if (!node) return;
    kv_free(node->key);
    kv_free(node->value);
    kv_free(node);
}

// 后序遍历销毁整棵树（辅助销毁函数）
static void _kv_rb_destroy_tree(struct rb_node *node)
{
    if (!node) return;
    _kv_rb_destroy_tree(node->rb_left);
    _kv_rb_destroy_tree(node->rb_right);
    kv_rb_node_t *kv_node = rb_entry(node, kv_rb_node_t, node);
    _kv_rb_free_node(kv_node);
}

// 前序遍历打印（辅助遍历函数）
static struct kv_rb_node* _kv_rb_tra_tree(struct rb_node *node,uint8_t** key,uint8_t **value,uint32_t *value_len,int i)
{
    if (!node) return NULL;
    kv_rb_node_t *kv_node = rb_entry(node, kv_rb_node_t, node);
    key[i]=kv_node->key;
    value[i]=kv_node->value;
    value_len[i]=kv_node->value_len;
    _kv_rb_tra_tree(node->rb_left,key,value,value_len,2*i+1);
    _kv_rb_tra_tree(node->rb_right,key,value,value_len,2*i+2);
}
// //层序遍历
// static void _kv_rb_tra_tree(struct rb_node* node,uint8_t **value,uint32_t *value_len){
//     queue_ *queue=(queue_*)kv_malloc(sizeof(queue_));
//     queue_init(queue);
//     kv_rb_node_t* root=rb_entry(node,kv_rb_node_t,node);
//     push(queue,root);
//     while(queue->num!=0){
        
//     }
// }
// ========== 对外接口实现 ==========

int KV_Rbtree_create(KV_Rbtree *tree)
{
    if (!tree) return -1;
    tree->root.rb_node = NULL;
    tree->count = 0;
    return 0;
}

void KV_Rbtree_destory(KV_Rbtree *tree)
{
    if (!tree) return;
    _kv_rb_destroy_tree(tree->root.rb_node);
    tree->root.rb_node = NULL;
    tree->count = 0;
}

int KV_Rbtree_set(KV_Rbtree *tree, char *key, uint8_t *value,uint32_t value_len)
{
    if (!tree || !key || !value) return -1;

    // 先查是否已存在
    struct rb_node *found = rb_find(&tree->root, key, _kv_rb_cmp_key);
    if (found) {
        return 1;  // 键已存在，与哈希引擎返回值一致
    }

    // 创建新节点
    kv_rb_node_t *new_node = _kv_rb_create_node(key, value,value_len);
    if (!new_node) return -2;
    // 插入红黑树
    rb_insert(&tree->root, &new_node->node, _kv_rb_node_cmp);
    tree->count++;
    return 0;
}

int KV_Rbtree_get(KV_Rbtree *tree, char *key,uint8_t **value,uint32_t *value_len)
{
    if (!tree || !key) return -2;

    struct rb_node *found = rb_find(&tree->root, key, _kv_rb_cmp_key);
    if (!found) return -1;

    kv_rb_node_t *kv_node = rb_entry(found, kv_rb_node_t, node);
    *value=kv_node->value;
    *value_len=kv_node->value_len;

    return 0;
}

int KV_Rbtree_del(KV_Rbtree *tree, char *key)
{
    if (!tree || !key) return -1;

    struct rb_node *found = rb_find(&tree->root, key, _kv_rb_cmp_key);
    if (!found) {
        return 1;  // 键不存在
    }

    // 先从树中移除
    rb_erase(found, &tree->root);

    // 释放节点内存
    kv_rb_node_t *kv_node = rb_entry(found, kv_rb_node_t, node);
    _kv_rb_free_node(kv_node);
    tree->count--;
    return 0;
}

int KV_Rbtree_mod(KV_Rbtree *tree, char *key, uint8_t *value,uint32_t value_len)
{
    if (!tree || !key || !value) return -1;

    struct rb_node *found = rb_find(&tree->root, key, _kv_rb_cmp_key);
    if (!found) {
        return 1;  // 键不存在
    }

    kv_rb_node_t *kv_node = rb_entry(found, kv_rb_node_t, node);

    // 释放旧值，分配新值
    kv_free(kv_node->value);
    uint32_t vlen = value_len;
    kv_node->value = (uint8_t *)kv_malloc(vlen);
    if (!kv_node->value) return -2;
    memset(kv_node->value, 0, vlen);
    memcpy(kv_node->value, value, vlen);
    kv_node->value_len = vlen; 

    return 0;
}

int KV_Rbtree_exist(KV_Rbtree *tree, char *key)
{
    if (!tree || !key) return -1;
    return rb_find(&tree->root, key, _kv_rb_cmp_key) ? 1 : 0;
}

void KV_Rbtree_traverse(KV_Rbtree *tree,uint8_t**key,uint8_t **value,uint32_t *value_len)
{
    if (!tree) return;
    _kv_rb_tra_tree(tree->root.rb_node,key,value,value_len,0);

}
int KV_Rbtree_count(KV_Rbtree* tree){
    return tree->count;
}
// 清理输入缓冲区，防止scanf残留导致死循环
static void clear_stdin(void)
{
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

