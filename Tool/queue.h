#ifndef _QUEUE_H_
#define _QUEUE_H_

#include <stddef.h>

/*
 * 通用 void* 队列（单向链表 + 尾指针，Push/Poll 均 O(1)）
 *
 * 约定：
 *  1. 队列只存指针，不负责释放元素内容；销毁/清空时可通过回调统一释放；
 *  2. push 拒绝 NULL，因此 poll 返回 NULL 一定表示"队列为空"；
 *  3. 非线程安全：如需多线程共享，请在外面加锁。
 */

typedef struct QueueNode {
    struct QueueNode *next;
    void             *data;
} QueueNode;

typedef struct Queue {
    QueueNode *head;
    QueueNode *tail;
    size_t     size;
} Queue;

/* 堆上创建/销毁一个队列；free_data 为 NULL 表示只释放节点、不碰元素 */
Queue *queue_create(void);
void   queue_destroy(Queue *q, void (*free_data)(void *));

/* 栈上或结构体内嵌使用：字段清零即为空队列，queue_init 只是显式表达意图 */
void   queue_init(Queue *q);

/* 入队：0 成功，-1 失败（q/data 为空指针，或分配节点失败） */
int    queue_push(Queue *q, void *data);

/* 出队：返回队头元素；队列为空时返回 NULL（节点本身被释放） */
void  *queue_poll(Queue *q);

/* 只看队头，不出队；为空返回 NULL */
void  *queue_peek(const Queue *q);

/* 1=空 0=非空 */
int    queue_is_empty(const Queue *q);

size_t queue_size(const Queue *q);

/* 清空但保留队列本身（用于断连时回收未发完的数据） */
void   queue_clear(Queue *q, void (*free_data)(void *));

#endif
