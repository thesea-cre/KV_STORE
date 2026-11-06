#include<stdio.h>
#include<sys/socket.h>
#include<unistd.h>
#include<string.h>
#include<stdlib.h>
#include<errno.h>
#include<sys/epoll.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<sys/time.h>
#include"kvstore.h"
#include"tool.h"
#include"upload.h"
#include"resp.h"
#if AOF
#include"aof.h"
extern AOFcontext_ctx* ctx;
extern char* huge_buf;
extern int huge_buf_len;
extern int huge_num;
#endif
#if SYNC
#include"sync_new.h"
extern MasterMgr *mgr;
int recv_from_slave_cb(int fd);
int recv_from_server_cb(int fd);
void notify(int fd);
int accept_slave_cb(int fd);

#endif
#define LISTENFD 1
#define LISTEN_CLIENT LISTENFD
#define LISTEN_SLAVE 4
#define CLIENTFD 0
#define SERVER_MASTER 2
#define SERVER_SLAVE 3
#define EVENT_SIZE 1024
typedef int (*ECALLBACK)(int fd);
struct conn{
    int fd;
    //status=0,初态。status=1，接收态，status=2,发送，status=2,处理态，status=-1,关闭
    int status;//
    int is_listenfd;
    char rbuffer[BUFFER_SIZE];//应用层数据累积缓冲区，这里该考虑一下环形缓冲区，这样可以好移除已经处理的数据，实际这里用的是线性缓冲区
    int rlength;//已经累积的数据的长度
    char wbuffer[BUFFER_SIZE];
    int wlength;//
    //result res;
    ECALLBACK send_callback;
    union{
        ECALLBACK recv_callback;//读回调是业务入口
        ECALLBACK accept_callback;//写回调是网络出口
    }r_action;
};
#if NETWORK_SELECT == ENABLE_REACTOR//只在选中epoll时编译，与uring_tcp_server.c互斥，避免同名函数撞链接
int epfd; 

int accept_cb(int fd);
int recv_cb(int fd);
int send_cb(int fd);
static struct timeval begin;
extern int s_or_c;

struct conn connection[MAX_CONNECT]={0};

int init_server(char*ip,int port){//初始化服务器
    int socketfd=socket(AF_INET,SOCK_STREAM,0);//socket()
    if(socketfd<0){
        perror("socket");
        return -1;
    }
    struct sockaddr_in addr;
    memset(&addr,0,sizeof(addr));//用结构体sockaddr_in填充地址，断口，协议类型
    addr.sin_family=AF_INET;
    addr.sin_port=htons(port);
    addr.sin_addr.s_addr=inet_addr(ip);
    int opt = 1;
    setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if(bind(socketfd,(struct sockaddr*)&addr,sizeof(struct sockaddr_in))){//再bind()这个socketfd
        perror("bind");
        return -1;
    }
    if(listen(socketfd,5)<0){//再listen（）,把这个socketfd设置成监听fd
        perror("listen");
        return -1;
    }
    printf("listen over\n");
    return socketfd;
}

