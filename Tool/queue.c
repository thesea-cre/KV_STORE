#include"queue.h"
#include"tool.h"          /* kv_malloc / kv_free */

/*
 * 分配器：默认走项目的 kv_malloc/kv_free，和全项目的分配统计保持一致。
 * 需要脱离项目单独编译测试时，用 -DQUEUE_MALLOC=malloc -DQUEUE_FREE=free 覆盖。
 * 两个宏必须同时覆盖，否则会出现"malloc 出来的指针交给 kv_free"的崩溃。
 */
#ifndef QUEUE_MALLOC
#define QUEUE_MALLOC(sz)  kv_malloc(sz)
#endif
#ifndef QUEUE_FREE
#define QUEUE_FREE(p)     kv_free(p)
#endif

void queue_init(Queue *q)
{
    if (!q) return;
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
}

Queue *queue_create(void)
{
    Queue *q = (Queue *)QUEUE_MALLOC(sizeof(Queue));
    if (!q) return NULL;
    queue_init(q);
    return q;
}

void queue_clear(Queue *q, void (*free_data)(void *))
{
    QueueNode *n;

    if (!q) return;

    n = q->head;
    while (n) {
        QueueNode *next = n->next;
        if (free_data && n->data) free_data(n->data);
        QUEUE_FREE(n);
        n = next;
    }
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
}

void queue_destroy(Queue *q, void (*free_data)(void *))
{
    if (!q) return;
    queue_clear(q, free_data);
    QUEUE_FREE(q);
}

int queue_push(Queue *q, void *data)
{
    QueueNode *n;

    if (!q || !data) return -1;      /* 不收 NULL：这样 poll 返回 NULL 就一定是空 */

    n = (QueueNode *)QUEUE_MALLOC(sizeof(QueueNode));
    if (!n) return -1;

    n->data = data;
    n->next = NULL;

    if (q->tail) {
        q->tail->next = n;           /* 追加到尾部 */
        q->tail = n;
    } else {
        q->head = q->tail = n;       /* 空队列：头尾都是它 */
    }

    q->size++;
    return 0;
}

void *queue_poll(Queue *q)
{
    QueueNode *n;
    void      *data;

    if (!q || !q->head) return NULL; /* 空 */

    n = q->head;
    q->head = n->next;
    if (!q->head) q->tail = NULL;    /* 摘掉最后一个元素，尾指针要归零 */

    q->size--;

    data = n->data;
    QUEUE_FREE(n);                   /* 只释放节点，数据仍归调用方 */
    return data;
}

void *queue_peek(const Queue *q)
{
    return (q && q->head) ? q->head->data : NULL;
}

int queue_is_empty(const Queue *q)
{
    return (!q || !q->head) ? 1 : 0;
}

size_t queue_size(const Queue *q)
{
    return q ? q->size : 0;
}
