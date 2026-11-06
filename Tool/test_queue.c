/* 队列模块回归测试
 *
 * 编译（用标准库分配器，不依赖 Tool/tool.c 和内存池）：
 *   gcc -DQUEUE_MALLOC=malloc -DQUEUE_FREE=free -o test_queue test_queue.c queue.c
 *   ./test_queue
 *
 * 也可以直接链项目分配器（前提是同目录有 tool.c 和 ../mempool/mempool.c）：
 *   gcc -o test_queue test_queue.c queue.c tool.c ../mempool/mempool.c -lpthread
 */
#include"queue.h"
#include<stdio.h>
#include<stdlib.h>

static int failures = 0;

#define CHECK(cond) do{                                                  \
    if(!(cond)){                                                         \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
        failures++;                                                      \
    }                                                                    \
}while(0)

int main(void)
{
    Queue q;
    int a = 1, b = 2, c = 3;

    /* 1. 空队列 */
    queue_init(&q);
    CHECK(queue_is_empty(&q));
    CHECK(queue_size(&q) == 0);
    CHECK(queue_poll(&q) == NULL);
    CHECK(queue_peek(&q) == NULL);

    /* 2. 入队/顺序/出队 */
    CHECK(queue_push(&q, &a) == 0);
    CHECK(queue_push(&q, &b) == 0);
    CHECK(queue_push(&q, &c) == 0);
    CHECK(queue_size(&q) == 3);
    CHECK(!queue_is_empty(&q));
    CHECK(queue_peek(&q) == &a);

    CHECK(queue_poll(&q) == &a);
    CHECK(queue_poll(&q) == &b);
    CHECK(queue_size(&q) == 1);
    CHECK(queue_poll(&q) == &c);
    CHECK(queue_is_empty(&q));
    CHECK(queue_size(&q) == 0);
    CHECK(queue_poll(&q) == NULL);

    /* 3. 反复入队/出队，验证 tail 指针清空后不残留 */
    for (int i = 0; i < 1000; i++) {
        CHECK(queue_push(&q, &a) == 0);
        CHECK(queue_push(&q, &b) == 0);
        CHECK(queue_poll(&q) == &a);
        CHECK(queue_poll(&q) == &b);
        CHECK(queue_is_empty(&q));
    }

    /* 4. clear 带释放回调 */
    CHECK(queue_push(&q, malloc(8)) == 0);
    CHECK(queue_push(&q, malloc(8)) == 0);
    queue_clear(&q, free);
    CHECK(queue_is_empty(&q));
    CHECK(queue_size(&q) == 0);

    /* 5. 拒绝 NULL 入队 */
    CHECK(queue_push(&q, NULL) != 0);
    CHECK(queue_push(NULL, &a) != 0);
    CHECK(queue_is_empty(NULL));

    if (failures) {
        printf("%d case(s) FAILED\n", failures);
        return 1;
    }
    printf("PASS test_queue\n");
    return 0;
}
