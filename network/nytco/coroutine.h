#ifndef COROUTINE_H
#define COROUTINE_H
#include <ucontext.h>
#include <sys/epoll.h>
#include<fcntl.h>
#include "rbtree.h"
#include "list.h"
#define UCONTEXT 0
#ifndef INFO
#define INFO
#endif
// ========== 配置宏 ==========
#define STACK_SIZE (16*1024)    // 协程栈大小，当前1KB极易栈溢出，建议至少改为16384(16KB)
#define EPOLL_EVENT 1024   // epoll单次返回最大事件数
#define CO_LIST 1048576       // 协程数组最大容量，注意：fd值不能超过该数值
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
// ========== 协程状态枚举 ==========
typedef enum {
    NREADY,  // 就绪态：已加入就绪队列，等待调度
    RUN,     // 运行态：当前正在CPU上执行
    WAIT,    // 等待态：阻塞在IO上，挂在红黑树等待
    FIN,     // 结束态：业务函数执行完毕，待回收
    SLEEP    // 睡眠态：预留，暂未实现
} Coroutine_status;

// ========== 结构体前置声明 ==========
typedef struct schedule schedule;

// ========== 协程结构体 ==========
typedef struct coroutine {
    int fd;                     // 关联的文件描述符
    #if UCONTEXT
    ucontext_t ctx;             // 协程上下文（寄存器、栈指针等）
    #else 
    co_ctx_t ctx;
    #endif
    schedule* main;             // 所属调度器指针
    void* arg;                  // 业务函数参数
    void (*func)(void*);        // 业务函数入口
    Coroutine_status status;    // 协程当前状态
    int stack_size;             // 栈空间大小
    char stack[STACK_SIZE];     // 独立栈内存
    struct rb_node rb_node;     // 红黑树节点：用于IO等待队列
    struct list_head ready_node;// 链表节点：用于就绪队列
} coroutine;

// ========== 调度器结构体 ==========
struct schedule {
    #if UCONTEXT
    ucontext_t main_ctx;
    #else            // 主调度线程的上下文
    co_ctx_t main_ctx;
    #endif
    coroutine* current;             // 当前正在运行的协程
    int co_total;   // 总协程数
    int co_waiting; 
    coroutine* co_list[CO_LIST];    // 按fd索引的协程数组
    int epfd;                       // epoll实例的文件描述符
    struct epoll_event events[EPOLL_EVENT]; // epoll事件接收数组
    struct rb_root rb_wait;         // IO等待协程的红黑树根节点
    struct list_head ready_queue;   // 就绪协程的链表头
};

// ==================== 对外接口函数声明 ====================

// 协程主动让出CPU，切回调度器
void co_yield(coroutine* cor);

// 调度器恢复指定协程运行
void co_resume(coroutine* cor);

// 创建新协程并自动加入就绪队列
coroutine* create_cor(schedule* sched, int fd, void (*func)(void*), void* arg);

// 创建并初始化一个调度器实例
schedule* sched_create(void);

// 销毁调度器，释放资源
void sched_destroy(schedule* sched);

// 启动调度器主循环，阻塞运行直到所有协程结束
void sched_loop(schedule* sched);
int cmp_fd(const struct rb_node* a, const struct rb_node* b);
int cmp_fd_key(const struct rb_node* node, const void* key);
void hook_init();
static inline int setnonblock(int fd){
    int flags=fcntl(fd,F_GETFL,0);
    if(flags<0) return -1;
    return fcntl(fd,F_SETFL,flags|O_NONBLOCK);
}
int co_accept(int fd, struct sockaddr* addr, socklen_t* addrlen);
ssize_t co_recv(int fd, void *buffer, size_t len, int flag);
ssize_t co_send(int fd,const void *buffer,size_t len,int flag);
#endif // COROUTINE_H