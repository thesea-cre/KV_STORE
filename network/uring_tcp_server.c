#include<sys/socket.h>
#include<arpa/inet.h>
#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<errno.h>
#include<liburing.h>
#include<unistd.h>
#include<sys/time.h>
#include"kvstore.h"
#include"log.h"
#include"tool.h"
#include"upload.h"
#include"resp.h"
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
int recv_from_slave_cb(struct io_uring_cqe*entries,int fd);
int recv_from_server_cb(struct io_uring_cqe*entries,int fd);
void notify(int fd);
int accept_slave_cb(struct io_uring_cqe*entries,int fd);
/*               */
#endif


#if NETWORK_SELECT == ENABLE_PROACTOR//整个文件只在选中io_uring时编译，避免与reactor.c的同名函数撞链接

#define EVENT_RECV 0
#define EVENT_SEND 1
#define EVENT_ACCEPT 2
#define EVENT_SLAVE_ACCEPT 3
#define EVENT_SERVER_RECV 4
// #define EVENT_SERVER_SEND 5
#define EVENT_SLAVE_RECV 6
// #define EVENT_SLAVE_SEND 7
#define EVENT_CANCEL 8

#define ENTRIES_LENGTH 1024
static struct timeval begin;
struct io_uring ring;
extern int s_or_c;


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
        if(KV_server(fd,connection[fd].rbuffer,connection[fd].rlength,connection[fd].wbuffer,
        BUFFER_SIZE,&cons_,&connection[fd].wlength)<0){
            kv_upload_abort(fd);
            set_event_cancel(&ring,fd,EVENT_CANCEL);
            close(fd);
            memset(&connection[fd],0,sizeof(struct conn));
            return 0;
        }
        if(cons_<connection[fd].rlength){
            memmove(connection[fd].rbuffer,connection[fd].rbuffer+cons_,connection[fd].rlength-cons_);
        }
        connection[fd].rlength-=cons_;
        if(connection[fd].wlength==0&&kv_resp_pending(fd)){
            int n=kv_resp_next(fd,(uint8_t*)connection[fd].wbuffer,BUFFER_SIZE);
            if(n>0) connection[fd].wlength=n;
        }
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
        kv_upload_abort(fd);
        kv_resp_abort(fd);
        close(fd);
        memset(&connection[fd],0,sizeof(struct conn));
        return 0;
    }
}
int send_cb(struct io_uring_cqe*entries,int fd){
    int set=entries->res;
    if (entries->res == -ECANCELED) return 0;
    if(set<=0){
        printf("fd ->%d is disconnected\n",fd);
        printf("rlength=%d\n",connection[fd].rlength);
        set_event_cancel(&ring,fd,EVENT_CANCEL);
        kv_upload_abort(fd);
        kv_resp_abort(fd);
        close(fd);
        memset(&connection[fd],0,sizeof(struct conn)); 
        return 0;
    }

    connection[fd].wlength-=set;
    if(connection[fd].wlength>0){                 /* 本段没发完：挪走已发部分继续 */
        memmove(connection[fd].wbuffer,connection[fd].wbuffer+set,connection[fd].wlength);
        set_event_send(&ring,fd,connection[fd].wbuffer,connection[fd].wlength,0,EVENT_SEND);
        return 0;
    }

    if(kv_resp_pending(fd)){
        int n=kv_resp_next(fd,(uint8_t*)connection[fd].wbuffer,BUFFER_SIZE);
        if(n>0){
            connection[fd].wlength=n;
            set_event_send(&ring,fd,connection[fd].wbuffer,n,0,EVENT_SEND);
            return 0;
        }
    }

    /* 队列空：处理 rbuffer 里残留的请求 */
    if(connection[fd].rlength>0){
        int cons_=0;
        if(KV_server(fd,connection[fd].rbuffer,connection[fd].rlength,
                     connection[fd].wbuffer,BUFFER_SIZE,&cons_,&connection[fd].wlength)<0){
            set_event_cancel(&ring,fd,EVENT_CANCEL);
            kv_upload_abort(fd);
            kv_resp_abort(fd);
            close(fd);
            memset(&connection[fd],0,sizeof(struct conn));
            return 0;
        }
        if(cons_<connection[fd].rlength){
            memmove(connection[fd].rbuffer,connection[fd].rbuffer+cons_,
                    connection[fd].rlength-cons_);
        }
        connection[fd].rlength-=cons_;
        if(connection[fd].wlength>0){
            set_event_send(&ring,fd,connection[fd].wbuffer,connection[fd].wlength,0,EVENT_SEND);
            return 0;
        }
    }

    set_event_recv(&ring,fd,connection[fd].rbuffer+connection[fd].rlength,
                   BUFFER_SIZE-connection[fd].rlength,0,EVENT_RECV);
    return 0;
}
int accept_cb(struct io_uring_cqe*entries,int fd){
    struct sockaddr_in addr;
    memset(&addr,0,sizeof(struct sockaddr_in));
    socklen_t len=sizeof(struct sockaddr_in);
    //set_event_accept(&ring,fd,(struct sockaddr*)&addr,&len,0,EVENT_ACCEPT);
    set_event_accept(&ring,fd,NULL,NULL,0,EVENT_ACCEPT);

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
#if SYNC
void notify(int fd){
    set_event_send(&ring,fd,connection[fd].wbuffer,connection[fd].wlength,0,EVENT_SEND);
    return;
}
int accept_slave_cb(struct io_uring_cqe*entries,int fd){
    struct sockaddr_in addr;
    memset(&addr,0,sizeof(struct sockaddr_in));
    socklen_t len=sizeof(struct sockaddr_in);
    set_event_accept(&ring,fd,(struct sockaddr*)&addr,&len,0,EVENT_SLAVE_ACCEPT);

    int connfd=entries->res;

    force_flush(ctx,huge_buf,&huge_buf_len,&huge_num);
    aof_rewrite(ctx);
    off_t offset=0;
    sendfile(connfd,ctx->db_fd,&offset,ctx->size);//同步阻塞发送

    set_event_recv(&ring,connfd,connection[connfd].rbuffer,BUFFER_SIZE,0,EVENT_SLAVE_RECV);

    Slaveconn *slave=(Slaveconn*)kv_malloc(sizeof(Slaveconn));
    memset(slave,0,sizeof(Slaveconn));
    slave->fd=connfd;
    if(mgr->slave_head->next){
        slave->next=mgr->slave_head->next;
    }
    mgr->slave_head->next=slave;
    mgr->slave_count++;

    slave->rbuffer=connection[connfd].rbuffer;
    slave->wbuffer=connection[connfd].wbuffer;
    slave->rlength=&connection[connfd].rlength;
    slave->wlength=&connection[connfd].wlength;
    slave->buf_length=BUFFER_SIZE;

    
    return connfd;
    
}
int recv_from_server_cb(struct io_uring_cqe*entries,int fd){
    int ret=entries->res;//接受到的数据
    if (entries->res == -ECANCELED) return 0;
    if(ret>0){
        #if ENABLEKV
        int cons_=0;
        int status=0;
        connection[fd].rlength+=ret;
        KV_sync(connection[fd].rbuffer,connection[fd].rlength,NULL,0,&cons_,&status);
        if(status==1){
            memmove(connection[fd].rbuffer,connection[fd].rbuffer+cons_,
                connection[fd].rlength-cons_);
        }
        connection[fd].rlength-=cons_;
        set_event_recv(&ring,fd,connection[fd].rbuffer+connection[fd].rlength,
                BUFFER_SIZE-connection[fd].rlength,0,EVENT_SERVER_RECV);
        return 0;
        #endif
    }else if(ret<=0){
        printf("fd ->%d is disconnected\n",fd);
        printf("rlength=%d\n",connection[fd].rlength);
        set_event_cancel(&ring,fd,EVENT_CANCEL);
        kv_upload_abort(fd);
        kv_resp_abort(fd);
        close(fd);
        
        memset(&connection[fd],0,sizeof(struct conn));
        return 0;
    }
}
int recv_from_slave_cb(struct io_uring_cqe *entries,int fd){
    int ret=entries->res;//接受到的数据
    if (entries->res == -ECANCELED) return 0;
    if(ret>0){
        #if ENABLEKV
        INFO("recv from slave %d (bytes)\n",ret);
        return 0;
        #endif
    }else if(ret<=0){
        printf("fd ->%d is disconnected\n",fd);
        printf("rlength=%d\n",connection[fd].rlength);
        set_event_cancel(&ring,fd,EVENT_CANCEL);
        slave_close(mgr,fd);
        kv_upload_abort(fd);
        kv_resp_abort(fd);
        close(fd);
        memset(&connection[fd],0,sizeof(struct conn));
        return 0;
    }
}
#endif
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
    #if SYNC
    if(s_or_c==0){//创建主服务器
        int listenfd=uring_init_server(ip,SLAVE_PORT);
        if(listenfd<0){
            INFO("uring_init_server failed\n");
            return -1;
        }
        set_event_accept(&ring,listenfd,(struct sockaddr*)&addr,&addrlen,0,EVENT_SLAVE_ACCEPT);
        INFO("从服务器接入端口：%d\n",SLAVE_PORT);
    }
    else if(s_or_c==1){//创建从服务器，创建和主服务器通信的fd
        int master_fd=connect_server();//连接主服务器
        if(master_fd<0){
            INFO("connect master failed\n");
            return -1;
        }
        set_event_recv(&ring,master_fd,connection[master_fd].rbuffer,BUFFER_SIZE,0,EVENT_SERVER_RECV);
    }
    #endif
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
            #if SYNC
            else if(result.event==EVENT_SERVER_RECV){
                recv_from_server_cb(entries,result.fd);
            }
            //else if(result.event==EVENT_SERVER_SEND){
            //     send_to_slave_cb(entries,result.fd);
            // }
            else if(result.event==EVENT_SLAVE_RECV){
                recv_from_slave_cb(entries,result.fd);
            }
            //else if(result.event==EVENT_SLAVE_SEND){
            //     send_server_cb(entries,result.fd);
            //}
            else if(result.event==EVENT_SLAVE_ACCEPT){
                accept_slave_cb(entries,result.fd);
            }
            #endif
        }
        io_uring_cq_advance(&ring, nready);
        io_uring_submit(&ring);
    }
    return 0;
}
#endif//NETWORK_SELECT == ENABLE_PROACTOR