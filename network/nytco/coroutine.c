//#define _GNU_SOURCE
#include<stdio.h>
#include<stdlib.h>
#include<ucontext.h>
#include<sys/socket.h>
#include<dlfcn.h>
#include<string.h>
#include<unistd.h>
#include<poll.h>
#include <assert.h>
#include"coroutine.h"
#include<errno.h>
 ssize_t (*real_recv)(int ,void *,size_t,int)=recv;
 ssize_t (*real_send)(int,const void*,size_t,int)=send;
 int (*real_accept)(int,struct sockaddr*,socklen_t*)=accept;
static __thread schedule* tls_sched;
 void hook_init(){
    return;
    // real_recv=dlsym(RTLD_NEXT, "recv");
    // real_accept = dlsym(RTLD_NEXT, "accept");
    // real_send=dlsym(RTLD_NEXT, "send");
}
//  ssize_t recv(int fd,void *buffer,size_t len,int flag){
//     coroutine* cor=tls_sched->co_list[fd];
//     while(1){
//         struct pollfd pfd;
//         pfd.fd=fd;
//         pfd.events=POLLIN;
//         int ret=poll(&pfd,1,0);
//         if(ret>0){
//             int n=real_recv(fd,buffer,len,flag);
//             return n;
//         }else if(ret == 0){
//            //printf("clientfd wait:%d\n",fd);
//             if(cor->status!=WAIT){
//             cor->fd=fd;
//             cor->status=WAIT;
//             cor->main->co_waiting++;
//             struct epoll_event ev;
//             ev.data.fd=fd;
//             ev.events=EPOLLIN;
//             epoll_ctl(tls_sched->epfd,EPOLL_CTL_ADD,fd,&ev);
//             //memset(&cor->rb_node, 0, sizeof(struct rb_node));
//             rb_insert(&tls_sched->rb_wait,&cor->rb_node,cmp_fd);
//             assert(rbtree_validate(&tls_sched->rb_wait));
//             }
//            // printf("让出\n");
//             co_yield(cor);
//         }else{
//             perror("recv");
//             return -1;
//         }
//     }
// }
ssize_t co_recv(int fd, void *buffer, size_t len, int flag) {
    coroutine* cor = tls_sched->co_list[fd];
    while (1) {
        ssize_t n = real_recv(fd, buffer, len, flag);
        if (n >= 0) return n;
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("recv");
            return -1;
        }
        // 真正阻塞，进入等待
        if (cor->status != WAIT) {
            cor->status = WAIT;
            cor->main->co_waiting++;
            // 注意：常驻模式下不需要重复ADD epoll
            struct epoll_event ev = {.events = EPOLLIN, .data.fd = fd};
            epoll_ctl(tls_sched->epfd, EPOLL_CTL_ADD, fd, &ev);
            rb_insert(&tls_sched->rb_wait, &cor->rb_node, cmp_fd);
        }
        co_yield(cor);
    }
}
ssize_t co_send(int fd,const void *buffer,size_t len,int flag){
    coroutine* cor=tls_sched->co_list[fd];
    while(1){
        ssize_t n=real_send(fd,buffer,len,flag);
        if(n>=0) return n;
        if(errno!=EAGAIN&& errno!=EWOULDBLOCK){
            perror("send");
            return -1;
        }
        if(cor->status!=WAIT){
            cor->status = WAIT;
            cor->main->co_waiting++;
            // 注意：常驻模式下不需要重复ADD epoll
            struct epoll_event ev = {.events = EPOLLOUT, .data.fd = fd};
            epoll_ctl(tls_sched->epfd, EPOLL_CTL_ADD, fd, &ev);
            rb_insert(&tls_sched->rb_wait, &cor->rb_node, cmp_fd);
        }
        co_yield(cor);
    }
    // while(1){
    //     struct pollfd pfd;
    //     pfd.fd=fd;
    //     pfd.events=POLLOUT;
    //     int ret=poll(&pfd,1,0);
    //     if(ret>0){
    //         return real_send(fd,buffer,len,flag);
    //     }else if(ret == 0){
    //         if(cor->status!=WAIT){
    //         cor->fd=fd;
    //         cor->status=WAIT;
    //         struct epoll_event ev;
    //         ev.data.fd=fd;
    //         ev.events=EPOLLOUT;
    //         epoll_ctl(tls_sched->epfd,EPOLL_CTL_ADD,fd,&ev);
    //         //memset(&cor->rb_node, 0, sizeof(struct rb_node));
    //         rb_insert(&tls_sched->rb_wait,&cor->rb_node,cmp_fd);
    //         assert(rbtree_validate(&tls_sched->rb_wait));
    //         }
    //         co_yield(cor);
    //     }else{
    //         perror("send");
    //         return -1;
    //     }
    // }
}
int co_accept(int fd, struct sockaddr* addr, socklen_t* addrlen)
{
    coroutine *cor=tls_sched->co_list[fd];
    while(1){
        ssize_t n=real_accept(fd,addr,addrlen);
        if(n>=0) {
            setnonblock(n);
            return n;
        }
        if(errno!=EAGAIN&& errno!=EWOULDBLOCK){
            perror("send");
            return -1;
        }
        if(cor->status!=WAIT){
            cor->status = WAIT;
            cor->main->co_waiting++;
            // 注意：常驻模式下不需要重复ADD epoll
            struct epoll_event ev = {.events = EPOLLIN, .data.fd = fd};
            epoll_ctl(tls_sched->epfd, EPOLL_CTL_ADD, fd, &ev);
            rb_insert(&tls_sched->rb_wait, &cor->rb_node, cmp_fd);
        }
        co_yield(cor);
    }
    // while (1) {
    //     struct pollfd pfd = {.fd = fd, .events = POLLIN};
    //     int ret = poll(&pfd, 1, 0);

    //     if (ret > 0) {
    //         int clientfd= real_accept(fd, addr, addrlen);
    //         return clientfd;
    //     }

    //     if (ret == 0) {
    //         if(co->status!=WAIT){
    //         co->fd = fd;
    //         co->status = WAIT;
    //         co->main->co_waiting++;
    //         struct epoll_event ev = {.events = EPOLLIN, .data.fd = fd};
    //         epoll_ctl(tls_sched->epfd, EPOLL_CTL_ADD, fd, &ev);
    //         //memset(&co->rb_node, 0, sizeof(struct rb_node));
    //         rb_insert(&tls_sched->rb_wait, &co->rb_node, cmp_fd);
    //         assert(rbtree_validate(&tls_sched->rb_wait));
    //         }
    //         co_yield(co);
    //     } else {
    //         return -1;
    //     }
    // }
}

