#include<sys/socket.h>
#include<arpa/inet.h>
#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<errno.h>
#include<liburing.h>
#include<unistd.h>
#include<sys/time.h>
#include"server.h"
#define EVENT_RECV 0
#define EVENT_SEND 1
#define EVENT_ACCEPT 2
#define ENTRIES_LENGTH 1024
// int uring_recv_cb(int fd,void *arg);
// int uring_send_cb(int fd,void *arg);
// int uring_accept_cb(int fd,void *arg);
static struct timeval begin;
struct conn_info{//这个结构体只是为了io完成操作后标识fd和io类型的
    int fd;
    int event;
};
struct conn{
    int fd;
    char wbuffer[BUFFER_SIZE];
    char res_buf[BUFFER_SIZE];
    char rbuffer[BUFFER_SIZE];
    char buf[BUFFER_SIZE];
    int wlength;
    int rlength;
};
static struct conn connection[1024]={0};
int uring_init_server(unsigned int port){
    int socketfd=socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in addr;
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=htonl(INADDR_ANY);
    addr.sin_port=htons(port);
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
int set_event_send(struct io_uring*ring,int fd,void*buf,size_t len,int flag){
     struct io_uring_sqe* sqe=io_uring_get_sqe(ring);
     struct conn_info conn={
        .fd=fd,
        .event=EVENT_SEND,
    };
    io_uring_prep_send(sqe,fd,buf,len,flag);
    memcpy(&sqe->user_data,&conn,sizeof(conn));
}
int set_event_recv(struct io_uring*ring,int fd,void*buf,size_t len,int flag){
    struct io_uring_sqe* sqe=io_uring_get_sqe(ring);
     struct conn_info conn={
        .fd=fd,
        .event=EVENT_RECV,
    };
    io_uring_prep_recv(sqe,fd,buf,len,flag);
    memcpy(&sqe->user_data,&conn,sizeof(conn));
}
int set_event_accept(struct io_uring*ring,int fd,struct sockaddr* addr,socklen_t *addrlen,int flag){
    struct io_uring_sqe* sqe=io_uring_get_sqe(ring);
    INFO("io_uing_get_sqe\n");
    struct conn_info conn={//ring只是维护这个请求队列，是什么样的请求要自己写个结构体去标
        .fd=fd,
        .event=EVENT_ACCEPT,
    };
    io_uring_prep_accept(sqe,fd,(struct sockaddr*)addr,addrlen,flag);//
    INFO("io_uring_prep_accept\n");
    memcpy(&sqe->user_data,&conn,sizeof(conn));//sqe有个void*的user_data来存
    INFO("memcpy\n");
}
// typedef struct io_uring_arg_client{
//     struct io_uring *ring;
//     int fd;
//     void *buf;
//     size_t len;
//     int flag;
// }client_arg;
// typedef struct io_uring_arg_accept{
//     struct io_uring*ring;
//     int fd;
//     struct sockaddr* addr;
//     socklen_t *addrlen;
//     int flag;
// }accept_arg;
// int uring_recv_cb(int fd,void *arg){
//     client_arg* Arg=(client_arg*)arg;
//     memset(Arg->buf,0,BUFFER_SIZE);
//     int ret=set_event_recv(Arg->ring,Arg->fd,Arg->buf,Arg->len,Arg->flag);
//     connection[fd].rlength=ret;
// }
// int uring_send_cb(int fd,void *arg){
//     client_arg* Arg=(client_arg*)arg;
//     set_event_send(Arg->ring,Arg->fd,Arg->buf,Arg->len,Arg->flag);
// }
// int uring_accept_cb(int fd,void *arg){
//     accept_arg* Arg=(accept_arg*)arg;
//     set_event_accept(Arg->ring,Arg->fd,Arg->addr,Arg->addrlen,Arg->flag);
// }
int uring_start(unsigned int port){
    struct io_uring_params params;//
    memset(&params,0,sizeof(struct io_uring_params));
    struct io_uring ring;
    int ret= io_uring_queue_init_params(ENTRIES_LENGTH,&ring,&params);//params是这个函数的配置入参，内核把输出信息写回这个结构体
    struct sockaddr_in addr;
    socklen_t addrlen=sizeof(addr);
    int i=0;
    while(i<MAX_PORT){
        int socketfd=uring_init_server(port+i);//创建一个套接字fd
        printf("port %d is used\n",port+i);
        set_event_accept(&ring,socketfd,(struct sockaddr*)&addr,&addrlen,0);
        i++;
    }
    printf("network:proactor\n");
    gettimeofday(&begin,NULL);
    // char res_buf[BUFFER_SIZE]={0};
    // char buf[BUFFER_SIZE]={0};
    while(1){
        io_uring_submit(&ring);//提交给执行
        struct io_uring_cqe* cqe;
        io_uring_wait_cqe(&ring,&cqe);//
        struct io_uring_cqe* cqes[128];//128个io事件
        int nready=io_uring_peek_batch_cqe(&ring,cqes,128);//也是peek128个iO事件
        for(int i=0;i<nready;i++){
            struct io_uring_cqe* entries=cqes[i];
            struct conn_info result;
            memcpy(&result,&entries->user_data,sizeof(result));
            if(result.event==EVENT_RECV){
                int ret=entries->res;//理想状态下是对的，字节数
                if(ret>0){
                    INFO("set_event_recv ret: %d, %s\n", ret, connection[result.fd].rbuffer);
                    #if ENABLEKV
                    memcpy(connection[result.fd].rbuffer+connection[result.fd].rlength,connection[result.fd].buf,ret);
                    connection[result.fd].rlength+=ret;
                    if(kv_request(connection[result.fd].rbuffer,
                        connection[result.fd].wbuffer,&connection[result.fd].rlength,
                        &connection[result.fd].wlength)<0){
                        memset(&connection[result.fd],0,sizeof(struct conn));
                    }
                    #endif
                    //  client_arg arg={
                    // .ring=&ring,.fd=result.fd,.buf=connection[result.fd].wbuffer,.len=connection[result.fd].wlength,.flag=0
                    // };
                    // connection[result.fd].send_callback(result.fd,(void*)&arg);
                    if(connection[result.fd].wlength>0){
                        set_event_send(&ring,result.fd,connection[result.fd].wbuffer,connection[result.fd].wlength,0);//要同时监听可读事件和可写事件，不然如果数据没来完，就会卡死
                        set_event_recv(&ring,result.fd,connection[result.fd].buf,BUFFER_SIZE,0);
                    }else{
                        set_event_recv(&ring,result.fd,connection[result.fd].buf,BUFFER_SIZE,0);
                    }
                }else if(ret==0){
                    close(result.fd);
                }
            }else if(result.event==EVENT_ACCEPT){
                // accept_arg arg_accept={
                //      .ring=&ring,.fd=result.fd,.addr=(struct sockaddr*)&addr,.addrlen=&addrlen,.flag=0
                // };
                // connection[result.fd].r_action.accept_callback(result.fd,(void*)&arg_accept);
                struct sockaddr_in addr;
                memset(&addr,0,sizeof(struct sockaddr_in));
                socklen_t len=sizeof(struct sockaddr_in);
                set_event_accept(&ring,result.fd,(struct sockaddr*)&addr,&len,0);
                int connfd=entries->res;
                 if(connfd%10000==0){
                    struct timeval current;
                    gettimeofday(&current,NULL);
                    int time=TIME_SUB_MS(current,begin);
                    printf("Hello server:  clientfd-->%d,timeused:%d\n",connfd,time);
                    memcpy(&begin,&current,sizeof(struct timeval));
                }
            //     client_arg arg_recv={
            //     .ring=&ring,.fd=connfd,.buf=connection[connfd].rbuffer,.len=BUFFER_SIZE,.flag=0
            //    };
            //    connection[connfd].r_action.recv_callback(connfd,(void*)&arg_recv);
            set_event_recv(&ring,connfd,connection[connfd].buf,BUFFER_SIZE,0);
            }else if(result.event==EVENT_SEND){
                int ret=entries->res;
            //    client_arg arg={
            //     .ring=&ring,.fd=result.fd,.buf=connection[result.fd].rbuffer,.len=BUFFER_SIZE,.flag=0
            //    };
            //    connection[result.fd].r_action.recv_callback(result.fd,(void*)&arg);
            // }
                connection[result.fd].wlength-=ret;
                if(connection[result.fd].wlength>0){
                    memmove(connection[result.fd].wbuffer, 
                    connection[result.fd].wbuffer + ret, 
                    connection[result.fd].wlength);
                }
                if(kv_request(connection[result.fd].rbuffer,connection[result.fd].wbuffer,&connection[result.fd].rlength,&connection[result.fd].wlength)<0){
                    memset(&connection[result.fd],0,sizeof(struct conn));
                }
                if(connection[result.fd].wlength>0){
                        set_event_send(&ring,result.fd,connection[result.fd].wbuffer,connection[result.fd].wlength,0);//要同时监听可读事件和可写事件，不然如果数据没来完，就会卡死
                        set_event_recv(&ring,result.fd,connection[result.fd].buf,BUFFER_SIZE,0);  
                }else{
                        set_event_recv(&ring,result.fd,connection[result.fd].buf,BUFFER_SIZE,0);
                }
                        
            }
        }
        io_uring_cq_advance(&ring, nready);
    }
    return 0;
}