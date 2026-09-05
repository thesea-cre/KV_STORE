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
#include"reactor.h"
int epfd;//定义一个全局的epoll，
int accept_cb(int fd,void *arg);
int recv_cb(int fd,void *arg);
int send_cb(int fd,void *arg);
static struct timeval begin;
struct conn connection[CONNECTION_SIZE]={0};//可以理解为一个fd事件表
int init_server(int port){//初始化服务器
    int socketfd=socket(AF_INET,SOCK_STREAM,0);//socket()
    if(socketfd<0){
        perror("socket");
        return -1;
    }
    struct sockaddr_in addr;
    memset(&addr,0,sizeof(addr));//用结构体sockaddr_in填充地址，断口，协议类型
    addr.sin_family=AF_INET;
    addr.sin_port=htons(port);
    addr.sin_addr.s_addr=htonl(INADDR_ANY);
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
    connection[fd].send_callback=send_cb;//赋值相应的回调函数
    memset(connection[fd].rbuffer,0,BUFFER_SIZE);//把收发缓冲区清空
    memset(connection[fd].wbuffer,0,BUFFER_SIZE);
    connection[fd].rlength=0;
    connection[fd].wlength=0;
    connection[fd].status=0;
    if(flag == LISTENFD){
        connection[fd].r_action.accept_callback=accept_cb;
        connection[fd].is_listenfd=1;
    }else if(flag == CLIENTFD){
        connection[fd].r_action.recv_callback=recv_cb;
        connection[fd].is_listenfd=0;
    }
    int se=set_event(fd,event);
    if(se==-1){
        return -1;
    }
    return 0;
}
int accept_cb(int fd,void *arg){
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
int recv_cb(int fd,void *arg){//流控，应用层反压
    char  buf[BUFFER_SIZE]={0};
    int ret;
    ret=recv(fd,buf,BUFFER_SIZE,0);//先recv到暂时缓冲区
    printf("收包长度：%d",ret);
    if(ret<0){
        perror("recv");
        cancel_event(fd);
        memset(&connection[fd],0,sizeof(struct conn));
        close(fd);
        return -1;
    }else if(ret == 0){
        INFO("fd: %d is diconnected\n",fd);
        cancel_event(fd);
        memset(&connection[fd],0,sizeof(struct conn));
        close(fd);
        return 0;
    }else{
        memcpy(connection[fd].rbuffer+connection[fd].rlength,buf,ret);//这里处理拆包的方式:追加包，先放到一个暂时的缓冲区
        connection[fd].rlength+=ret;
        INFO("recv from clientfd:clientfd--> %d,buffer-->%s\n",fd,connection[fd].rbuffer);
        #if ENABLEKV
        if(kv_request(connection[fd].rbuffer,connection[fd].wbuffer,&connection[fd].rlength,&connection[fd].wlength)<0){
            cancel_event(fd);
            memset(&connection[fd],0,sizeof(struct conn));
            close(fd);
            return -1;
        }
        #endif
            if(connection[fd].wlength>0){
                set_event(fd,EPOLLIN|EPOLLOUT);//要同时监听可读事件和可写事件，不然如果数据没来完，就会卡死
            }else{
                set_event(fd,EPOLLIN);
            }
        return 0;
    }
}
int send_cb(int fd,void *arg){
    
    #if TEST100w
    memcpy(connection[fd].wbuffer,connection[fd].rbuffer,connection[fd].rlength);
    connection[fd].wlength=connection[fd].rlength;
    #endif
    int set;
    set= send(fd,connection[fd].wbuffer,connection[fd].wlength,0);//send之后不一定是recv，我再send
    if(set<0){
        perror("send");
        cancel_event(fd);
        close(fd);
        return -1;
    }else if(set == 0){
        printf("fd: %d is diconnected\n",fd);
        memset(&connection[fd],0,sizeof(struct conn));
        cancel_event(fd);
        close(fd);
        return 0;
    }else{
        INFO("server send to clientd:( %s)   %d(bytes)\n",connection[fd].wbuffer,set);
        connection[fd].wlength -= set;  // 扣掉已经发给内核的字节
        if(connection[fd].wlength > 0){
            // 没发完，残留前移，继续保留写事件
            memmove(connection[fd].wbuffer, 
                connection[fd].wbuffer + set, 
                connection[fd].wlength);
            set_event(fd, EPOLLIN | EPOLLOUT);
        }
        if(kv_request(connection[fd].rbuffer,connection[fd].wbuffer,&connection[fd].rlength,&connection[fd].wlength)<0){
            cancel_event(fd);
            memset(&connection[fd],0,sizeof(struct conn));
            close(fd);
            return -1;
        }
        if(connection[fd].wlength>0){
                set_event(fd,EPOLLIN|EPOLLOUT);//要同时监听可读事件和可写事件，不然如果数据没来完，就会卡死
            }else{
                set_event(fd,EPOLLIN);
            }
    }
    // if(connection[fd].status==1){//发送的时候做一个简单的状态转换机，解决需要多次send的问题，不用多次调用send，而是设置它的
    //     //事件类型，在fd结构体里定义一个状态机标志，初态先把请求头send,中间态再次设置EPOLLOUT，把回应内容send,终态判断fd的发送区是否为空，为空再次send
    //     set= send(fd,connection[fd].wbuffer,connection[fd].wlength,0);//send之后不一定是recv，我再send
    //     if(set<0){
    //         perror("send");
    //         cancel_event(fd);
    //         close(fd);
    //         return -1;
    //     }else if(set == 0){
    //         printf("fd: %d is diconnected\n",fd);
    //         memset(&connection[fd],0,sizeof(struct conn));
    //         cancel_event(fd);
    //         close(fd);
    //         return 0;
    //     }else{
    //         INFO("server send to clientd:( %s)   %d(bytes)\n",connection[fd].wbuffer,set);
    //         connection[fd].wlength -= set;
    //         set_event(fd,EPOLLIN);
    //     }
    // }else if(connection[fd].status==2){//三个状态具体怎么发送，业务层去管怎么发送
    //     set=send(fd,connection[fd].wbuffer,connection[fd].wlength,0);
    //     set_event(fd,EPOLLIN);

    // }else if(connection[fd].status==0){
    //     if (connection[fd].wlength != 0) {
    //             set= send(fd,connection[fd].wbuffer,connection[fd].wlength,0);
    //             connection[fd].wlength=0;
    //             INFO("server send to clientd:(%s)   %d(bytes)\n",connection[fd].wbuffer,set);
    //         }
    //     set_event(fd,EPOLLIN);
    // }
}
typedef struct reactor_arg{
    int fd;
}Arg;
int reactor_entry(unsigned int port){
    struct epoll_event events[EVENT_SIZE]={0};
    epfd=epoll_create(1);
    for(int i=0;i<MAX_PORT;i++){//代码运行，直接调用init_server(),然后把返回的fd调用注册函数，将fd加入事件表
        int listenfd=init_server(port+i);
        if(event_register(listenfd,EPOLLIN,LISTENFD)){
            printf("event_register listenfd failed\n");
            return -1;
        }
        printf("port %d is used\n",port+i);
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
            if(connection[fd].is_listenfd){//判断是listenfd还是clientfd
                Arg arg={
                    .fd=fd
                };
                connection[fd].r_action.accept_callback(fd,(void*)&arg);//Listenfd就调用对应的回调函数
            }else{
                if(events[i].events&EPOLLIN){
                    Arg arg={
                        .fd=fd
                    };
                    connection[fd].r_action.recv_callback(fd,(void*)&arg);//同理
                }
                if(events[i].events&EPOLLOUT){
                    Arg arg={
                        .fd=fd
                    };
                    connection[fd].send_callback(fd,(void*)&arg);
                }
            }
        }
    }
}