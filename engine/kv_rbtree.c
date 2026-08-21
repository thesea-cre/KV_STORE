#include "kv_rbtree.h"
#include "kvstore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
static kv_rb_node_t* _kv_rb_create_node(char *key, char *value)
{
    if (!key || !value) return NULL;

    kv_rb_node_t *node = (kv_rb_node_t *)kv_malloc(sizeof(kv_rb_node_t));
    if (!node) return NULL;

    // 拷贝键
    size_t klen = strlen(key) + 1;
    node->key = (char *)kv_malloc(klen);
    if (!node->key) {
        kv_free(node);
        return NULL;
    }
    memset(node->key, 0, klen);
    strncpy(node->key, key, klen - 1);

    // 拷贝值
    size_t vlen = strlen(value) + 1;
    node->value = (char *)kv_malloc(vlen);
    if (!node->value) {
        kv_free(node->key);
        kv_free(node);
        return NULL;
    }
    memset(node->value, 0, vlen);
    strncpy(node->value, value, vlen - 1);

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

// 中序遍历打印（辅助遍历函数）
static void _kv_rb_print_tree(struct rb_node *node)
{
    if (!node) return;
    _kv_rb_print_tree(node->rb_left);
    kv_rb_node_t *kv_node = rb_entry(node, kv_rb_node_t, node);
    printf("key:%s,value:%s\n", kv_node->key, kv_node->value);
    _kv_rb_print_tree(node->rb_right);
}

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

int KV_Rbtree_set(KV_Rbtree *tree, char *key, char *value)
{
    if (!tree || !key || !value) return -1;

    // 先查是否已存在
    struct rb_node *found = rb_find(&tree->root, key, _kv_rb_cmp_key);
    if (found) {
        return 1;  // 键已存在，与哈希引擎返回值一致
    }

    // 创建新节点
    kv_rb_node_t *new_node = _kv_rb_create_node(key, value);
    if (!new_node) return -2;

    // 插入红黑树
    rb_insert(&tree->root, &new_node->node, _kv_rb_node_cmp);
    tree->count++;
    return 0;
}

char* KV_Rbtree_get(KV_Rbtree *tree, char *key)
{
    if (!tree || !key) return NULL;

    struct rb_node *found = rb_find(&tree->root, key, _kv_rb_cmp_key);
    if (!found) return NULL;

    kv_rb_node_t *kv_node = rb_entry(found, kv_rb_node_t, node);
    return kv_node->value;
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

int KV_Rbtree_mod(KV_Rbtree *tree, char *key, char *value)
{
    if (!tree || !key || !value) return -1;

    struct rb_node *found = rb_find(&tree->root, key, _kv_rb_cmp_key);
    if (!found) {
        return 1;  // 键不存在
    }

    kv_rb_node_t *kv_node = rb_entry(found, kv_rb_node_t, node);

    // 释放旧值，分配新值
    kv_free(kv_node->value);
    size_t vlen = strlen(value) + 1;
    kv_node->value = (char *)kv_malloc(vlen);
    if (!kv_node->value) return -2;
    memset(kv_node->value, 0, vlen);
    strncpy(kv_node->value, value, vlen - 1);

    return 0;
}

int KV_Rbtree_exist(KV_Rbtree *tree, char *key)
{
    if (!tree || !key) return -1;
    return rb_find(&tree->root, key, _kv_rb_cmp_key) ? 1 : 0;
}

void KV_Rbtree_traverse(KV_Rbtree *tree)
{
    if (!tree) return;
    printf("rbtree count:%d\n", tree->count);
    _kv_rb_print_tree(tree->root.rb_node);
}
// 清理输入缓冲区，防止scanf残留导致死循环
static void clear_stdin(void)
{
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

// int main()
// {
//     KV_Rbtree tree;
//     int ret = KV_Rbtree_create(&tree);
//     if (ret != 0) {
//         printf("KV_Rbtree 创建失败\n");
//         return -1;
//     }

//     char key[128] = {0};
//     char value[128] = {0};

//     printf("===== KV 存储引擎（红黑树实现）=====\n");
//     while (1) {
//         int opt = 0;
//         printf("\n----- 命令菜单 -----\n");
//         printf("0: 插入键值对  set key value\n");
//         printf("1: 查询键值    get key\n");
//         printf("2: 删除键值    del key\n");
//         printf("3: 修改键值    mod key value\n");
//         printf("4: 判断存在    exist key\n");
//         printf("5: 遍历全部    traverse\n");
//         printf("其他: 退出程序\n");
//         printf("请输入命令编号: ");

//         if (scanf("%d", &opt) != 1) {
//             printf("输入格式错误，请输入数字\n");
//             clear_stdin();
//             continue;
//         }

//         switch (opt) {
//             case 0:
//                 printf("请输入 key 和 value（空格分隔）: ");
//                 if (scanf("%127s %127s", key, value) != 2) {
//                     printf("输入格式错误\n");
//                     clear_stdin();
//                     break;
//                 }
//                 ret = KV_Rbtree_set(&tree, key, value);
//                 if (ret == 0)
//                     printf("插入成功\n");
//                 else if (ret == 1)
//                     printf("插入失败：键已存在\n");
//                 else
//                     printf("插入失败：系统错误\n");
//                 break;

//             case 1:
//                 printf("请输入 key: ");
//                 if (scanf("%127s", key) != 1) {
//                     printf("输入格式错误\n");
//                     clear_stdin();
//                     break;
//                 }
//                 char *vl = KV_Rbtree_get(&tree, key);
//                 if (vl == NULL)
//                     printf("查询失败：键不存在\n");
//                 else
//                     printf("查询成功，value = %s\n", vl);
//                 break;

//             case 2:
//                 printf("请输入 key: ");
//                 if (scanf("%127s", key) != 1) {
//                     printf("输入格式错误\n");
//                     clear_stdin();
//                     break;
//                 }
//                 ret = KV_Rbtree_del(&tree, key);
//                 if (ret == 0)
//                     printf("删除成功\n");
//                 else if (ret == 1)
//                     printf("删除失败：键不存在\n");
//                 else
//                     printf("删除失败：系统错误\n");
//                 break;

//             case 3:
//                 printf("请输入 key 和 新value（空格分隔）: ");
//                 if (scanf("%127s %127s", key, value) != 2) {
//                     printf("输入格式错误\n");
//                     clear_stdin();
//                     break;
//                 }
//                 ret = KV_Rbtree_mod(&tree, key, value);
//                 if (ret == 0)
//                     printf("修改成功\n");
//                 else if (ret == 1)
//                     printf("修改失败：键不存在\n");
//                 else
//                     printf("修改失败：系统错误\n");
//                 break;

//             case 4:
//                 printf("请输入 key: ");
//                 if (scanf("%127s", key) != 1) {
//                     printf("输入格式错误\n");
//                     clear_stdin();
//                     break;
//                 }
//                 ret = KV_Rbtree_exist(&tree, key);
//                 if (ret == 1)
//                     printf("键 %s 存在\n", key);
//                 else if (ret == 0)
//                     printf("键 %s 不存在\n", key);
//                 else
//                     printf("查询失败：系统错误\n");
//                 break;

//             case 5:
//                 printf("\n----- 全部键值对 -----\n");
//                 KV_Rbtree_traverse(&tree);
//                 break;

//             default:
//                 printf("退出程序\n");
//                 goto exit_program;
//         }
//     }

// exit_program:
//     KV_Rbtree_destory(&tree);
//     return 0;
// }