int connect_server(void){
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
    if(connect(socketfd,(struct sockaddr*)&addr,sizeof(struct sockaddr_in))<0){
        perror("connect");
        return -1;
    }//connect自动帮你填自己的ip,port,也可以自己bind
    return socketfd;
}
int set_event(int fd,int event){//传fd和要设置的事件类型，设置fd监听事件类型，然后加入到epoll中
    struct epoll_event ev;//用一个epoll_event的结构体
    ev.data.fd= fd;
    ev.events=event;
    if (epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev) < 0) {//调用epoll_ctl(),修改epoll
        if (errno == ENOENT) {
            if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
                perror("epoll_ctl ADD");
                return -1;
            }
        } else {
            perror("epoll_ctl MOD");
            return -1;
        }
    }
    return 0;
}
int cancel_event(int fd){//移除io函数
    if(epoll_ctl(epfd,EPOLL_CTL_DEL,fd,NULL)<0){
        printf("cancel_event failed\n");
        return -1;
    }
    return 0;
}
int event_register(int fd,int event,int flag){//事件注册函数，调用刚刚的set_event()，设置监听io事件类型，
    //把fd加入到事件表中
    connection[fd].fd=fd;
    memset(connection[fd].rbuffer,0,sizeof(connection[fd].rbuffer));//把收发缓冲区清空
    memset(connection[fd].wbuffer,0,sizeof(connection[fd].wbuffer));
    connection[fd].rlength=0;
    connection[fd].wlength=0;
    connection[fd].status=0;
    if(flag == LISTENFD){
        connection[fd].send_callback=send_cb;//赋值相应的回调函数
        connection[fd].r_action.accept_callback=accept_cb;
        connection[fd].is_listenfd=flag;
    }
    else if(flag == CLIENTFD){
        connection[fd].send_callback=send_cb;//赋值相应的回调函数
        connection[fd].r_action.recv_callback=recv_cb;
        connection[fd].is_listenfd=flag;
    }
    #if SYNC
    else if(flag==SERVER_MASTER){
        connection[fd].send_callback=send_cb;
        connection[fd].r_action.recv_callback=recv_from_server_cb;
        connection[fd].is_listenfd=flag;
    }
    else if(flag==SERVER_SLAVE){
        connection[fd].send_callback=send_cb;
        connection[fd].r_action.recv_callback=recv_from_slave_cb;
        connection[fd].is_listenfd=flag;
    }
    else if(flag==LISTEN_SLAVE){
        connection[fd].send_callback=send_cb;//赋值相应的回调函数
        connection[fd].r_action.accept_callback=accept_slave_cb;
        connection[fd].is_listenfd=flag;
    }
    #endif
    int se=set_event(fd,event);
    if(se==-1){
        return -1;
    }
    return 0;
}
int accept_cb(int fd){
    struct sockaddr_in addr;
    socklen_t addrlen=sizeof(struct sockaddr_in);

    int clientfd =accept(fd,(struct sockaddr*)&addr,&addrlen);//直接accept
    if(clientfd<0){
        printf("accept errno:%d ---> %s\n",errno,strerror(errno));
        return -1;
    }
    if(event_register(clientfd,EPOLLIN,CLIENTFD)){//将返回的fd，调用事件注册函数
        printf(" clientfd:%d of event_register is failed\n",clientfd);
        return -1;
    }
    #if TEST100w
    if(clientfd%10000==0){
        struct timeval current;
        gettimeofday(&current,NULL);
        int time=TIME_SUB_MS(current,begin);
        printf("Hello server:  clientfd-->%d,timeused:%d\n",clientfd,time);
        memcpy(&begin,&current,sizeof(struct timeval));
    }
    INFO("clientfd-->%d\n",clientfd);
    #endif
    return clientfd;
}
#if SYNC
int accept_slave_cb(int fd){
    struct sockaddr_in addr;
    socklen_t addrlen=sizeof(struct sockaddr_in);
    int slavefd =accept(fd,(struct sockaddr*)&addr,&addrlen);//直接accept
    
    if(slavefd<0){
        printf("accept errno:%d ---> %s\n",errno,strerror(errno));
        return -1;
    }


    force_flush(ctx,huge_buf,&huge_buf_len,&huge_num);
    aof_rewrite(ctx);
    off_t offset=0;
    sendfile(slavefd,ctx->db_fd,&offset,ctx->size);//同步阻塞发送

    if(event_register(slavefd,EPOLLIN,SERVER_SLAVE)){//将返回的fd，调用事件注册函数
        printf(" clientfd:%d of event_register is failed\n",slavefd);
        return -1;
    }

    Slaveconn *slave=(Slaveconn*)kv_malloc(sizeof(Slaveconn));
    memset(slave,0,sizeof(Slaveconn));
    slave->fd=slavefd;
    if(mgr->slave_head->next){
        slave->next=mgr->slave_head->next;
    }
    mgr->slave_head->next=slave;
    mgr->slave_count++;

    slave->rbuffer=connection[slavefd].rbuffer;
    slave->wbuffer=connection[slavefd].wbuffer;
    slave->rlength=&connection[slavefd].rlength;
    slave->wlength=&connection[slavefd].wlength;
    slave->buf_length=BUFFER_SIZE;

    
    return slavefd;
}
#endif
int recv_cb(int fd){//处理粘包，分包 
    int ret;
    ret=recv(fd,connection[fd].rbuffer+connection[fd].rlength,BUFFER_SIZE-connection[fd].rlength,0);//先recv到暂时缓冲区
    if(ret<0){
        perror("recv");
        cancel_event(fd);
        memset(&connection[fd],0,sizeof(struct conn));
        kv_upload_abort(fd);
        kv_resp_abort(fd);
        close(fd);
        return -1;
    }else if(ret == 0){
        INFO("fd: %d is diconnected\n",fd);
        cancel_event(fd);
        memset(&connection[fd],0,sizeof(struct conn));
        kv_upload_abort(fd);
        kv_resp_abort(fd);
        close(fd);
        return 0;
    }else{
        #if ENABLEKV
        int cons_=0;
        connection[fd].rlength+=ret;
        INFO("接受了%d bytes\n",ret);
        INFO("rlength=%d bytes \n",connection[fd].rlength);
        if(KV_server(fd,connection[fd].rbuffer,connection[fd].rlength,connection[fd].wbuffer,
        BUFFER_SIZE,&cons_,&connection[fd].wlength)<0){
            kv_upload_abort(fd);
            cancel_event(fd);
            memset(&connection[fd],0,sizeof(struct conn));
            close(fd);
            return -1;
        }
        if(cons_<connection[fd].rlength){
            memmove(connection[fd].rbuffer,connection[fd].rbuffer+cons_,connection[fd].rlength-cons_);
        }
        INFO("这次处理了%d bytes\n",cons_);
        connection[fd].rlength-=cons_;
        if(connection[fd].wlength==0&&kv_resp_pending(fd)){
            int n=kv_resp_next(fd,(uint8_t*)connection[fd].wbuffer,BUFFER_SIZE);
            if(n>0) connection[fd].wlength=n;
        }
        if(connection[fd].wlength>0){
            set_event(fd,EPOLLOUT);
        }else{
            set_event(fd,EPOLLIN);
        }
        #else
        INFO("服务端接受 %s, %d (bytes)\n");
        set_event(fd,EPOLLOUT);
        #endif
        return 0;
    }
}
int send_cb(int fd){
    #if TEST100w
    memcpy(connection[fd].wbuffer,connection[fd].rbuffer,connection[fd].rlength);
    connection[fd].wlength=connection[fd].rlength;
    #endif
    int set = send(fd,connection[fd].wbuffer,connection[fd].wlength,0);
    if(set<0){
        perror("send");
        cancel_event(fd);
        kv_upload_abort(fd);
        kv_resp_abort(fd);
        close(fd);
        return -1;
    }else if(set == 0){
        printf("fd: %d is diconnected\n",fd);
        memset(&connection[fd],0,sizeof(struct conn));
        cancel_event(fd);
        kv_upload_abort(fd);
        kv_resp_abort(fd);
        close(fd);
        return 0;
    }

    connection[fd].wlength-=set;
    if(connection[fd].wlength>0){                 /* 本段没发完：挪走已发部分继续 */
        memmove(connection[fd].wbuffer,connection[fd].wbuffer+set,connection[fd].wlength);
        set_event(fd,EPOLLOUT);
        return 0;
    }

    /* 本段发完：响应队列还有就继续拉 */
    if(kv_resp_pending(fd)){
        int n=kv_resp_next(fd,(uint8_t*)connection[fd].wbuffer,BUFFER_SIZE);
        if(n>0){
            connection[fd].wlength=n;
            set_event(fd,EPOLLOUT);
            return 0;
        }
    }

    /* 队列空：处理 rbuffer 里残留的请求 */
    if(connection[fd].rlength>0){
        int cons_=0;
        if(KV_server(fd,connection[fd].rbuffer,connection[fd].rlength,
                     connection[fd].wbuffer,BUFFER_SIZE,&cons_,&connection[fd].wlength)<0){
            cancel_event(fd);
            kv_upload_abort(fd);
            kv_resp_abort(fd);
            close(fd);
            return -1;
        }
        if(cons_<connection[fd].rlength){
            memmove(connection[fd].rbuffer,connection[fd].rbuffer+cons_,
                    connection[fd].rlength-cons_);
        }
        connection[fd].rlength-=cons_;
        if(connection[fd].wlength>0){
            set_event(fd,EPOLLOUT);
            return 0;
        }
    }

    set_event(fd,EPOLLIN);
    return 0;
}
#if SYNC
void notify(int fd){
    set_event(fd,EPOLLOUT);
}
int recv_from_slave_cb(int fd){
    int ret;
    ret=recv(fd,connection[fd].rbuffer+connection[fd].rlength,BUFFER_SIZE-connection[fd].rlength,0);//先recv到暂时缓冲区
    if(ret<0){
        perror("recv");
        cancel_event(fd);
        memset(&connection[fd],0,sizeof(struct conn));
        slave_close(mgr,fd);
        kv_upload_abort(fd);
        kv_resp_abort(fd);
        close(fd);
        return -1;
    }else if(ret == 0){
        INFO("fd: %d is diconnected\n",fd);
        cancel_event(fd);
        memset(&connection[fd],0,sizeof(struct conn));
        slave_close(mgr,fd);
        kv_upload_abort(fd);
        kv_resp_abort(fd);
        close(fd);
        return 0;
    }else{
        INFO("recv from slave %d (bytes)\n",ret);
        return 0;
    }
}
int recv_from_server_cb(int fd){
    int ret;
    ret=recv(fd,connection[fd].rbuffer+connection[fd].rlength,BUFFER_SIZE-connection[fd].rlength,0);//先recv到暂时缓冲区
    if(ret<0){
        perror("recv");
        cancel_event(fd);
        memset(&connection[fd],0,sizeof(struct conn));
        kv_upload_abort(fd);
        kv_resp_abort(fd);
        close(fd);
        return -1;
    }else if(ret == 0){
        INFO("fd: %d is diconnected\n",fd);
        cancel_event(fd);
        memset(&connection[fd],0,sizeof(struct conn));
        kv_upload_abort(fd);
        kv_resp_abort(fd);
        close(fd);
        return 0;
    }else{
        INFO("recv from slave %d (bytes)\n",ret);
        int cons_=0;
        int status=0;
        connection[fd].rlength+=ret;
        KV_sync(connection[fd].rbuffer,connection[fd].rlength,NULL,0,&cons_,&status);
        if(status==1){
            memmove(connection[fd].rbuffer,connection[fd].rbuffer+cons_,
                connection[fd].rlength-cons_);
        }
        connection[fd].rlength-=cons_;
        set_event(fd,EPOLLIN);
        return 0;
    }
}
#endif
int reactor_entry(char*ip,unsigned int port){
    struct epoll_event events[EVENT_SIZE]={0};
    epfd=epoll_create(1);
    for(int i=0;i<MAX_PORT;i++){//代码运行，直接调用init_server(),然后把返回的fd调用注册函数，将fd加入事件表
        int listenfd=init_server(ip,port+i);
        if(event_register(listenfd,EPOLLIN,LISTENFD)){
            printf("event_register listenfd failed\n");
            return -1;
        }
        printf("port %d is used\n",port+i);

    }

    if(s_or_c==0){//创建主服务器
        int listenfd=init_server(ip,SLAVE_PORT);
        if(event_register(listenfd,EPOLLIN,LISTEN_SLAVE)){
            printf("event_register listenfd failed\n");
            return -1;
        }
        INFO("从服务器接入端口：%d\n",SLAVE_PORT);
    }
    else if(s_or_c==1){//创建从服务器，创建和主服务器通信的fd
        int master_fd=connect_server();//连接主服务器
        if(master_fd<0){
            printf("connect master failed\n");
            return -1;
        }
        if(event_register(master_fd,EPOLLIN,SERVER_MASTER)){
            printf("event_register\n");
            return -1;
        }
    }

    gettimeofday(&begin,NULL);
    while(1){//进入主循环
        int nready=epoll_wait(epfd,events,EVENT_SIZE,-1);//阻塞在epoll_wait，等待就绪事件
        if(nready<0){
            perror("epoll_wait");
            return -1;
        }
        for(int i=0;i<nready;i++){//有就绪事件后for循环遍历就绪事件
            int fd=events[i].data.fd;
            if(connection[fd].is_listenfd==LISTENFD){//判断是listenfd还是clientfd
                connection[fd].r_action.accept_callback(fd);//Listenfd就调用对应的回调函数
            }
            else if(connection[fd].is_listenfd==LISTEN_SLAVE){
                connection[fd].r_action.accept_callback(fd);//Listenfd就调用对应的回调函数
            }
            else{
                if(events[i].events&EPOLLIN){
                    connection[fd].r_action.recv_callback(fd);//同理
                }
                if(events[i].events&EPOLLOUT){
                    connection[fd].send_callback(fd);
                }
            }
        }
    }
}


#endif//NETWORK_SELECT == ENABLE_REACTOR