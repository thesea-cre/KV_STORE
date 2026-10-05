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
#include"sync.h"
#define INFO printf
int epfd;//定义一个全局的epoll
int accept_cb(int fd,void *arg);
int recv_cb(int fd,void *arg);
int send_cb(int fd,void *arg);
static struct timeval begin;
extern int s_or_c;
int recv_server_cb(int fd,void *arg);
int send_server_cb(int fd,void*arg);
int accept_slave_cb(int fd,void *arg);
int recv_from_slave_cb(int fd, void *arg);
int send_to_slave_cb(int fd,void* arg);
void master_notify_slaves(MasterMgr *mgr);
void hex_dump(const void *data, size_t size, const char *title) {
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
struct conn connection[CONNECTION_SIZE]={0};//可以理解为一个fd事件表
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
    addr.sin_addr.s_addr=inet_addr("192.168.72.129");
    if(connect(socketfd,(struct sockaddr*)&addr,sizeof(struct sockaddr_in))<0){
        perror("connect");
        return -1;
    }
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
    
    memset(connection[fd].rbuffer,0,BUFFER_SIZE);//把收发缓冲区清空
    memset(connection[fd].wbuffer,0,BUFFER_SIZE);
    connection[fd].rlength=0;
    connection[fd].wlength=0;
    connection[fd].status=0;
    if(flag == LISTENFD){
        connection[fd].send_callback=send_cb;//赋值相应的回调函数
        connection[fd].r_action.accept_callback=accept_cb;
        connection[fd].is_listenfd=flag;
    }else if(flag == CLIENTFD){
        connection[fd].send_callback=send_cb;//赋值相应的回调函数
        connection[fd].r_action.recv_callback=recv_cb;
        connection[fd].is_listenfd=flag;
    }else if(flag==SERVER_MASTER){
        connection[fd].send_callback=send_server_cb;
        connection[fd].r_action.recv_callback=recv_server_cb;
        connection[fd].is_listenfd=flag;
    }else if(flag==SERVER_SLAVE){
        connection[fd].send_callback=send_to_slave_cb;
        connection[fd].r_action.recv_callback=recv_from_slave_cb;
        connection[fd].is_listenfd=flag;
    }else if(flag==LISTEN_SLAVE){
        connection[fd].send_callback=send_cb;//赋值相应的回调函数
        connection[fd].r_action.accept_callback=accept_slave_cb;
        connection[fd].is_listenfd=flag;
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
int accept_slave_cb(int fd,void *arg){
    struct sockaddr_in addr;
    socklen_t addrlen=sizeof(struct sockaddr_in);
    int slavefd =accept(fd,(struct sockaddr*)&addr,&addrlen);//直接accept
    if(slavefd<0){
        printf("accept errno:%d ---> %s\n",errno,strerror(errno));
        return -1;
    }
    if(event_register(slavefd,EPOLLIN,SERVER_SLAVE)){//将返回的fd，调用事件注册函数
        printf(" clientfd:%d of event_register is failed\n",slavefd);
        return -1;
    }
    return slavefd;
}
int recv_cb(int fd,void *arg){//流控，应用层反压，分包
    char  buf[BUFFER_SIZE]={0};
    int ret;
    ret=recv(fd,buf,BUFFER_SIZE,0);//先recv到暂时缓冲区
    printf("收包长度：%d",ret);
    hex_dump(buf,ret,"服务端收\n");
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
        if(connection[fd].rlength>0){
            if(connection[fd].rlength+ret>BUFFER_SIZE){
                cancel_event(fd);
                memset(&connection[fd],0,sizeof(struct conn));
                close(fd);
                return -1;
            }
            memcpy(connection[fd].rbuffer+connection[fd].rlength,buf,ret);//这里处理拆包的方式:追加包，先放到一个暂时的缓冲区
            connection[fd].rlength+=ret;
            INFO("recv from clientfd:clientfd--> %d,buffer-->%s\n",fd,connection[fd].rbuffer);
            #if ENABLEKV
            int handled=kv_request(connection[fd].rbuffer,connection[fd].wbuffer,connection[fd].rlength,&connection[fd].wlength);
            if(handled==-2){
                cancel_event(fd);
                memset(&connection[fd],0,sizeof(struct conn));
                close(fd);
                return -1;
            }else if(handled==-1){
                set_event(fd,EPOLLIN);
            }else if(handled==0){
                connection[fd].rlength=0;
                set_event(fd,EPOLLOUT);
            }else if(handled>0){
                connection[fd].rlength-=handled;
                if(connection[fd].rlength>0){
                    memmove(connection[fd].rbuffer,connection[fd].rbuffer+handled,connection[fd].rlength);
                }
                if(connection[fd].wlength>0){
                    set_event(fd,EPOLLOUT);
                }else{
                    set_event(fd,EPOLLIN);
                }
                
            }
            #endif
            return 0;
        }
       
        int handled=kv_request(buf,connection[fd].wbuffer,ret,&connection[fd].wlength);
        if(handled==-2){
            cancel_event(fd);
            memset(&connection[fd],0,sizeof(struct conn));
            close(fd);
            return -1;
        }else if(handled==-1){
            if(connection[fd].rlength+ret>BUFFER_SIZE){
                cancel_event(fd);
                memset(&connection[fd],0,sizeof(struct conn));
                close(fd);
                return -1;
            }
            memcpy(connection[fd].rbuffer+connection[fd].rlength,buf,ret);//这里处理拆包的方式:追加包，先放到一个暂时的缓冲区
            connection[fd].rlength+=ret;
            set_event(fd,EPOLLIN);
        }else if(handled==0){
            set_event(fd,EPOLLOUT);
        }else if(handled>0){
            ret-=handled;
            if(ret==0){
                set_event(fd,EPOLLOUT);
            }
            else if(ret>0){
                memcpy(connection[fd].rbuffer,buf+handled,ret);
                set_event(fd,EPOLLOUT);
            }
            set_event(fd,EPOLLOUT);
        }
        // if(connection[fd].wlength>0){
        //     // set_event(fd,EPOLLIN|EPOLLOUT);
        //     set_event(fd,EPOLLOUT);
        // }else{
        //     set_event(fd,EPOLLIN);
        // }
        // return 0;
    }
}
int send_cb(int fd,void *arg){
    
    #if TEST100w
    memcpy(connection[fd].wbuffer,connection[fd].rbuffer,connection[fd].rlength);
    connection[fd].wlength=connection[fd].rlength;
    #endif


    int set;//阻塞式的肯定能发完
    set= send(fd,connection[fd].wbuffer,connection[fd].wlength,0);
    hex_dump(connection[fd].wbuffer,set,"服务端发\n");
    // if(connection[fd].status==0){//先把状态机放一边
    //     set=send(fd,connection[fd].wbuffer,connection[fd].wlength,0);
    //     set_event(fd,EPOLLOUT);
    // }else if(connection[fd].status==1){
    //     set_event(fd,EPOLLOUT);
    // }else if(connection[fd].status==2){
    //     set_event(fd,EPOLLIN);
    // }
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
        // if(connection[fd].wlength > 0){//不要删，后续扩充
        //     // 没发完，残留前移，继续保留写事件
        //     memmove(connection[fd].wbuffer, 
        //         connection[fd].wbuffer + set, 
        //         connection[fd].wlength);
        //     set_event(fd, EPOLLIN | EPOLLOUT);
        // }
        // if(kv_request(connection[fd].rbuffer,connection[fd].wbuffer,&connection[fd].rlength,&connection[fd].wlength)<0){
        //     cancel_event(fd);
        //     memset(&connection[fd],0,sizeof(struct conn));
        //     close(fd);
        //     return -1;
        // }
        int handled=0;
        if(connection[fd].rlength>0){
            handled=kv_request(connection[fd].rbuffer,connection[fd].wbuffer,connection[fd].rlength,&connection[fd].wlength);
        }
        if(handled==-2){
            cancel_event(fd);
            memset(&connection[fd],0,sizeof(struct conn));
            close(fd);
            return -1;
        }else if(handled==-1){
            set_event(fd,EPOLLIN);
        }else if(handled==0){
            connection[fd].rlength=0;
            set_event(fd,EPOLLIN);
        }else if(handled>0){
            connection[fd].rlength-=handled;
            if(connection[fd].rlength>0){
                memmove(connection[fd].rbuffer,connection[fd].rbuffer+handled,connection[fd].rlength);
            }
            if(connection[fd].wlength>0){
                set_event(fd,EPOLLOUT);
            }else{
                set_event(fd,EPOLLIN);
            }
        }
        // if(connection[fd].wlength>0){
        //         set_event(fd,EPOLLIN|EPOLLOUT);//要同时监听可读事件和可写事件，不然如果数据没来完，就会卡死
        //     }else{
        //         set_event(fd,EPOLLIN);
        //     }
    }
}

