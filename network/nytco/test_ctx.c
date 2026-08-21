#include <stdio.h>
#include <stdlib.h>

typedef struct {
    unsigned long rbx;
    unsigned long rbp;
    unsigned long r12;
    unsigned long r13;
    unsigned long r14;
    unsigned long r15;
    unsigned long rsp;
    unsigned long rip;
} co_ctx_t;

int  co_get_ctx(co_ctx_t *ctx);
void co_make_ctx(co_ctx_t *ctx, void *stack, size_t stack_size, void (*func)(void*), void *arg);
void co_swap_ctx(co_ctx_t *cur_ctx, co_ctx_t *dst_ctx);

static co_ctx_t main_ctx;
static co_ctx_t cor_ctx;
static char stack[8192];

void cor_func(void *arg) {
    printf("✅ 协程启动，参数: %d\n", *(int*)arg);
    co_swap_ctx(&cor_ctx, &main_ctx); // 切回主函数
    printf("✅ 协程再次运行\n");
    co_swap_ctx(&cor_ctx, &main_ctx);
}

int main() {
    int arg = 666;

    printf("开始测试上下文切换...\n");
    co_make_ctx(&cor_ctx, stack, sizeof(stack), cor_func, &arg);

    printf("第一次切换到协程\n");
    co_swap_ctx(&main_ctx, &cor_ctx);
    printf("回到主函数\n");

    printf("第二次切换到协程\n");
    co_swap_ctx(&main_ctx, &cor_ctx);
    printf("回到主函数，测试通过 ✅\n");

    return 0;
}