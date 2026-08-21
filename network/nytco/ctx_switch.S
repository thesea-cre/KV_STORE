# ctx_switch.S
.text
.global co_get_ctx
.global co_make_ctx
.global co_swap_ctx

# ==============================================
# 结构体偏移（和C语言严格对应）
# 0  : rbx
# 8  : rbp
# 16 : r12
# 24 : r13
# 32 : r14
# 40 : r15
# 48 : rsp
# 56 : rip
# ==============================================

# int co_get_ctx(co_ctx_t *ctx);
# 保存当前上下文到ctx，返回0
co_get_ctx:
    movq %rbx,  0(%rdi)
    movq %rbp,  8(%rdi)
    movq %r12, 16(%rdi)
    movq %r13, 24(%rdi)
    movq %r14, 32(%rdi)
    movq %r15, 40(%rdi)

    leaq 8(%rsp), %rax       # 调用者的栈指针（排除call压入的返回地址）
    movq %rax, 48(%rdi)
    movq (%rsp), %rax        # 保存返回地址作为rip
    movq %rax, 56(%rdi)

    xorq %rax, %rax          # 返回值0
    ret

# void co_swap_ctx(co_ctx_t *cur, co_ctx_t *dst);
# 保存当前上下文到cur，切换到dst
co_swap_ctx:
    # 保存当前上下文
    movq %rbx,  0(%rdi)
    movq %rbp,  8(%rdi)
    movq %r12, 16(%rdi)
    movq %r13, 24(%rdi)
    movq %r14, 32(%rdi)
    movq %r15, 40(%rdi)

    leaq 8(%rsp), %rax
    movq %rax, 48(%rdi)
    movq (%rsp), %rax
    movq %rax, 56(%rdi)

    # 加载目标上下文
    movq  0(%rsi), %rbx
    movq  8(%rsi), %rbp
    movq 16(%rsi), %r12
    movq 24(%rsi), %r13
    movq 32(%rsi), %r14
    movq 40(%rsi), %r15
    movq 48(%rsi), %rsp
    jmpq *56(%rsi)

# ==============================================
# void co_make_ctx(co_ctx_t *ctx, void *stack, size_t size,
#                  void (*func)(void*), void *arg);
# 用r12存arg，r13存func，完全避免栈操作
# ==============================================
co_make_ctx:
    # 1. 所有callee-saved寄存器初始化为0，避免垃圾值
    movq $0,  0(%rdi)   # rbx = 0
    movq $0,  8(%rdi)   # rbp = 0
    movq %r8, 16(%rdi)  # r12 = arg（第5个参数在r8）
    movq %rcx, 24(%rdi) # r13 = func（第4个参数在rcx）
    movq $0, 32(%rdi)   # r14 = 0
    movq $0, 40(%rdi)   # r15 = 0

    # 2. 计算栈顶并16字节对齐（栈从高地址向低地址增长）
    movq %rsi, %rax
    addq %rdx, %rax
    andq $-16, %rax     # 严格16字节对齐，符合System V ABI
    movq %rax, 48(%rdi) # rsp = 栈顶

    # 3. 设置入口函数
    leaq co_ctx_entry(%rip), %rax
    movq %rax, 56(%rdi) # rip = 入口包装函数

    ret

# ==============================================
# 协程入口包装：从r12取参数，调用r13里的函数
# ==============================================
co_ctx_entry:
    movq %r12, %rdi     # 第一个参数 = arg
    callq *%r13         # 调用业务函数 exec
    # 正常业务不会返回到这里（exec结尾会co_yield）
    hlt