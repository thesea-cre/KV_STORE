#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include"tool.h"
#include "kvstore.h"
#include"sync_new.h"
int master_init(MasterMgr *mgr) {
    if (!mgr) return -1;
    memset(mgr, 0, sizeof(MasterMgr));
    mgr->slave_head = (Slaveconn*)kv_malloc(sizeof(Slaveconn));
    if (!mgr->slave_head) {
        return -1;
    }
    memset(mgr->slave_head, 0, sizeof(Slaveconn));
    mgr->slave_head->fd = -1;
    mgr->slave_head->next = NULL;

    mgr->slave_count = 0;
    return 0;
}

void master_destory(MasterMgr* mgr) {
    if (!mgr) return;
    sync_flush();               /* 退出前把缓冲里的增量发完 */
    Slaveconn *cur = mgr->slave_head;
    while (cur) {
        Slaveconn *next = cur->next;
        if (cur->fd >= 0) close(cur->fd);
        kv_free(cur);
        cur = next;
    }
    mgr->slave_head = NULL;
    mgr->slave_count = 0;
}

void slave_close(MasterMgr* mgr,int fd){
    Slaveconn* p=mgr->slave_head->next;
    Slaveconn* q=mgr->slave_head;
    while(p){
        if(p->fd==fd){
            q->next=p->next;
            p->next=NULL;
            kv_free(p);
            return;
        }
        q=q->next;
        p=p->next;
    }
}

/* ---------- 直接发送工具（从节点 socket 是非阻塞的） ---------- */

static int wait_writable(int fd){
    struct pollfd pfd;
    pfd.fd=fd;
    pfd.events=POLLOUT;
    pfd.revents=0;
    while(1){
        int r=poll(&pfd,1,-1);
        if(r<0){
            if(errno==EINTR) continue;
            return -1;
        }
        return (pfd.revents&POLLOUT)?0:-1;
    }
}

int sync_send_all(int fd,const void *buf,int len){
    const char *p=(const char*)buf;
    int off=0;
    while(off<len){
        ssize_t n=send(fd,p+off,len-off,0);
        if(n>0){ off+=(int)n; continue; }
        if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK)){
            if(wait_writable(fd)!=0) return -1;
            continue;
        }
        return -1;
    }
    return 0;
}

int sync_send_file(int out_fd,int in_fd,off_t size){
    off_t offset=0;
    while(offset<size){
        ssize_t n=sendfile(out_fd,in_fd,&offset,(size_t)(size-offset));
        if(n>0) continue;
        if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK)){
            if(wait_writable(out_fd)!=0) return -1;
            continue;
        }
        return -1;
    }
    return 0;
}



