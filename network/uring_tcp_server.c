#include<sys/socket.h>
#include<arpa/inet.h>
#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<errno.h>
#include<liburing.h>
#include<unistd.h>
#include<sys/time.h>
// #include"server.h"
#include"kvstore.h"
#include"tool.h"
#if NETWORK_SELECT == ENABLE_PROACTOR//整个文件只在选中io_uring时编译，避免与reactor.c的同名函数撞链接

#define EVENT_RECV 0
#define EVENT_SEND 1
#define EVENT_ACCEPT 2
// #define EVENT_SLAVE_ACCEPT 3
// #define EVENT_SERVER_RECV 4
// #define EVENT_SERVER_SEND 5
// #define EVENT_SLAVE_RECV 6
// #define EVENT_SLAVE_SEND 7
#define EVENT_CANCEL 8

#define ENTRIES_LENGTH 1024
// #define SLAVE_PORT 8000
// int uring_recv_cb(int fd,void *arg);
// int uring_send_cb(int fd,void *arg);
// int uring_accept_cb(int fd,void *arg);
static struct timeval begin;
struct io_uring ring;
// extern int s_or_c;
// extern MasterMgr mgr;
// extern Slave slave;
struct conn_info{//这个结构体只是为了io完成操作后标识fd和io类型的
    int fd;
    int event;
};
struct conn{
    int fd;
    char wbuffer[BUFFER_SIZE];
    char rbuffer[BUFFER_SIZE];
    int wlength;
    int rlength;
    int send_pending;
};
static struct conn connection[1024]={0};
int uring_init_server(char *ip,unsigned int port){
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
    return socketfd;
}
//提交send队列项
int set_event_send(struct io_uring*ring,int fd,void*buf,size_t len,int flag,int select){
     struct io_uring_sqe* sqe=io_uring_get_sqe(ring);//从ring中拿一个sqe，
     if (!sqe) {
        fprintf(stderr, "ERROR: io_uring_get_sqe returned NULL in set_event_send!\n");
        return -1; // 阻止后面的崩溃
    }
     struct conn_info conn={
        .fd=fd,
        .event=select,//一种身份
    };
    io_uring_prep_send(sqe,fd,buf,len,flag);
    memcpy(&sqe->user_data,&conn,sizeof(conn));
}
//提交recv队列项
int set_event_recv(struct io_uring*ring,int fd,void*buf,size_t len,int flag,int select){
    struct io_uring_sqe* sqe=io_uring_get_sqe(ring);
    if (!sqe) {
        fprintf(stderr, "ERROR: io_uring_get_sqe returned NULL in set_event_recv!\n");
    }
    struct conn_info conn={//ring只是维护这个请求队列，是什么样的请求要自己写个结构体去标
        .fd=fd,
        .event=select,
    };
    io_uring_prep_recv(sqe,fd,buf,len,flag);
    memcpy(&sqe->user_data,&conn,sizeof(conn));
}
//提交accept队列项
int set_event_accept(struct io_uring*ring,int fd,struct sockaddr* addr,socklen_t *addrlen,int flag,int select){
    struct io_uring_sqe* sqe=io_uring_get_sqe(ring);
    if (!sqe) {
        fprintf(stderr, "ERROR: io_uring_get_sqe returned NULL in set_event_accept!\n");
        return -1; // 阻止后面的崩溃
    }
    INFO("io_uing_get_sqe\n");
    struct conn_info conn={//ring只是维护这个请求队列，是什么样的请求要自己写个结构体去标
        .fd=fd,
        .event=select,
    };
    io_uring_prep_accept(sqe,fd,(struct sockaddr*)addr,addrlen,flag);//
    INFO("io_uring_prep_accept\n");
    memcpy(&sqe->user_data,&conn,sizeof(conn));//sqe有个void*的user_data来存
    INFO("memcpy\n");
}
int set_event_cancel(struct io_uring *ring,int fd,int select){
    struct io_uring_sqe* sqe=io_uring_get_sqe(ring);
    if(!sqe){
        fprintf(stderr, "ERROR: io_uring_get_sqe returned NULL in set_event_recv!\n");
    }
    struct conn_info conn={//ring只是维护这个请求队列，是什么样的请求要自己写个结构体去标
        .fd=fd,
        .event=select,
    };
    io_uring_prep_cancel(sqe,fd,IORING_ASYNC_CANCEL_ALL);
    memcpy(&sqe->user_data,&conn,sizeof(conn));//sqe有个void*的user_data来存
}
//用于唤醒函数
// int set_event_nop(struct io_uring* ring,int fd,int select){//只用于唤醒：nop立即完成，触发对应send回调去读真实数据
//     struct io_uring_sqe* sqe=io_uring_get_sqe(ring);
//     if (!sqe) {
//         fprintf(stderr, "ERROR: io_uring_get_sqe returned NULL in set_event_accept!\n");
//         return -1; // 阻止后面的崩溃
//     }
//     struct conn_info conn={
//         .fd=fd,
//         .event=select,
//     };
//     io_uring_prep_nop(sqe);
//     memcpy(&sqe->user_data,&conn,sizeof(conn));
// }
int cancel_cb(struct io_uring_cqe *entries,int fd){
    int ret=entries->res;
    return 0;
}
int recv_cb(struct io_uring_cqe *entries,int fd){
    int ret=entries->res;//接受到的数据
    if (entries->res == -ECANCELED) return 0;
    if(ret>0){
        #if ENABLEKV
        int cons_=0;
        connection[fd].rlength+=ret;
        KV_server(connection[fd].rbuffer,connection[fd].rlength,connection[fd].wbuffer,
        BUFFER_SIZE,&cons_,&connection[fd].wlength);
        if(cons_<connection[fd].rlength){
            memmove(connection[fd].rbuffer,connection[fd].rbuffer+cons_,connection[fd].rlength-cons_);
        }
        connection[fd].rlength-=cons_;
        if(connection[fd].wlength>0){
            set_event_send(&ring,fd,connection[fd].wbuffer,connection[fd].wlength,0,EVENT_SEND);
        }else{
            set_event_recv(&ring,fd,connection[fd].rbuffer+connection[fd].rlength,
                BUFFER_SIZE-connection[fd].rlength,0,EVENT_RECV);
        }
        
        return 0;
        #endif
    }else if(ret<=0){
        printf("fd ->%d is disconnected\n",fd);
        printf("rlength=%d\n",connection[fd].rlength);
        set_event_cancel(&ring,fd,EVENT_CANCEL);
        close(fd);
        memset(&connection[fd],0,sizeof(struct conn));
        return 0;
    }
}
int send_cb(struct io_uring_cqe*entries,int fd){
    int set=entries->res;
    if (entries->res == -ECANCELED) return 0;
    if(set>0){
        INFO("server send to clientd:( %s)   %d(bytes)\n",connection[fd].wbuffer,set);
        connection[fd].wlength-=set;
        INFO("wbuffer中还有多少数据：%d\n",connection[fd].wlength);
        set_event_recv(&ring,fd,connection[fd].rbuffer+connection[fd].rlength,BUFFER_SIZE-connection[fd].rlength,0,EVENT_RECV);
        return 0;
    }else{
        printf("fd ->%d is disconnected\n",fd);
        printf("rlength=%d\n",connection[fd].rlength);
        set_event_cancel(&ring,fd,EVENT_CANCEL);
        close(fd);
        memset(&connection[fd],0,sizeof(struct conn)); 
        return 0;
    }
}
int accept_cb(struct io_uring_cqe*entries,int fd){
    struct sockaddr_in addr;
    memset(&addr,0,sizeof(struct sockaddr_in));
    socklen_t len=sizeof(struct sockaddr_in);
    set_event_accept(&ring,fd,(struct sockaddr*)&addr,&len,0,EVENT_ACCEPT);
    int connfd=entries->res;
    //  if(connfd%10000==0){
    //     struct timeval current;
    //     gettimeofday(&current,NULL);
    //     int time=TIME_SUB_MS(current,begin);
    //     printf("Hello server:  clientfd-->%d,timeused:%d\n",connfd,time);
    //     memcpy(&begin,&current,sizeof(struct timeval));
    // }
    if (connfd < 0) {
        // 打印具体的错误原因
        printf("Accept failed on listen_fd %d! Error: %d (%s)\n", 
               fd, connfd, strerror(-connfd));
        return 0; // 千万不要用负数的 fd 去操作数组
    }
    memset(&connection[connfd], 0, sizeof(struct conn));
    set_event_recv(&ring,connfd,connection[connfd].rbuffer,BUFFER_SIZE,0,EVENT_RECV);
}
// int accept_slave_cb(struct io_uring_cqe*entries,int fd){
//     struct sockaddr_in addr;
//     memset(&addr,0,sizeof(struct sockaddr_in));
//     socklen_t len=sizeof(struct sockaddr_in);
//     set_event_accept(&ring,fd,(struct sockaddr*)&addr,&len,0,EVENT_SLAVE_ACCEPT);
//     int connfd=entries->res;
//     set_event_recv(&ring,connfd,connection[connfd].buf,BUFFER_SIZE,0,EVENT_SERVER_RECV);
// }
// int recv_from_slave_cb(struct io_uring_cqe*entries,int fd) {//先做简单一些，因为只是发psync请求
//     int ret=entries->res;
//     if(ret<0){
//         perror("recv");
//         slave_remove_by_fd(&mgr, fd);
//         memset(&connection[fd],0,sizeof(struct conn));
//         close(fd);
//         return -1;
//     }else if(ret == 0){
//         INFO("fd: %d is diconnected\n",fd);
//         slave_remove_by_fd(&mgr, fd);
//         memset(&connection[fd],0,sizeof(struct conn));
//         close(fd);
//         return 0;
//     }else{
//         hex_dump(connection[fd].buf,ret,"主服务器收\n");
//         if(connection[fd].rlength+ret>BUFFER_SIZE){
//             INFO("recv_from_slave_cb: 持久缓冲区溢出，关闭fd %d\n",fd);
//             memset(&connection[fd],0,sizeof(struct conn));
//             close(fd);
//             return -1;
//         }
//         memcpy(connection[fd].rbuffer+connection[fd].rlength,connection[fd].buf,ret);
//         connection[fd].rlength+=ret;
//         int handled=master_handle_sync(&mgr,fd,connection[fd].rbuffer,connection[fd].rlength);
//         if(handled==-2){
//             Slaveconn *slave=slave_find_by_fd(&mgr,fd);
//             slave_remove_by_fd(&mgr, fd);
//             close(fd);
//             return 0;
//         }else if(handled==-1){
//             set_event_recv(&ring,fd,connection[fd].buf,BUFFER_SIZE,0,EVENT_SERVER_RECV);
//             return 0;
//         }
//         connection[fd].rlength-=handled;
//         if(connection[fd].rlength>0){
//             memmove(connection[fd].rbuffer,connection[fd].rbuffer+handled,connection[fd].rlength);
//         }
//         Slaveconn *sconn=slave_find_by_fd(&mgr,fd);
//         if(sconn&&sconn->status!=0){
//             // set_event_send(&ring,fd,connection[fd].wbuffer,connection[fd].wlength,0,EVENT_SERVER_SEND);
//             set_event_nop(&ring,fd,EVENT_SERVER_SEND);

