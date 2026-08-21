#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "coroutine.h" // 你的协程库头文件
#include<sys/time.h>
#define PORT 2000
#define BUF_SIZE 1024
#define MAX_CLIENT_NUM 1048576
#define PORT_NUM 20
#define TIME_SUB_MS(tv1, tv2)  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)
schedule* tls_sched;
struct timeval tv_begin;
void client_handler(void* arg) {
     
    int fd = *(int*)arg;
    //printf("客户端协程启动，fd=%d\n", fd); // 加这行
    free(arg);

    char buf[BUF_SIZE];
    while (1) {
        // 调用Hook后的recv，阻塞时自动让出CPU
        ssize_t n = recv(fd, buf, BUF_SIZE, 0);
        if (n <= 0) {
            break;
        }
       // printf("recv from client:%s,%ld(bytes)\n",buf,n);//业务执行完返回一个值，拿这个值去判断执行完没有
        send(fd, buf, n, 0);
    }
    // struct rb_node* node = rb_find(&tls_sched->rb_wait, &fd, cmp_fd_key);
    // if (node) {
    //     rb_erase(node, &tls_sched->rb_wait);
    //     memset(node, 0, sizeof(struct rb_node));
    //     epoll_ctl(tls_sched->epfd, EPOLL_CTL_DEL, fd, NULL);
    //     tls_sched->co_waiting--;
    // }
    //cor->status=fin;
    //cor_destroy();
    //free(cor)
    close(fd);
}

// 监听连接协程
void accept_worker(void* arg) {
    int listen_fd = *(int*)arg;
    free(arg);
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);

    while (1) {
        // Hook后的accept，无连接时自动让出
        int conn_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &len);
        if (conn_fd < 0) {
            perror("accept");
            continue;
        }
        if (conn_fd % 10000 == 999) {

			struct timeval tv_cur;
			memcpy(&tv_cur, &tv_begin, sizeof(struct timeval));
			
			gettimeofday(&tv_begin, NULL);
			int time_used = TIME_SUB_MS(tv_begin, tv_cur);
			
			printf("client fd : %d, time_used: %d\n", conn_fd, time_used);
		}
        //printf("new connection: %d\n",conn_fd);

        int* pfd = malloc(sizeof(int));
        *pfd = conn_fd;
        // 为每个连接创建独立协程
        create_cor(tls_sched,conn_fd, client_handler, pfd);
    }
    
}

int main() {
    hook_init();
    tls_sched=sched_create();
    for(int i=0;i<PORT_NUM;i++){
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    setnonblock(listen_fd);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT+i);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(listen_fd, 5) < 0) {
        perror("listen");
        return 1;
    }
    printf("Server listening on port %d\n", PORT+i);
    int* p_listen_fd = malloc(sizeof(int));
    *p_listen_fd = listen_fd;
    INFO("准备进入create_cor\n");
    create_cor(tls_sched,listen_fd, accept_worker, p_listen_fd);
    INFO("exit create_cor\n");
	gettimeofday(&tv_begin, NULL);
    }
    // 创建调度器并启动
    
    printf("sched_loop\n");
    sched_loop(tls_sched);

    // 清理资源
    sched_destroy(tls_sched);
    //close(listen_fd);
    return 0;
}