coroutine* current;
void co_yield(coroutine* cor){
    INFO("进入co_yield，fd=%d\n", cor->fd); // 加这行
    cor->main->current=NULL;
    #if UCONTEXT
    swapcontext(&cor->ctx,&cor->main->main_ctx);
    #else
    co_swap_ctx(&cor->ctx, &cor->main->main_ctx);
    #endif
}
void co_resume(coroutine *cor){
    INFO("进入co_resume,fd=%d\n",cor->fd);
    cor->main->current = cor;
    cor->status = RUN;
    #if UCONTEXT
    swapcontext(&cor->main->main_ctx,&cor->ctx);
    #else
    co_swap_ctx(&cor->main->main_ctx, &cor->ctx);
    #endif
    cor->main->current = NULL;
}
void exec(void* lt){
    coroutine *cor =(coroutine*)lt;
    cor->status=RUN;
    cor->func(cor->arg);
    cor->status=FIN;
    cor->main->co_total--;
    // swapcontext(&cor->ctx,&cor->main->main_ctx);
    co_yield(cor);
}
coroutine* create_cor(schedule* sched,int fd,void (*func)(void *),void *arg){
    coroutine *cor=(coroutine*)malloc(sizeof(coroutine));
    memset(cor, 0, sizeof(coroutine));
    cor->fd=fd;
    cor->func=func;
    cor->arg=arg;
    cor->stack_size=STACK_SIZE;
    cor->main=sched;
    cor->main->co_total++;
    sched->co_list[fd]=cor;
    cor->status=NREADY;
    #if UCONTEXT
    getcontext(&cor->ctx);
    cor->ctx.uc_stack.ss_sp=cor->stack;
    cor->ctx.uc_stack.ss_size=cor->stack_size;
    cor->ctx.uc_link=&sched->main_ctx;
    makecontext(&cor->ctx,(void(*)(void))exec,1,(void*)cor);
    #else
    //co_get_ctx(&cor->ctx);
    co_make_ctx(&cor->ctx, cor->stack, cor->stack_size, (void(*)(void*))exec, cor);
    #endif
    list_add_tail(&cor->ready_node, &sched->ready_queue);
    return cor;
}
schedule* sched_create(void){
    schedule* sched=(schedule*)malloc(sizeof(schedule));
    #if UCONTEXT
    getcontext(&sched->main_ctx);
    #endif
    sched->current=NULL;
    sched->co_total=0;
    sched->co_waiting=0;
    sched->epfd=epoll_create(1);
    memset(&sched->events,0,EPOLL_EVENT);
    memset(&sched->co_list,0,CO_LIST);
    INIT_LIST_HEAD(&sched->ready_queue);
    sched->rb_wait = (struct rb_root)RB_ROOT_INIT;
    return sched;
}
void sched_destroy(schedule* shed){
    close(shed->epfd);
    free(shed);
    return;
}
// 前置：wait树比较函数
int cmp_fd(const struct rb_node* a, const struct rb_node* b)
{
    coroutine* ca = rb_entry(a, coroutine, rb_node);
    coroutine* cb = rb_entry(b, coroutine, rb_node);
    return ca->fd - cb->fd;
}

