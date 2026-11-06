#ifndef _SYNC_NEW_H
#define _SYNC_NEW_H
#include<stdint.h>
#include<stddef.h>
#include<sys/types.h>

/* 每个从节点的发送缓冲：攒满才真正 send()（沿用原来的设计） */
#define SYNC_BUF_SIZE   (16*1024)
/* 从节点单条记录上限：50MB 的 value 会以一条 SET 记录过来 */
#define SYNC_MAX_RECORD (128u*1024u*1024u)

typedef struct slaveconn{
    int fd;
    /* 旧字段：网络层还在赋值，保留兼容，复制逻辑不再使用 */
    char *rbuffer;
    char *wbuffer;
    int *rlength;
    int *wlength;
    int buf_length;
    /* 复制专用：每从节点自己持有的发送缓冲 */
    char  sbuf[SYNC_BUF_SIZE];
    int   swlength;
    struct slaveconn* next;
} Slaveconn;


typedef struct mastermgr{
    int slave_count;        // 在线从节点数量
    Slaveconn *slave_head;  // 从节点链表头
    
} MasterMgr;


int master_init(MasterMgr *mgr);//初始化上下文
void master_destory(MasterMgr* mgr);
void slave_close(MasterMgr* mgr, int fd);
void KV_sync_reset(void);   /* 从节点断连时丢弃未重组完的记录 */
extern void notify(int fd);

/* 从节点 socket 是非阻塞的：这两个工具内部用 poll 等待可写，表现成阻塞发送 */
int sync_send_all(int fd,const void *buf,int len);
int sync_send_file(int out_fd,int in_fd,off_t size);
/* 把各从节点缓冲里还没发出去的数据立刻发掉（缓冲没满时用） */
void sync_flush(void);
#endif