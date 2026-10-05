#ifndef _SYNC_H_
#define _SYNC_H
#include<stdio.h>
#define REPL_BACKLOG_SIZE   (1024*16)  // 16KB 积压缓冲区
#define REPL_TIME_OUT       30         // 从节点超时时间(秒)
#define REPL_HEART_SEC      1          // 心跳间隔(秒)

#include <stddef.h>
#include <stdint.h>
#include <time.h>

// ===================== 内部控制命令码 =====================
#define CMD_PSYNC        100  // 从→主：同步请求（携带凭证id + offset）
#define CMD_FULLRESYNC   101  // 主→主：开始全量同步（下发凭证id + full_end + master_offset）
#define CMD_CONTINUE     102  // 主→从：开始增量同步（下发补发截止偏移 master_offset）
#define CMD_REPLCONF_ACK 103  // 从→主：心跳+偏移上报（未实现）
#define CMD_SYNC_END     104  // 主→从：全量/补发传输结束
#define ERROR -2
#define WAIT -1
#define OVER 0
// 环形积压缓冲区
typedef struct ringbuffer{
    char *buf;
    uint64_t start;    // 缓冲区最旧数据对应的全局偏移
    uint64_t end;      // 缓冲区最新数据对应的全局偏移
    size_t size;       // 缓冲区总容量
} Ringbuffer;

// 单个从节点连接上下文（按 fd 索引，连接存活期间 fd 不变）
typedef struct slaveconn{
    int fd;
    int aof_fd;//全量重传aof文件fd
    uint64_t slave_offset;  // 该从节点已同步的逻辑偏移（复制流偏移）
    uint64_t file_offset;   // 全量同步文件传输进度（AOF读取偏移），与逻辑偏移解耦
    int status;             // 0-空闲 1-全量同步中 2-增量补发 3-增量在线 4-离线
    time_t last_heartbeat;  // 最后心跳时间
    struct slaveconn* next;
    uint64_t full_send;//全量重传的状态机
    uint64_t incremental_send;//增量重传的状态机
    uint64_t full_end;//全量重传应收数据长度
} Slaveconn;

// 主节点全局管理器
typedef struct mastermgr{
    int id;                 // 本节点运行ID（重启即变，兼作同步凭证签发）
    uint64_t master_offset; // 全局写入偏移量
    Ringbuffer *buf;        // 全局环形积压缓冲区
    Slaveconn *slave_head;  // 从节点链表头
    int slave_count;        // 在线从节点数量
} MasterMgr;
// 全局管理器实例（可选）
extern MasterMgr mgr;
// 从节点全局上下文
typedef struct slave{
    int master_fd;
    int master_id;          // 主节点签发的同步凭证（FULLRESYNC 时下发，存 repl.state 重启读回）
    uint64_t slave_offset;  // 本地已同步的逻辑偏移（复制流偏移）
    uint64_t file_offset;   // 全量同步已写入临时文件的字节数，与逻辑偏移解耦
    uint64_t master_offset;
    int status;             // 0-空闲/过渡(初始、全量数据收完待SYNC_END、出错重置) 1-全量接收中 2-增量补发接收中 3-增量在线接收中
    FILE *tmp_fp;
    uint64_t full_end;//
    uint64_t full_send;//
} Slave;
extern Slave slave;
// 主节点函数
/*
读函数参数：buf:缓冲区，buf_len:有效数据长度
写函数：buf:缓冲区，buf_len:缓冲区长度
函数返回值：-2出错,-1等待，0结束
*/
int master_init(MasterMgr *mgr);
void master_destory(MasterMgr* mgr);
int master_append_cmd(MasterMgr *mgr, uint8_t* buf, uint32_t buf_len);
int master_handle_sync(MasterMgr *mgr,int fd,uint8_t* buf,uint32_t buf_len);
uint64_t master_full_sync(MasterMgr *mgr,Slaveconn* slave, const char *aof_path,uint8_t *buf,uint32_t buf_len);
uint64_t master_incremental_sync(MasterMgr *mgr, Slaveconn* slave, uint8_t *buf,uint32_t buf_len);
uint64_t master_online_sync(MasterMgr *mgr, Slaveconn* slave, uint8_t *buf,uint32_t buf_len);
// kv写路径钩子：写命令入积压区后调用，唤醒增量补发(2)/在线(3)状态的从节点连接
void master_notify_slaves(MasterMgr *mgr);
//void master_check_timeout(MasterMgr* mgr, time_t now);
Slaveconn* slave_find_by_fd(MasterMgr *mgr,int fd);

// 从节点函数
int slave_init(Slave *slave);
void slave_destory(Slave* slave);
int slave_send_sync(Slave *slave,char *buf,int buf_len);
uint64_t slave_process(Slave *slave, uint8_t *buf, uint32_t buf_len);

//void slave_send_heartbeat(Slave *slave);

#endif
