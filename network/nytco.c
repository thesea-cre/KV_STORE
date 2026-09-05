#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "coroutine.h" // 你的协程库头文件
#include<sys/time.h>
#include"server.h"
#define BUF_SIZE SIZE
#define MAX_CLIENT_NUM MAX_CONNECT
#define PORT_NUM MAX_PORT
schedule* tls_sched;
struct timeval tv_begin;
void client_handler(void* arg) {
     
    int fd = *(int*)arg;
    //printf("客户端协程启动，fd=%d\n", fd); // 加这行
    free(arg);

    char buf[BUF_SIZE]={0};
    char wbuffer[BUF_SIZE]={0};
    int wlength=0;
    char rbuffer[BUF_SIZE]={0};
    int rlength=0;
    char res_buf[BUF_SIZE]={0};
    while (1) {
        // 调用Hook后的recv，阻塞时自动让出CPU
        ssize_t n = co_recv(fd, buf, BUF_SIZE, 0);
        if (n <= 0) {
            break;
        }
        if (rlength + n > BUF_SIZE) {
            printf("fd %d recv buffer overflow\n", fd);
            break;
        }
        #if ENABLEKV
            memcpy(rbuffer+rlength,buf,n);//这里处理拆包的方式:追加包，先放到一个暂时的缓冲区
            rlength+=n;
        #endif
        #if ENABLEKV
        while(1){
            int proc_ret=kv_request(rbuffer,wbuffer,&rlength,&wlength);
            if (proc_ret < 0) {
                printf("fd %d invalid packet\n", fd);
                goto close_conn; // 直接跳出，关闭连接
            }
            if (proc_ret == 0) {
                break; // 没有完整包了，退出解包循环
            }
            if(wlength>0){
                int ret=co_send(fd, wbuffer, wlength, 0);
                if (ret <= 0) {
                    printf("fd %d send error\n", fd);
                    goto close_conn;
                }
                wlength-=ret;
                if(wlength>0){
                    memmove(wbuffer,wbuffer+ret,wlength);
                }
            }
        }
        #endif
       // printf("recv from client:%s,%ld(bytes)\n",buf,n);//业务执行完返回一个值，拿这个值去判断执行完没有
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
close_conn:
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
        int conn_fd = co_accept(listen_fd, (struct sockaddr*)&client_addr, &len);
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

int nytco(unsigned int port) {
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