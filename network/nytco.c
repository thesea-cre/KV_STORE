#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "coroutine.h" // 你的协程库头文件
#include<sys/time.h>
#include"server.h"
#include"sync.h"//MasterMgr/Slaveconn/slave_remove_by_fd，master_notify_slaves要用
#define INFO printf
#if NETWORK_SELECT == ENABLE_NTYCO//整文件只在选中协程模型时编译，与reactor/uring互斥，避免master_notify_slaves撞链接

#define SLAVE_PORT 8000//与reactor.h保持一致；监听/连接从节点用
#define BUF_SIZE SIZE
#define MAX_CLIENT_NUM MAX_CONNECT
#define PORT_NUM MAX_PORT
schedule* tls_sched;
struct timeval tv_begin;
void accept_slave_worker(void* arg);
void accept_worker(void* arg);
static void hex_dump(const void *data, size_t size, const char *title) {
    const uint8_t *buf = (const uint8_t *)data;
    size_t i, j;

    // 打印标题和总长度
    if (title != NULL) {
        printf("--- %s (总长度: %zu 字节) ---\n", title, size);
    }

    // 每行16字节
    for (i = 0; i < size; i += 16) {
        // 1. 打印偏移地址（4位十六进制）
        printf("%04zx: ", i);

        // 2. 打印十六进制部分
        for (j = 0; j < 16; j++) {
            if (i + j < size) {
                printf("%02X ", buf[i + j]);
            } else {
                printf("   "); // 最后一行不足16字节，补空格对齐
            }
            // 第8字节后加一个空格分隔，更易读
            if (j == 7) {
                printf(" ");
            }
        }

        printf(" |");

        // 3. 打印ASCII字符部分
        for (j = 0; j < 16; j++) {
            if (i + j >= size) break;
            uint8_t c = buf[i + j];
            // 只打印可打印ASCII字符，其他用点代替
            if (c >= 0x20 && c <= 0x7E) {
                printf("%c", c);
            } else {
                printf(".");
            }
        }

        printf("|\n");
    }
}
int nytco_init_port(char *ip,unsigned int port){
    int socketfd=socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in addr;
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=inet_addr(ip);
    addr.sin_port=htons(port);
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
    if(port==SLAVE_PORT){
        create_cor(tls_sched,socketfd, accept_slave_worker, p_listen_fd);//为每个fd创建一个协程
    }else{
        create_cor(tls_sched,socketfd, accept_worker, p_listen_fd);//为每个fd创建一个协程
    }
    
    INFO("exit create_cor\n");
    return socketfd;
}
void client_handler(void *arg){
    int fd = *(int*)arg;
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
        hex_dump(buf,n,"服务器收数据\n");
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
        while(1){//不断地解包发包，直到解析完或者需要更多信息
            int proc_ret=kv_request(rbuffer,wbuffer,rlength,&wlength);
            if (proc_ret ==-2) {
                printf("fd %d invalid packet\n", fd);
                goto close_conn; // 直接跳出，关闭连接
            }else if (proc_ret == 0||proc_ret==-1) {
                break; // 解析完或者半包
            }
            rlength-=proc_ret;//kv_request返回的是消费了多少接收字节，推进接收缓冲区
            if(rlength>0){
                memmove(rbuffer,rbuffer+proc_ret,rlength);
            }
        }
        if(wlength>0){
            int ret=co_send(fd, wbuffer, wlength, 0);
            hex_dump(wbuffer,wlength,"服务器发数据");
            if (ret <= 0) {
                printf("fd %d send error\n", fd);
                goto close_conn;
            }
            wlength-=ret;
            if(wlength>0){//部分发送：残留前移，下轮kv_request会追加在后面
                memmove(wbuffer,wbuffer+ret,wlength);
            }
        }
        #endif
       // printf("recv from client:%s,%ld(bytes)\n",buf,n);//业务执行完返回一个值，拿这个值去判断执行完没有
    }
