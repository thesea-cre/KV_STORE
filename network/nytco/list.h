#ifndef LIST_H
#define LIST_H

#include <stddef.h>

// 链表节点结构体：嵌入到协程里，就是你定义的 ready_node
struct list_head {
    struct list_head *next, *prev;
};

// 核心宏：从成员地址反推结构体首地址（和 rb_entry 原理完全一样）
#define container_of(ptr, type, member) ({          \
    const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
    (type *)( (char *)__mptr - offsetof(type, member) );})

// 从链表节点拿到对应的协程结构体
#define list_entry(ptr, type, member) \
    container_of(ptr, type, member)

// 1. 初始化链表头（调度器初始化就绪队列时用）
#define INIT_LIST_HEAD(head) do { \
    (head)->next = (head); \
    (head)->prev = (head); \
} while(0)

// 2. 尾部插入节点（入队，就绪队列FIFO用）
static inline void list_add_tail(struct list_head *new_node, struct list_head *head)
{
    new_node->prev = head->prev;
    new_node->next = head;
    head->prev->next = new_node;
    head->prev = new_node;
}

// 3. 删除指定节点
static inline void list_del(struct list_head *entry)
{
    entry->next->prev = entry->prev;
    entry->prev->next = entry->next;
    entry->next = entry->prev = NULL;
}

// 4. 判断队列是否为空
static inline int list_empty(const struct list_head *head)
{
    return head->next == head;
}

// 5. 安全遍历链表（遍历中删除节点也不会崩）
#define list_for_each_safe(pos, n, head) \
    for (pos = (head)->next, n = pos->next; pos != (head); \
        pos = n, n = pos->next)

#endif