//         }else{
//             set_event_recv(&ring,fd,connection[fd].buf,BUFFER_SIZE,0,EVENT_SERVER_RECV);
//         }
//         return 0;
//     }
//     return 0;
// }

// int connect_server(void){
//     int socketfd=socket(AF_INET,SOCK_STREAM,0);
//     if(socketfd<0){
//         perror("sockte");
//         return -1;
//     }
//     struct sockaddr_in addr;
//     memset(&addr,0,sizeof(addr));
//     addr.sin_family=AF_INET;
//     addr.sin_port=htons(SLAVE_PORT);
//     addr.sin_addr.s_addr=inet_addr("192.168.72.129");
//     if(connect(socketfd,(struct sockaddr*)&addr,sizeof(struct sockaddr_in))<0){
//         perror("connect");
//         return -1;
//     }
//     return socketfd;
// }
// int recv_server_cb(struct io_uring_cqe*entries,int fd){
//     int ret=entries->res;
//     char *buf=connection[fd].buf;
//     hex_dump(buf,ret,"从服务器收\n");
//     if(ret<0){
//         perror("recv");
//         memset(&connection[fd],0,sizeof(struct conn));
//         close(fd);
//         return -1;
//     }else if(ret == 0){
//         INFO("fd: %d is diconnected\n",fd);
//         memset(&connection[fd],0,sizeof(struct conn));
//         close(fd);
//         return 0;
//     }else{
//         //情景1，持久缓冲区有剩余数据
//         if(connection[fd].rlength>0){
//             INFO("持久缓冲区有数据\n");
//             if(connection[fd].rlength+ret>BUFFER_SIZE){
//                 memset(&connection[fd],0,sizeof(struct conn));
//                 close(fd);
//                 return -1;
//             }
//             memcpy(connection[fd].rbuffer+connection[fd].rlength,buf,ret);
//             int processed=0;
//             while(processed<connection[fd].rlength){
//                 INFO("进入slave_process函数处理数据\n");
//                 int offset=slave_process(&slave,connection[fd].rbuffer+processed,
//                     connection[fd].rlength-processed);
//                     INFO("slave_process这次处理了的字节数：%d(bytes)\n",offset);

