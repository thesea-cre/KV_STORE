#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "coroutine.h" // 你的协程库头文件
#include<sys/time.h>
#include"server.h"
#define BUF_SIZE 1024
#define MAX_CLIENT_NUM 1048576
#define PORT_NUM 20
#define TIME_SUB_MS(tv1, tv2)  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)
schedule* tls_sched;
static struct timeval tv_begin;
static struct conn connection[CONNECTION_SIZE]={0};//可以理解为一个fd事件表
static void accept_worker(void* arg);
static void client_handler(void* arg);
static int event_register(int fd);
int event_register(int fd){//事件注册函数，调用刚刚的set_event()，设置监听io事件类型，
    //把fd加入到事件表中
    connection[fd].fd=fd;
    memset(connection[fd].rbuffer,0,BUFFER_SIZE);//把收发缓冲区清空
    memset(connection[fd].wbuffer,0,BUFFER_SIZE);
    connection[fd].rlength=0;
    connection[fd].wlength=0;
    connection[fd].status=0;
    return 0;
}
void client_handler(void* arg) {
    int fd = *(int *)arg;
    INFO("客户端协程启动，fd=%d\n", fd); // 加这行
    free(arg);
    while (1) {
        // 调用Hook后的recv，阻塞时自动让出CPU
        memset(connection[fd].rbuffer,0,BUF_SIZE);
        ssize_t n = recv(fd, connection[fd].rbuffer, BUF_SIZE, 0);
        connection[fd].rlength=n;
        int s_ret=0;
        #if ENABLEKV
        int s_set=kv_request(&connection[fd]);
        connection[fd].wlength=s_set;
        #endif
        if (n <= 0) {
            break;
        }
       INFO("recv from client:%s,%ld(bytes)\n",connection[fd].rbuffer,n);//业务执行完返回一个值，拿这个值去判断执行完没有
       #if ENABLEKV
       kv_response(&connection[fd]);
       #endif
       s_ret= send(fd, connection[fd].wbuffer, connection[fd].wlength, 0);
       memset(connection[fd].wbuffer,0,BUF_SIZE);
    }
    close(fd);
}
// 监听连接协程
void accept_worker(void* arg) {
    int listen_fd = *(int *)arg;
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
        if(event_register(conn_fd)){//将返回的fd，调用事件注册函数
            printf(" clientfd:%d of event_register is failed\n",conn_fd);
            return;
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

int nytco(unsigned short port) {
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
    addr.sin_port = htons(port+i);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(listen_fd, 5) < 0) {
        perror("listen");
        return 1;
    }
    printf("Server listening on port %d\n", port+i);
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