int recv_from_slave_cb(int fd, void *arg) {//先做简单一些，因为只是发psync请求
    char buf[BUFFER_SIZE]={0};
    int ret;
    ret=recv(fd,buf,BUFFER_SIZE,0);
    hex_dump(buf,ret,"主服务器收\n");
    INFO("recv到栈的数据长度：%d(bytes)\n",ret);
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
        if(connection[fd].rlength+ret>BUFFER_SIZE){
            INFO("recv_from_slave_cb: 持久缓冲区溢出，关闭fd %d\n",fd);
            cancel_event(fd);
            memset(&connection[fd],0,sizeof(struct conn));
            close(fd);
            return -1;
        }
        memcpy(connection[fd].rbuffer+connection[fd].rlength,buf,ret);
        connection[fd].rlength+=ret;
        int handled=master_handle_sync(&mgr,fd,connection[fd].rbuffer,connection[fd].rlength);
        if(handled<=0){//WAIT：不足20字节留在缓冲区等下一段；OVER：暂不认识的包
            set_event(fd,EPOLLIN);
            return 0;
        }
        connection[fd].rlength-=handled;
        if(connection[fd].rlength>0){
            memmove(connection[fd].rbuffer,connection[fd].rbuffer+handled,connection[fd].rlength);
        }
        Slaveconn *sconn=slave_find_by_fd(&mgr,fd);
        if(sconn&&sconn->status!=0){
            set_event(fd,EPOLLOUT);
        }else{
            set_event(fd,EPOLLIN);
        }
        return 0;
    }
    return 0;
}
//要么主服务器传字节流，全量重传，要么是写命令，增量重传
int recv_server_cb(int fd,void *arg){
    char buf[BUFFER_SIZE]={0};
    int ret;
    ret=recv(fd,buf,BUFFER_SIZE,0);
    hex_dump(buf,ret,"从服务器收\n");
    INFO("recv到栈的数据长度：%d(bytes)\n",ret);
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
        //情景1，持久缓冲区有剩余数据
        if(connection[fd].rlength>0){
            INFO("持久缓冲区有数据\n");
            if(connection[fd].rlength+ret>BUFFER_SIZE){
                cancel_event(fd);
                memset(&connection[fd],0,sizeof(struct conn));
                close(fd);
                return -1;
            }
            memcpy(connection[fd].rbuffer+connection[fd].rlength,buf,ret);
            int processed=0;
            while(processed<connection[fd].rlength){
                INFO("进入slave_process函数处理数据\n");
                int offset=slave_process(&slave,connection[fd].rbuffer+processed,
                    connection[fd].rlength-processed);
                    INFO("slave_process这次处理了的字节数：%d(bytes)\n",offset);

                INFO("processed已经处理了的字节数:%d(bytes)\n",processed);
                if(offset<=0){//0=本段没处理完；-1=WAIT等数据；-2=ERROR，都停下等下一段
                    set_event(fd,EPOLLIN);
                    break;
                }
                processed+=offset;
            }
            connection[fd].rlength-=processed;
            INFO("这次recv到持久缓冲区处理的总字节数：%d(bytes)\n",processed);
            INFO("持久化缓冲区剩余的字节数：%d(bytes)\n",connection[fd].rlength);
            if(connection[fd].rlength>0){//缓冲区前移
                memmove(connection[fd].rbuffer,connection[fd].rbuffer+processed,
                    connection[fd].rlength);
            }

            return 0;
        }
        //情景2：持久缓冲区没有剩余数据，直接在栈上处理数据
        int buf_offset=0;
        while(buf_offset<ret){
            int offset=slave_process(&slave,buf+buf_offset,ret-buf_offset);
            if(offset<=0){//0=本段没处理完；-1=WAIT等数据；-2=ERROR，都停下等下一段
                set_event(fd,EPOLLIN);
                break;
            }
            buf_offset+=offset;
        }
        INFO("这次recv直接在栈上处理的总字节数：%d(bytes)\n",buf_offset);
        INFO("栈内剩余的字节数：%d(bytes)\n",ret-buf_offset);
        if(buf_offset<ret){
            memcpy(connection[fd].rbuffer+connection[fd].rlength,buf+buf_offset,ret-buf_offset);
            connection[fd].rlength+=ret-buf_offset;
        }

        return 0;
    }
}
//写路径传播钩子：status 1(全量)不需要唤醒——全量收尾SYNC_END时本就留着EPOLLOUT，
//online_sync会从FULLRESYNC时对齐的slave_offset续传；只唤醒处于发送态的2/3
void master_notify_slaves(MasterMgr *mgr) {
    if (!mgr) return;
    Slaveconn *cur = mgr->slave_head;
    while (cur) {
        if ((cur->status == 2 || cur->status == 3) && cur->fd >= 0) {
            set_event(cur->fd, EPOLLOUT);
        }
        cur = cur->next;
    }
}
//全量重传字节流,增量重传TLV，
int send_to_slave_cb(int fd,void* arg){
    int set;
    char buf[BUFFER_SIZE]={0};
    int buf_len=BUFFER_SIZE;
    Slaveconn *slave=slave_find_by_fd(&mgr,fd);
    if(!slave){//还没有PSYNC建立上下文，继续等请求，不能把EPOLLOUT留着空转
        set_event(fd,EPOLLIN);
        return 0;
    }
    int ret=0;
    if(slave->status==1){
        INFO("走全量重传\n");
        ret=master_full_sync(&mgr,slave,"aof_db",buf,buf_len);
    }else if(slave->status==2){
        INFO("走增量重传\n");
        ret=master_incremental_sync(&mgr,slave,buf,buf_len);
    }else if(slave->status==3){
        INFO("在线转发\n");
        ret=master_online_sync(&mgr,slave,buf,buf_len);
    }
    if(ret<0){//上层出错：标记离线并摘掉fd，避免fd复用后slave_find_by_fd张冠李戴
        INFO("master sync error\n");
        slave->status=4;
        slave->fd=-1;
        cancel_event(fd);
        memset(&connection[fd],0,sizeof(struct conn));
        close(fd);
        return -1;
    }
    if(ret==0){//无数据可发：在线追平回EPOLLIN等kv写路径唤醒；其余状态兜底EPOLLOUT
        if(slave->status==3){
            set_event(fd,EPOLLIN);
        }else{
            set_event(fd,EPOLLOUT);
        }
        return 0;
    }
    set=send(fd,buf,ret,0);
    hex_dump(buf,set,"主服务器发\n");
    // else{//先不删
    //     memcpy(connection[fd].wbuffer+connection[fd].wlength,buf,buf_len);
    //     connection[fd].wlength+=buf_len;
    //     set= send(fd,connection[fd].wbuffer,connection[fd].wlength,0);//send之后不一定是recv，我再send
    // }
    if(set<0){
        perror("send");
        slave->status=4;
        slave->fd=-1;
        cancel_event(fd);
        memset(&connection[fd],0,sizeof(struct conn));
        close(fd);
        return -1;
    }else{
        set_event(fd,EPOLLOUT);
    }
}
//包含发送心跳包，发送同步请求,目前还没有发送心跳包
int send_server_cb(int fd,void* arg){
    int set;
    char buf[BUFFER_SIZE]={0};
    int  buf_len=BUFFER_SIZE;
    int ret=slave_send_sync(&slave,buf,buf_len);
    INFO("slave send sync return ret:%d\n",ret);
    // if(connection[fd].wlength==0){//不经过缓冲区直接发
    //     set=send(fd,buf,buf_len,0);
    // }
    if(ret<=0){
        set_event(fd,EPOLLIN);
        return 0;
    }
    set=send(fd,buf,ret,0);
    hex_dump(buf,set,"从服务器发\n");
    
    
    // else{
    //     memcpy(connection[fd].wbuffer+connection[fd].wlength,buf,buf_len);
    //     connection[fd].wlength+=buf_len;
    //     set= send(fd,connection[fd].wbuffer,connection[fd].wlength,0);//send之后不一定是recv，我再send
    // }
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
        // if(connection[fd].wlength!=0){
        //     connection[fd].wlength-=set;
        // }
        // if(connection[fd].wlength>0){
        //     memmove(connectionp[fd].wbuffer,connection[fd].wbuffer+connection[fd].wlength,
        //         connection[fd].wlength);
        // }
        set_event(fd,EPOLLIN);
    }
}
typedef struct reactor_arg{
    int fd;
}Arg;
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
        master_init(&mgr);
    }
    else if(s_or_c==1){//创建从服务器，创建和主服务器通信的fd
        int master_fd=connect_server();//连接主服务器
        if(master_fd<0){
            printf("connect master failed\n");
            return -1;
        }
        if(event_register(master_fd,EPOLLOUT,SERVER_MASTER)){
            printf("event_register\n");
            return -1;
        }
        slave_init(&slave);
        slave.master_fd=master_fd;
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
                Arg arg={
                    .fd=fd
                };
                connection[fd].r_action.accept_callback(fd,(void*)&arg);//Listenfd就调用对应的回调函数
            }
            else if(connection[fd].is_listenfd==LISTEN_SLAVE){
                Arg arg={
                    .fd=fd
                };
                connection[fd].r_action.accept_callback(fd,(void*)&arg);//Listenfd就调用对应的回调函数
            }
            else{
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