//                 INFO("processed已经处理了的字节数:%d(bytes)\n",processed);
//                 if(offset==-1||offset==0){//0=本段没处理完；-1=WAIT等数据；-2=ERROR，都停下等下一段
//                     // set_event_recv(&ring,fd,connection[fd].buf,BUFFER_SIZE,0,EVENT_SLAVE_RECV);
//                     break;
//                 }else if(offset==-2){
//                     close (fd);
//                     return 0;
//                 }
//                 processed+=offset;
//             }
//             connection[fd].rlength-=processed;
//             INFO("这次recv到持久缓冲区处理的总字节数：%d(bytes)\n",processed);
//             INFO("持久化缓冲区剩余的字节数：%d(bytes)\n",connection[fd].rlength);
//             if(connection[fd].rlength>0){//缓冲区前移
//                 memmove(connection[fd].rbuffer,connection[fd].rbuffer+processed,
//                     connection[fd].rlength);
//             }
//             set_event_recv(&ring,fd,connection[fd].buf,BUFFER_SIZE,0,EVENT_SLAVE_RECV);
//             return 0;
//         }
//         //情景2：持久缓冲区没有剩余数据，直接在栈上处理数据
//         int buf_offset=0;
//         while(buf_offset<ret){
//             int offset=slave_process(&slave,buf+buf_offset,ret-buf_offset);
//             if(offset<=0){//0=本段没处理完；-1=WAIT等数据；-2=ERROR，都停下等下一段
//                 set_event_recv(&ring,fd,connection[fd].buf,BUFFER_SIZE,0,EVENT_SLAVE_RECV);
//                 break;
//             }
//             buf_offset+=offset;
//         }
//         INFO("这次recv直接在栈上处理的总字节数：%d(bytes)\n",buf_offset);
//         INFO("栈内剩余的字节数：%d(bytes)\n",ret-buf_offset);
//         if(buf_offset<ret){
//             memcpy(connection[fd].rbuffer+connection[fd].rlength,buf+buf_offset,ret-buf_offset);
//             connection[fd].rlength+=ret-buf_offset;
//         }
//         set_event_recv(&ring,fd,connection[fd].buf,BUFFER_SIZE,0,EVENT_SLAVE_RECV);
//         return 0;
//     }
// }
// //写路径传播钩子：给发送态(2/3)的从节点fd提交一个nop SQE，立即完成并触发send_to_slave_cb去读真实数据
// void master_notify_slaves(MasterMgr *mgr) {
//     if (!mgr) return;
//     Slaveconn *cur = mgr->slave_head ? mgr->slave_head->next : NULL;//虚拟头节点之后才是真实节点
//     while (cur) {
//         if ((cur->status == 2 || cur->status == 3) && cur->fd >= 0
//             && connection[cur->fd].send_pending == 0) {//有send在途的fd不能塞nop，否则send_to_slave_cb会覆写还在被内核引用的buf
//             set_event_nop(&ring, cur->fd, EVENT_SERVER_SEND);
//         }
//         cur = cur->next;
//     }
// }
// //全量重传字节流,增量重传TLV，
// int send_to_slave_cb(struct io_uring_cqe*entries,int fd){
//     int set=entries->res;
//     if(set<0){
//         perror("send");
//         slave_remove_by_fd(&mgr,fd);
//         memset(&connection[fd],0,sizeof(struct conn));
//         close(fd);
//         return -1;
//     }
//     connection[fd].wlength-=set;
//     connection[fd].send_pending=0;//上一个send SQE已完成，buf可以复用了
//     char *buf=connection[fd].buf;
//     int buf_len=BUFFER_SIZE;
//     Slaveconn *slave=slave_find_by_fd(&mgr,fd);
//     if(!slave){
//         set_event_recv(&ring,fd,connection[fd].buf,BUFFER_SIZE,0,EVENT_SERVER_RECV);
//         return 0;
//     }
//     int ret=0;
//     if(slave->status==1){
//         INFO("走全量重传\n");
//         ret=master_full_sync(&mgr,slave,"aof_db",buf,buf_len);
//     }else if(slave->status==2){
//         INFO("走增量重传\n");
//         ret=master_incremental_sync(&mgr,slave,buf,buf_len);
//     }else if(slave->status==3){
//         INFO("在线转发\n");
//         ret=master_online_sync(&mgr,slave,buf,buf_len);
//     }
//     if(ret<0){//上层出错
//         INFO("master sync error\n");
//         slave_remove_by_fd(&mgr, fd);
//         memset(&connection[fd],0,sizeof(struct conn));
//         close(fd);
//         return -1;
//     }
//     if(ret==0){
//         if(slave->status==3){
//             set_event_recv(&ring,fd,connection[fd].buf,BUFFER_SIZE,0,EVENT_SERVER_RECV);
//         }else if(slave->status==4){//online_sync淘汰或上层出错：摘链并断开
//             slave_remove_by_fd(&mgr, fd);
//             memset(&connection[fd],0,sizeof(struct conn));
//             close(fd);
//         }else{
//             //兜底：状态机出现意外值，挂起等唤醒，不能什么都不武装（one-shot下不arm就是死连接）
//             set_event_recv(&ring,fd,connection[fd].buf,BUFFER_SIZE,0,EVENT_SERVER_RECV);
//         }
//         return 0;
//     }
//     connection[fd].send_pending=1;//arm send后buf被内核引用，完成前不能覆写、不能重复arm
//     set_event_send(&ring,fd,buf,ret,0,EVENT_SERVER_SEND);
//     hex_dump(buf,ret,"主服务器发\n");
// }
// //包含发送心跳包，发送同步请求,目前还没有发送心跳包
// int send_server_cb(struct io_uring_cqe*entries,int fd){
//     int set=entries->res;//上一轮的
//     if(set<0){
//         perror("send");
//         close(fd);
//         return -1;
//     }
//     connection[fd].wlength-=set;
//     //PSYNC已发出：从节点此后只收（心跳未实现，没有要主动发的），arm recv挂起
//     set_event_recv(&ring,fd,connection[fd].buf,BUFFER_SIZE,0,EVENT_SLAVE_RECV);
//     return 0;
// }
int uring_start(char *ip,unsigned int port){
    // memset(&connection,0,sizeof(connection));
    struct io_uring_params params;//设置ring的参数结构体
    memset(&params,0,sizeof(struct io_uring_params));
    // params.flags = IORING_SETUP_SQPOLL;
    // params.sq_thread_idle = 2000;
    int ret= io_uring_queue_init_params(ENTRIES_LENGTH,&ring,&params);//params是这个函数的配置入参，内核把输出信息写回这个结构体
    if (ret < 0) {
        errno = -ret; // liburing 返回负数，需转为 errno
        perror("io_uring_queue_init_params failed");
        return -1;
    }
    printf("io_uring init success. Kernel sq_entries: %u, cq_entries: %u\n", 
       *ring.sq.kring_entries, *ring.cq.kring_entries);
    struct sockaddr_in addr;
    socklen_t addrlen=sizeof(addr);
    int i=0;
    while(i<MAX_PORT){
        int socketfd=uring_init_server(ip,port+i);//创建一个套接字fd
        if(socketfd<0){
            INFO("uring_init_server failed\n");
            return -1;
        }
        INFO("port %d is used\n",port+i);
        set_event_accept(&ring,socketfd,(struct sockaddr*)&addr,&addrlen,0,EVENT_ACCEPT);
        i++;
    }
    // if(s_or_c==0){//创建主服务器
    //     int listenfd=uring_init_server(ip,SLAVE_PORT);
    //     if(listenfd<0){
    //          INFO("uring_init_server failed\n");
    //         return -1;
    //     }
    //     set_event_accept(&ring,listenfd,(struct sockaddr*)&addr,&addrlen,0,EVENT_SLAVE_ACCEPT);
    //     INFO("从服务器接入端口：%d\n",SLAVE_PORT);
    // }
    // else if(s_or_c==1){//创建从服务器，创建和主服务器通信的fd
    //     int master_fd=connect_server();//连接主服务器
    //     if(master_fd<0){
    //         INFO("connect master failed\n");
    //         return -1;
    //     }
    //     slave.master_fd=master_fd;
    //     //io_uring是one-shot：connect后必须主动提交第一个SQE，发送完成会触发send_server_cb
    //     int n=slave_send_sync(&slave,connection[master_fd].buf,BUFFER_SIZE);
    //     if(n>0){
    //         set_event_send(&ring,master_fd,connection[master_fd].buf,n,0,EVENT_SLAVE_SEND);
    //     }
    // }
    gettimeofday(&begin,NULL);
    io_uring_submit(&ring);
    while(1){
        struct io_uring_cqe* cqes[128];//128个io事件
        int nready=io_uring_peek_batch_cqe(&ring,cqes,128);//也是peek128个iO事件
        if (nready == 0) {
            io_uring_submit_and_wait(&ring, 1); 
            continue; 
        }
        for(int i=0;i<nready;i++){
            struct io_uring_cqe* entries=cqes[i];
            struct conn_info result;
            memcpy(&result,&entries->user_data,sizeof(result));
            if(result.event==EVENT_RECV){
                recv_cb(entries,result.fd);
            }else if(result.event==EVENT_ACCEPT){
                accept_cb(entries,result.fd);
            }else if(result.event==EVENT_SEND){
                send_cb(entries,result.fd);
            }else if(result.event==EVENT_CANCEL){
                cancel_cb(entries,result.fd);
            }
            // else if(result.event==EVENT_SERVER_RECV){
            //     recv_from_slave_cb(entries,result.fd);
            // }else if(result.event==EVENT_SERVER_SEND){
            //     send_to_slave_cb(entries,result.fd);
            // }else if(result.event==EVENT_SLAVE_RECV){
            //     recv_server_cb(entries,result.fd);
            // }else if(result.event==EVENT_SLAVE_SEND){
            //     send_server_cb(entries,result.fd);
            // }else if(result.event==EVENT_SLAVE_ACCEPT){
            //     accept_slave_cb(entries,result.fd);
            // }
        }
        io_uring_cq_advance(&ring, nready);
        io_uring_submit(&ring);
    }
    return 0;
}
#endif//NETWORK_SELECT == ENABLE_PROACTOR