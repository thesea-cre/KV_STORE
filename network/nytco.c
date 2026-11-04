#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include"kvstore.h"
#include "coroutine.h" // 你的协程库头文件
#include<sys/time.h>
#include"tool.h"
/******* */
#if AOF
#include"aof.h"
extern AOFcontext_ctx* ctx;
extern char* huge_buf;
extern int huge_buf_len;
extern int huge_num;
#endif
/******* */
#if SYNC
#include"sync_new.h"
extern MasterMgr *mgr;
extern int s_or_c;
void slave_handler(void *arg);
void master_handler(void *arg);
void accept_slave_worker(void* arg);
#endif
#if NETWORK_SELECT == ENABLE_NTYCO//整文件只在选中协程模型时编译，与reactor/uring互斥，避免master_notify_slaves撞链接

#define BUF_SIZE BUFFER_SIZE
#define MAX_CLIENT_NUM MAX_CONNECT
#define PORT_NUM MAX_PORT
schedule* tls_sched;
struct timeval tv_begin;
void accept_worker(void* arg);
int nytco_init_port(char *ip,unsigned int port){
    int socketfd=socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in addr;
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=inet_addr(ip);
    addr.sin_port=htons(port);
    int opt = 1;
    setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if(-1==bind(socketfd,(struct sockaddr*)&addr,sizeof(struct sockaddr))){
        perror("bind");
        return -1;
    }
    if(listen(socketfd,5)<0){
        perror("listen");
        return -1;
    }
    setnonblock(socketfd);
    int* p_listen_fd = malloc(sizeof(int));
    *p_listen_fd = socketfd;
    INFO("准备进入create_cor\n");
    #if SYNC
    if(port==SLAVE_PORT){
        create_cor(tls_sched,socketfd, accept_slave_worker, p_listen_fd);//为每个fd创建一个协程
    }else{
        create_cor(tls_sched,socketfd, accept_worker, p_listen_fd);//为每个fd创建一个协程
    }
    #endif
    //create_cor(tls_sched,socketfd, accept_worker, p_listen_fd);//为每个fd创建一个协程
    INFO("exit create_cor\n");
    return socketfd;
}
void client_handler(void *arg){
    int fd = *(int*)arg;
    free(arg);
    char wbuffer[BUF_SIZE]={0};
    int wlength=0;
    char rbuffer[BUF_SIZE]={0};
    int rlength=0;
    char res_buf[BUF_SIZE]={0};
    while (1) {
        // 调用Hook后的recv，阻塞时自动让出CPU
        int ret = co_recv(fd, rbuffer+rlength, BUF_SIZE-rlength, 0);
        if (ret <= 0) {
            goto close_conn;
        }
        #if KV_COMPLEX
            int cons_=0;
            rlength+=ret;
            KV_server(rbuffer,rlength,wbuffer,BUF_SIZE,&cons_,&wlength);
            if(cons_<rlength){
                memmove(rbuffer,rbuffer+cons_,rlength-cons_);
            }
            rlength-=cons_;
            if(wlength>0){
                int set=co_send(fd,wbuffer,wlength,0);
                INFO("server send to clientd:( %s)   %d(bytes)\n",wbuffer,set);
                wlength-=set;
                INFO("wbuffer中还有多少数据：%d\n",wlength);
            }
        #elif 
            INFO("recv %d (bytes)\n",ret);
            INFO("data: %s\n",rbuffer);
            wlength=sprintf(wbuffer,"confirm recv %d(bytes)\n");
            int set=co_send(fd,wbuffer,wlength,0);
        #endif
    }
close_conn:
    close(fd);
}
#if SYNC
void slave_handler(void *arg){//从节点调用，和主节点通信
    int fd = *(int*)arg;
    free(arg);
    char wbuffer[BUF_SIZE]={0};
    int wlength=0;
    char rbuffer[BUF_SIZE]={0};
    int rlength=0;
    char res_buf[BUF_SIZE]={0};
    while (1) {
        // 调用Hook后的recv，阻塞时自动让出CPU
        int ret = co_recv(fd, rbuffer+rlength, BUF_SIZE-rlength, 0);
        if (ret <= 0) {
            goto close_conn;
        }
        INFO("recv from slave %d (bytes)\n",ret);
    }
close_conn:
    slave_close(mgr,fd);
    close(fd);
}


