#ifndef _SYNC_NEW_H
#define _SYNC_NEW_H
#include<stdint.h>
#include<stddef.h>
typedef struct slaveconn{
    int fd;
    char *rbuffer;
    char *wbuffer;
    int *rlength;
    int *wlength;
    int buf_length;
    struct slaveconn* next;
} Slaveconn;


typedef struct mastermgr{
    int slave_count;        // 在线从节点数量
    Slaveconn *slave_head;  // 从节点链表头
    
} MasterMgr;


int master_init(MasterMgr *mgr);//初始化上下文
void master_destory(MasterMgr* mgr);
void slave_close(MasterMgr* mgr, int fd);
extern void notify(int fd);
#endif