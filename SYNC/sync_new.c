#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
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