void master_handler(void *arg){//主节点调用
    int fd = *(int*)arg;
    free(arg);
    char rbuffer[BUF_SIZE]={0};
    int rlength=0;
    //阶段一：握手，等从节点的PSYNC（心跳未实现，从节点之后不会再发数据）
    while (1) {
        int ret = co_recv(fd, rbuffer+rlength, BUF_SIZE-rlength, 0);
        if (ret <= 0) {
            goto close_conn;
        }
        INFO("recv from slave %d (bytes)\n",ret);
        int cons_=0;
        int status=0;
        rlength+=ret;
        KV_sync(rbuffer,rlength,NULL,0,&cons_,&status);
        if(status==1){
            memmove(rbuffer,rbuffer+cons_,rlength-cons_);
        }
        rlength-=cons_;
        return 0;
    }
close_conn:
    close(fd);
}
void accept_slave_worker(void* arg){
    int listen_fd = *(int*)arg;
    free(arg);
    char wbuffer[BUF_SIZE]={0};
    int wlength=0;
    char rbuffer[BUF_SIZE]={0};
    int rlength=0;
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);
    while (1) {
        // Hook后的accept，无连接时自动让出
        int conn_fd = co_accept(listen_fd, (struct sockaddr*)&client_addr, &len);
        if (conn_fd < 0) {
            perror("accept");
            continue;
        }
        
        force_flush(ctx,huge_buf,&huge_buf_len,&huge_num);
        aof_rewrite(ctx);
        off_t offset=0;
        sendfile(conn_fd,ctx->db_fd,&offset,ctx->size);//同步阻塞发送


        int* pfd = malloc(sizeof(int));
        *pfd = conn_fd;
        // 为每个连接创建独立协程
        create_cor(tls_sched,conn_fd, slave_handler, pfd);
        int slavefd=(*pfd);
        Slaveconn *slave=(Slaveconn*)kv_malloc(sizeof(Slaveconn));
        memset(slave,0,sizeof(Slaveconn));
        slave->fd=slavefd;
        if(mgr->slave_head->next){
            slave->next=mgr->slave_head->next;
        }
        mgr->slave_head->next=slave;
        mgr->slave_count++;
        slave->rbuffer=rbuffer;
        slave->wbuffer=wbuffer;
        slave->rlength=&rlength;
        slave->wlength=&wlength;
        slave->buf_length=BUFFER_SIZE;
        return;
    }
}
#endif
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
static int connect_server(void){
    int socketfd=socket(AF_INET,SOCK_STREAM,0);
    if(socketfd<0){
        perror("sockte");
        return -1;
    }
    struct sockaddr_in addr;
    memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET;
    addr.sin_port=htons(SLAVE_PORT);
    addr.sin_addr.s_addr=inet_addr(MASTER_IP);
    //socklen_t len = sizeof(addr);
    if(connect(socketfd,(struct sockaddr*)&addr,sizeof(struct sockaddr_in))<0){
        perror("connect");
        return -1;
    }
    return socketfd;
}
int nytco(char *ip,unsigned int port) {
    hook_init();
    tls_sched=sched_create();//创建全局调度器
    for(int i=0;i<PORT_NUM;i++){
        nytco_init_port(ip,port+i);
        INFO("port %d is used\n",port+i);
    }
    #if SYNC
        if(s_or_c==0){
            nytco_init_port(ip,SLAVE_PORT);
            INFO("从节点接入端口 %d\n",SLAVE_PORT);
            
        }else if(s_or_c==1){//从节点
            int socketfd=connect_server();
            if(socketfd<0){
                printf("connect master failed\n");
                return -1;
            }
            int *pfd=(int *)malloc(sizeof(int));
            *pfd=socketfd;
            create_cor(tls_sched,socketfd, master_handler, pfd);
        }
    #endif
    // 创建调度器并启动
    gettimeofday(&tv_begin, NULL);
    INFO("sched_loop\n");
    sched_loop(tls_sched);//启动调度器

    // 清理资源
    sched_destroy(tls_sched);
    //close(listen_fd);
    return 0;
}
#endif//NETWORK_SELECT == ENABLE_NTYCO