close_conn:
    close(fd);
}
//作为从节点，创建时connect后唤醒使用
void slave_handler(void *arg){//从节点调用，和主节点通信
    int fd = *(int*)arg;
    free(arg);
    char buf[BUF_SIZE]={0};
    char rbuffer[BUF_SIZE]={0};
    int rlength=0;
    //协议由从节点先开口：进recv循环前先把PSYNC发出去（slave_send_sync返回组好的字节数）
    int psync_len=slave_send_sync(&slave,buf,BUF_SIZE);
    if(psync_len>0){
        int set=co_send(fd,buf,psync_len,0);
        hex_dump(buf,set,"从节点发数据");
        if(set<=0){
            goto close_conn;
        }
    }
    while (1) {
        ssize_t n = co_recv(fd, buf, BUF_SIZE, 0);
        hex_dump(buf,n,"从节点收数据");
        if (n <= 0) {
            goto close_conn;
        }
        if (rlength + n > BUF_SIZE) {
            printf("fd %d recv buffer overflow\n", fd);
            goto close_conn;
        }
        memcpy(rbuffer+rlength,buf,n);//拼包
        rlength+=n;
        while(1){
            //slave_process返回的是"本次消费了多少接收字节"，不是用来发送的长度；
            //三个状态（收文件/补发执行/在线执行）全是消费输入，推进缓冲区即可
            int proc_ret=slave_process(&slave,rbuffer,rlength);
            if (proc_ret == -2) {
                printf("fd %d invalid packet\n", fd);
                goto close_conn;
            }
            else if (proc_ret == 0 || proc_ret == -1) {
                break; // 消费完/半包，回去recv
            }
            rlength-=proc_ret;
            if(rlength>0){
                memmove(rbuffer,rbuffer+proc_ret,rlength);
            }
        }
    }
close_conn:
    close(fd);
}
//写路径传播钩子（协程版）：kv_protocol主节点写分支调用。
//给发送态(2/3)的从节点fd对应的协程发signal，唤醒sleep中的master_handler去发数据；
//不在SLEEP态返回-1直接忽略——数据已在积压区，协程下一轮发数据自然会取到，唤醒不丢
void master_notify_slaves(MasterMgr *mgr) {
    if (!mgr) return;
    Slaveconn *cur = mgr->slave_head ? mgr->slave_head->next : NULL;//虚拟头节点之后才是真实节点
    while (cur) {
        if ((cur->status == 2 || cur->status == 3) && cur->fd >= 0) {
            co_cond_signal(cur->fd);
        }
        cur = cur->next;
    }
}
//主节点，initSLAVE_PORT后，accept_slave_worker唤醒
void master_handler(void *arg){//主节点调用
    int fd = *(int*)arg;
    free(arg);
    char buf[BUF_SIZE]={0};
    char rbuffer[BUF_SIZE]={0};
    int rlength=0;
    //阶段一：握手，等从节点的PSYNC（心跳未实现，从节点之后不会再发数据）
    while (1) {
        ssize_t n = co_recv(fd, buf, BUF_SIZE, 0);
        hex_dump(buf,n,"主节点收数据");
        if (n <= 0) {
            goto close_conn;
        }
        if (rlength + n > BUF_SIZE) {
            printf("fd %d recv buffer overflow\n", fd);
            goto close_conn;
        }
        memcpy(rbuffer+rlength,buf,n);//拼包
        rlength+=n;
        int proc_ret=master_handle_sync(&mgr,fd,rbuffer,rlength);
        if (proc_ret == -2) {
            printf("fd %d invalid packet\n", fd);
            goto close_conn;
        }else if (proc_ret == 0 || proc_ret == -1) {
            continue; // 半包/未识别，继续recv凑齐
        }
        //proc_ret==20：PSYNC已消费，推进接收缓冲区
        rlength-=proc_ret;
        if(rlength>0){
            memmove(rbuffer,rbuffer+proc_ret,rlength);
        }
        break;
    }
    //阶段二：发送状态机。主动权在发送侧，sync函数返回正数就一直发，追平才睡
    Slaveconn *slave=slave_find_by_fd(&mgr,fd);
    if(!slave){
        goto close_conn;
    }
    while (1) {
        int ret=0;
        if(slave->status==1){
            ret=master_full_sync(&mgr,slave,"aof_db",buf,BUF_SIZE);
        }else if(slave->status==2){
            ret=master_incremental_sync(&mgr,slave,buf,BUF_SIZE);
        }else if(slave->status==3){
            ret=master_online_sync(&mgr,slave,buf,BUF_SIZE);
        }else{
            break;
        }
        if(ret<0){//上层出错
            goto close_conn;
        }
        if(ret==0){
            if(slave->status==4){//online_sync淘汰：落后过多（从节点重连会走全量）
                goto close_conn;
            }
            co_cond_sleep();//追平，睡到kv写路径notify
            continue;
        }
        int sent=0;
        while(sent<ret){//fd非阻塞，可能部分发送，必须循环补发；sync函数填buf时已推进slave_offset，漏发=永久丢数据
            int set=co_send(fd,buf+sent,ret-sent,0);
            if(set<=0){//断线
                goto close_conn;
            }
            sent+=set;
        }
        hex_dump(buf,ret,"主节点发数据");
    }
close_conn:
    slave_remove_by_fd(&mgr,fd);//幂等：链表里还有就摘掉（PSYNC已建节点/淘汰/断线各路径统一在此清理）
    close(fd);
}
void accept_slave_worker(void* arg){
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
        create_cor(tls_sched,conn_fd, master_handler, pfd);
    }
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
    addr.sin_addr.s_addr=inet_addr("192.168.72.129");
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
    if(s_or_c==0){
        nytco_init_port(ip,SLAVE_PORT);
        INFO("从节点接入端口 %d\n",SLAVE_PORT);
        
    }else if(s_or_c==1){//从节点
        int socketfd=connect_server();
        if(socketfd<0){
            printf("connect master failed\n");
            return -1;
        }
        slave.master_fd=socketfd;//回填，slave_send_sync要检查它
        int *pfd=(int *)malloc(sizeof(int));
        *pfd=socketfd;
        create_cor(tls_sched,socketfd, slave_handler, pfd);
    }
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