int cmp_fd_key(const struct rb_node* node, const void* key)
{
    coroutine* co = rb_entry(node, coroutine, rb_node);
    int fd = *(const int*)key;
    return co->fd - fd;
}
void rb_dump(struct rb_node* node) {
    if (!node) return;
    rb_dump(node->rb_left);
    coroutine* co = rb_entry(node, coroutine, rb_node);
    printf("%d ", co->fd);
    rb_dump(node->rb_right);
}
// 带遍历上限的安全版本，防止链表成环时死循环
void print_ready_queue_safe(schedule* sched) {
    if (list_empty(&sched->ready_queue)) {
        printf("[ready_queue] 队列为空\n");
        return;
    }

    printf("[ready_queue] ========== 队列元素 ==========\n");
    struct list_head* pos,*n;
    int index = 0;

    const int MAX_LOOP = 10000; // 最大遍历次数，防成环卡死

    list_for_each_safe(pos,n, &sched->ready_queue) {
        if (index >= MAX_LOOP) {
            printf("  !!! 遍历超过上限，疑似链表成环！\n");
            break;
        }

        coroutine* co = list_entry(pos, coroutine, ready_node);
        const char* status_str;
        switch (co->status) {
            case NREADY: status_str = "NREADY"; break;
            case WAIT:   status_str = "WAIT";   break;
            case RUN:    status_str = "RUN";    break;
            case FIN:    status_str = "FIN";    break;
            default:     status_str = "UNKNOWN";
        }

        printf("  [%02d] 协程地址: %p | fd: %4d | 状态: %s\n",
               index++, co, co->fd, status_str);
    }

    printf("[ready_queue] 遍历结束，计数 %d 个元素\n\n", index);
}


static void sched_process_io(schedule* sched, int nfds)
{
    // printf("当前树中序：");
    // rb_dump(sched->rb_wait.rb_node);
    // printf("\n");
    for (int i = 0; i < nfds; i++) {
        int fd = sched->events[i].data.fd;
        struct rb_node* node = rb_find(&sched->rb_wait, &fd, cmp_fd_key);
        if (!node) {
        printf("警告：fd=%d 在等待树中找不到！\n", fd);
        continue;
        }else{
            //printf("rb_find在等待树中找到了节点\n");
        }
        coroutine* co = rb_entry(node, coroutine, rb_node);//从节点返回协程指针
        INFO("事件fd=%d, 查到协程fd=%d, 协程地址=%p, 节点地址=%p\n", fd, co->fd, co, node);
        INFO("准备进入rb_erase\n");
        rb_erase(node, &sched->rb_wait);          // 从wait树移除
        epoll_ctl(sched->epfd, EPOLL_CTL_DEL, fd, NULL);
        memset(node, 0, sizeof(struct rb_node));
        INFO("exit rb_erase\n");
        // printf("当前树中序：");
        // rb_dump(sched->rb_wait.rb_node);
        // printf("\n");
        assert(rbtree_validate(&sched->rb_wait)); // 删除后校验
        memset(node, 0, sizeof(struct rb_node));
        co->status = NREADY;
        co->main->co_waiting--;
        list_add_tail(&co->ready_node, &sched->ready_queue); // 加入就绪队列
    }
    // printf("红黑树被修改后当前树中序：");
    // rb_dump(sched->rb_wait.rb_node);
    // printf("\n");
 //print_rbtree_level_shape(&sched->rb_wait);
}
static void sched_run_ready(schedule* sched)
{
    struct list_head *pos, *n;
    //print_ready_queue_safe(sched);
    list_for_each_safe(pos, n, &sched->ready_queue) {
        coroutine* co = list_entry(pos, coroutine, ready_node);
        list_del(pos);
        INFO("准备进入co_resume\n");
        co_resume(co);
        INFO("exit co_resume\n");
        if (co->status == FIN) {
            free(co);
        } else if (co->status == NREADY) {
            list_add_tail(&co->ready_node, &sched->ready_queue);
        }
        // CORO_WAIT状态已经在wait树里，不用处理
    }

}
void sched_loop(schedule* sched){
    tls_sched=sched;
    while(1){
        while (!list_empty(&sched->ready_queue)) {
            INFO("准备进入sche_run_ready\n");
            sched_run_ready(sched);
            INFO("exit sched_run_ready\n");
        }
         if (rb_empty(&sched->rb_wait)) {
            break;
        }
        INFO("等待io事件\n");
        int nfds=epoll_wait(sched->epfd,sched->events,EPOLL_EVENT,-1);
        INFO("有io事件\n");
        if(nfds>0){
            INFO("准备进入sched_process_io\n");
            sched_process_io(sched,nfds);
            INFO("exit sched_process_io\n");
        }
        
    }
}



