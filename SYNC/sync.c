#include "sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
//kvstore.h 内含 aof.h；INFO 需先定义，其 #ifndef INFO 守卫会跳过空宏定义
#include "kvstore.h"
//sync.h 中 extern 的两个全局实例在此定义（网络层 reactor.c 直接使用）
MasterMgr mgr;
Slave slave;
// 64位整数网络字节序转换
#define INFO printf
static uint64_t htonll(uint64_t x) {
    return ((uint64_t)htonl((uint32_t)(x >> 32)) | ((uint64_t)htonl((uint32_t)x) << 32));
}
static uint64_t ntohll(uint64_t x) {
    return ((uint64_t)ntohl((uint32_t)(x >> 32)) | ((uint64_t)ntohl((uint32_t)x) << 32));
}

// ===================== 环形缓冲区底层操作 =====================
static int ringbuffer_init(Ringbuffer *rb, size_t size) {
    rb->buf = (char*)malloc(size);
    if (!rb->buf) return -1;
    rb->start = 0;
    rb->end = 0;
    rb->size = size;
    return 0;
}

static void ringbuffer_destroy(Ringbuffer *rb) {
    if (rb->buf) free(rb->buf);
    rb->buf = NULL;
    rb->start = rb->end = rb->size = 0;
}

// 写入数据到环形缓冲区，自动覆盖旧数据
static int ringbuffer_write(Ringbuffer *rb, const uint8_t *data, uint32_t len) {
    if (len > rb->size) {
        data += len - rb->size;
        len = (uint32_t)rb->size;
    }
    size_t phys_end = rb->end % rb->size;
    if (phys_end + len <= rb->size) {//剩余空间足够装
        memcpy(rb->buf + phys_end, data, len);
    } else {
        size_t part1 = rb->size - phys_end;//不足够，part1剩余空间
        memcpy(rb->buf + phys_end, data, part1);//先把剩余空间装满
        memcpy(rb->buf, data + part1, len - part1);//再将剩下的装到前面
    }

    rb->end += len;
    if (rb->end - rb->start > rb->size) {
        rb->start = rb->end - rb->size;
    }
    return len;
}

// 从指定偏移读取数据，返回实际读取字节数，读到out中，最多读max_len个字节
static int ringbuffer_read(Ringbuffer *rb, uint64_t offset, uint8_t *out, uint32_t max_len) {
    if (offset < rb->start || offset >= rb->end) return 0;

    uint64_t available = rb->end - offset;//有多少数据可读
    uint32_t read_len = (available > max_len) ? max_len : (uint32_t)available;

    size_t phys_start = offset % rb->size;
    if (phys_start + read_len <= rb->size) {//没有超
        memcpy(out, (rb->buf )+ phys_start, read_len);
    } else {
        size_t part1 = rb->size - phys_start;//
        memcpy(out, rb->buf + phys_start, part1);
        memcpy(out + part1, rb->buf, read_len - part1);
    }
    return read_len;
}

// ===================== 主节点核心函数 =====================

/**
 * @brief 初始化主节点复制管理器
 * @return 0成功 -1失败
 */
int master_init(MasterMgr *mgr) {
    if (!mgr) return -1;
    memset(mgr, 0, sizeof(MasterMgr));

    // 生成随机运行ID（重启即变）
    srand((unsigned int)time(NULL));
    mgr->id = rand();

    // 初始化全局环形积压缓冲区
    mgr->buf = (Ringbuffer*)malloc(sizeof(Ringbuffer));
    if (!mgr->buf) return -1;
    if (ringbuffer_init(mgr->buf, REPL_BACKLOG_SIZE) != 0) {
        free(mgr->buf);
        return -1;
    }

    mgr->master_offset = 0;
    mgr->slave_head = NULL;
    mgr->slave_count = 0;
    return 0;
}

/**
 * @brief 销毁主节点复制管理器，释放所有资源
 */
void master_destory(MasterMgr* mgr) {
    if (!mgr) return;
    if (mgr->buf) {
        ringbuffer_destroy(mgr->buf);
        free(mgr->buf);
        mgr->buf = NULL;
    }
    Slaveconn *cur = mgr->slave_head;
    while (cur) {
        Slaveconn *next = cur->next;
        if (cur->fd >= 0) close(cur->fd);
        free(cur);
        cur = next;
    }
    mgr->slave_head = NULL;
    mgr->slave_count = 0;
}

/**
 * @brief 写命令追加到复制流：仅写入积压缓冲区并推进全局偏移，发送交给网络层
 * @param mgr 主管理器指针
 * @param buf 完整TLV命令包
 * @param buf_len 包长
 * @return 写入字节数
 */
int master_append_cmd(MasterMgr *mgr, uint8_t* buf, uint32_t buf_len) {
    if (!mgr || !buf||buf_len==0) return ERROR;

    // 写入全局环形积压缓冲区；增量补发(状态0)与实时转发(状态2)都由网络层从积压区取数据发送
    int ret = ringbuffer_write(mgr->buf, buf, buf_len);
    mgr->master_offset += buf_len;
    return ret;
}

//buf_len数据长度
int master_handle_sync(MasterMgr *mgr,int fd,uint8_t *buf,uint32_t buf_len) {
    if (!mgr || fd < 0||!buf) return ERROR;
    if(buf_len==0){
        return OVER;
    }
    if(buf_len<20){
        return WAIT;
    }
    uint32_t cmd,total_len;
    uint32_t rid;
    uint64_t off;
    memcpy(&cmd,buf,4);
    memcpy(&rid,buf+4,4);
    memcpy(&off,buf+8,8);
    memcpy(&total_len,buf+16,4);
    int32_t req_runid;//runid
    uint64_t req_offset;//slave的流偏移量
    uint32_t CMD;
    uint32_t TOTAL;
    CMD=ntohl(cmd);
    req_runid=ntohl(rid);
    req_offset=ntohll(off);
    TOTAL=ntohl(total_len);
    switch(CMD){
        case CMD_PSYNC://启动或者重连的时候
            // Slaveconn *slave = slave_find_by_id(mgr, req_runid);
            // 更新心跳时间
            // slave->last_heartbeat = time(NULL);
            // 新来的从服务器节点加入进来
            // if (slave==NULL) {
            Slaveconn *slave_t=(Slaveconn*)malloc(sizeof(struct slaveconn));
            memset(slave_t,0,sizeof(Slaveconn));//full_send/incremental_send/file_offset等状态机字段全部归零
            // slave_t->slave_id=req_runid;
            slave_t->fd=fd;
            slave_t->aof_fd=-1;
            slave_t->slave_offset=req_offset;
            slave_t->next=mgr->slave_head;
            slave_t->full_send=0;
            slave_t->incremental_send=0;
            mgr->slave_head=slave_t;
            mgr->slave_count++;
            //}
            if(req_runid!=mgr->id){//凭证不通过
                slave_t->status=1;
            }
            // 校验2：偏移在积压缓冲区内 → 增量同步
            else if (req_offset >= mgr->buf->start && req_offset <= mgr->buf->end) {
                slave_t->status = 2;       // 增量补发中：reactor驱动补发，追平后转增量在线
                // 清掉可能残留的全量/补发传输状态，从干净状态开始补发
                if (slave_t->aof_fd >= 0) { close(slave_t->aof_fd); slave_t->aof_fd = -1; }
            }else{
                // 偏移超出缓冲区 → 降级全量同步
                slave_t->status = 1;
            }
            return 20;
        default:
            return OVER;
    }
    return OVER;
    
}


/**
 * @brief 全量同步：直接传输AOF文件
 * @return 本次应发送的字节数(>0，含收尾的4字节CMD_SYNC_END)，-1出错
 */
 //buf_len代表缓冲区可写空间；FULLRESYNC控制包携带full_end（AOF文件实际大小）作为全量数据长度
uint64_t master_full_sync(MasterMgr *mgr,Slaveconn *slave,const char *aof_path,uint8_t *buf,uint32_t buf_len) {
    if (!mgr||!slave || !aof_path ||  !buf||buf_len==0) return ERROR;
    // 发送FULLRESYNC控制包
    
    if(slave->full_send==0){
        uint32_t cmd = htonl(CMD_FULLRESYNC);
        int32_t rid = htonl(mgr->id);
        
        memcpy(buf, &cmd, 4);
        memcpy(buf + 4, &rid, 4);
        
        slave->full_send=1;
        slave->file_offset=0;                    // 文件传输进度归零，从文件头读起
        slave->slave_offset=mgr->master_offset;  // 逻辑偏移直接对齐全量起点（FULLRESYNC下发值）
        slave->aof_fd = open(aof_path, O_RDONLY);//这里每次都要打开有点麻烦
        if (slave->aof_fd < 0) return ERROR;
        // 锁定当前AOF文件大小作为全量截止点
        struct stat st;
        if (fstat(slave->aof_fd, &st) != 0) {
            close(slave->aof_fd);
            return ERROR;
        }
        slave->full_end= st.st_size;
        uint64_t off = htonll(slave->full_end);
        memcpy(buf + 8, &off, 8);
        uint64_t master_off=htonll(mgr->master_offset);
        memcpy(buf+16,&master_off,8);
        
        return 24;
    }
    if(slave->full_send==1){
    // 分块传输AOF文件
        if(slave->file_offset>=slave->full_end){
            slave->full_send=2;
        }
        else{
            uint32_t read_len = buf_len;//一次性读取的字节数
            if (slave->file_offset+read_len >= slave->full_end) {
                read_len = (uint32_t)(slave->full_end - slave->file_offset);
            }//这里fd是磁盘io
            ssize_t n = pread(slave->aof_fd, buf, read_len,slave->file_offset);//这里感觉可以优化一下用sendfile()
            if (n < 0){
                close(slave->aof_fd);
                slave->aof_fd=-1;
                return ERROR;
            }
            slave->file_offset+=n;
            return n;   
            }
        
    }
    // 发送全量结束标记
    if(slave->full_send==2){
        uint32_t end_cmd = htonl(CMD_SYNC_END);
        memcpy(buf,&end_cmd,4);
        slave->full_send=0;
        slave->file_offset=0;
        slave->status=3;//进入增量在线
        if (slave->aof_fd >= 0) {
            close(slave->aof_fd);
            slave->aof_fd = -1;
        }
        return 4;
    }
    return OVER;
}

/**
 * @brief 增量同步：从积压缓冲区补发偏移之后的数据
 * @return 补发字节数
 */
uint64_t master_incremental_sync(MasterMgr *mgr, Slaveconn* slave, uint8_t *buf,uint32_t buf_len) {
    if (!mgr || !slave || !buf||buf_len==0) return ERROR;
    uint64_t start_offset=slave->slave_offset;
    // 先发送CONTINUE确认，携带补发截止偏移master_offset，从节点收到后进入增量补发并据其判断补发结束
    if(slave->incremental_send==0){
        uint32_t cmd = htonl(CMD_CONTINUE);
        INFO("发送CMD_CONTINUE\n");
        uint64_t off = htonll(mgr->master_offset);
        memcpy(buf,&cmd,4);
        memcpy(buf+4,&off,8);
        slave->incremental_send=1;
        return 12;
    }
    if(slave->incremental_send==1){
        INFO("发送数据\n");
         if (slave->slave_offset == mgr->master_offset) {
            slave->incremental_send = 2;
        } else{
            int n = ringbuffer_read(mgr->buf, start_offset, buf, buf_len);
            if (n < 0){
                return ERROR;
            }else if(n==0){
                slave->incremental_send=2;
            }else{
                slave->slave_offset +=n;
                return n;
            }
        }
        
    } 
    if(slave->incremental_send==2){
        uint32_t cmd=htonl(CMD_SYNC_END);
        INFO("发送CMD_SYNC_END\n");
        memcpy(buf,&cmd,4);
        slave->incremental_send=0;
        slave->status=3; //增量补发接受完后进入增量在线
        return 4;
    }
    return OVER;
}

/**
 * @brief 增量在线：无握手无收尾标记，直接从积压区取[slave_offset, master_offset)的数据发送
 * @return 本次应发送的字节数；0=已追平或偏移已被积压区淘汰（网络层可切EPOLLIN等唤醒/重同步），-1出错
 */
uint64_t master_online_sync(MasterMgr *mgr, Slaveconn* slave, uint8_t *buf, uint32_t buf_len) {
    if (!mgr || !slave || !buf || buf_len == 0) return ERROR;
    if (slave->slave_offset >= mgr->master_offset) return OVER;  // 已追平，无数据可发
    if (slave->slave_offset < mgr->buf->start) {
        INFO("掉线移除\n");
        Slaveconn* p=mgr->slave_head;
        while(p->next!=slave){
            p=p->next;
        }
        p->next=slave->next;
        slave->next=NULL;
        free(slave);
        return OVER;
    }
    int n = ringbuffer_read(mgr->buf, slave->slave_offset, buf, buf_len);
    if (n <= 0) return OVER;
    slave->slave_offset += n;
    return n;
}

// /**
//  * @brief 定时检测从节点心跳超时，移除掉线节点
//  * @param now 当前时间戳
//  */
// void master_check_timeout(MasterMgr* mgr, time_t now) {
//     if (!mgr) return; 

//     Slaveconn **cur = &mgr->slave_head;
//     while (*cur) {
//         Slaveconn *node = *cur;
//         if (now - node->last_heartbeat > REPL_TIME_OUT) {
//             *cur = node->next;
//             if (node->fd >= 0) close(node->fd);
//             free(node);
//             mgr->slave_count--;
//         } else {
//             cur = &node->next;
//         }
//     }
// }
// 按fd查找从节点连接
// Slaveconn* slave_find_by_id(MasterMgr *mgr, int id) {
//     Slaveconn *cur = mgr->slave_head;
//     while (cur) {
//         if (cur->slave_id == id) return cur;
//         cur = cur->next;
//     }
//     return NULL;
// }
Slaveconn* slave_find_by_fd(MasterMgr *mgr, int fd) {
    Slaveconn *cur = mgr->slave_head;
    while (cur) {
        if (cur->fd == fd) return cur;
        cur = cur->next;
    }
    return NULL;
}
// ===================== 从节点核心函数 =====================

/**
 * @brief 初始化从节点同步上下文
 */
int slave_init(Slave *slave) {
    if (!slave) return -1;
    memset(slave, 0, sizeof(Slave));
    slave->master_fd = -1;
    // srand((unsigned int)time(NULL));
    // slave->slave_id = rand();   // 本节点ID，重启即变
    FILE *file=fopen("repl_stat","rb");
    if(file!=NULL){
        if(fread(&slave->master_id,4,1,file)!=1){
            slave->master_id=0;
        }
        if(fread(&slave->slave_offset,8,1,file)!=1){
            slave->slave_offset=0;
        }
        fclose(file);
    }
    INFO("slave->master_id=%d\n",slave->master_id);
    INFO("slave->slave_offset=%d\n",slave->slave_offset);
    // slave->master_id = 0;       // 尚未保存任何主节点的runid
    // slave->slave_offset = 0;
    slave->file_offset = 0;
    slave->status = 0;
    return 0;
}

/**
 * @brief 销毁从节点上下文，关闭连接
 */
void slave_destory(Slave* slave) {
    if (!slave) return;
    if (slave->master_fd >= 0) close(slave->master_fd);
    slave->master_fd = -1;
    slave->master_id = 0;
    slave->slave_offset = 0;
    slave->status = 0;
}

/**
 * @brief 向主节点发送PSYNC同步请求
 */
/**
 * @brief  从节点发送PSYNC同步请求（标准TLV帧格式）
 * @return 发送字节数，失败返回-1
 */
int slave_send_sync(Slave *slave,char *buf,int buf_len) {
    if (!slave || slave->master_fd < 0) return ERROR;
    if(buf_len==0) return OVER;
    // 帧结构：4字节命令码 + 4字节slave_id + 8字节offset + 4字节总长度 = 20字节
    uint32_t total_len = htonl(20);              // 帧总长度
    uint32_t cmd = htonl(CMD_PSYNC);             // 命令码

    uint32_t rid = htonl(slave->master_id);       // ID
    uint64_t off = htonll(slave->slave_offset);  // 本地同步偏移量

    // 按顺序组装帧

    memcpy(buf, &cmd, 4);            // 偏移0：命令码
    memcpy(buf + 4, &rid, 4);        // 偏移4：id
    memcpy(buf + 8, &off, 8);        // 偏移8：偏移量
    memcpy(buf+16, &total_len, 4);   // 偏移16：帧总长度
    return 20;
}
/**
 * @brief 从节点处理收到的数据包
 * @return 处理的字节数
 */
uint64_t slave_process(Slave *slave, uint8_t *buf, uint32_t buf_len) {
    if (!slave || !buf || buf_len==0) return ERROR;
    if(slave->status==1){//正处于全量同步状态
        if(slave->file_offset>=slave->full_end){
            slave->status=0;
            return OVER;
        }
        if(slave->full_end-slave->file_offset<=buf_len){
            slave->status=0;//文件数据已收完，等待随后的CMD_SYNC_END
            buf_len=(uint32_t)(slave->full_end-slave->file_offset);
            INFO("全量重传字节流读完,这次读的字节长度%d(bytes)\n",buf_len);
        }
        fwrite(buf,1,buf_len,slave->tmp_fp);
        slave->file_offset+=buf_len;
        INFO("这次全量重传写入到暂时文件的字节数：%d(bytes)\n",buf_len);
        return buf_len;
    }
    if(slave->status==2){//增量补发中：执行命令并累计偏移，到达master_offset即转实时转发

        int ret=kv_incremental(buf,buf_len);
        if(ret>0){
            slave->slave_offset+=ret;
            if(slave->slave_offset>=slave->master_offset){
                slave->status=3;
            }
        }
        return ret>OVER?ret:OVER;
    }
    if(slave->status==3){//增量实时转发中：无截止偏移，SYNC_END只是补发收尾标记，识别后直接吃掉
        if(buf_len>=4){
            uint32_t live_cmd;
            memcpy(&live_cmd, buf, 4);
            if(ntohl(live_cmd)==CMD_SYNC_END){
                return 4;
            }
        }
        
        int ret=kv_incremental(buf,buf_len);
        if(ret>0){
            slave->slave_offset+=ret;
        }
        return ret>OVER?ret:OVER;
    }
    if(buf_len<4){//不足一个命令码，等下一段数据
        return WAIT;
    }
    uint32_t cmd;
    memcpy(&cmd, buf, 4);
    cmd = ntohl(cmd);
    switch (cmd) {
        case CMD_FULLRESYNC: {
            if(buf_len<24){//控制包被TCP拆包，等数据凑齐再解析
                return WAIT;
            }
            int32_t rid;
            uint64_t off;
            uint64_t master_off;
            memcpy(&rid, buf + 4, 4);
            memcpy(&off, buf + 8, 8);
            memcpy(&master_off,buf+16,8);
            slave->master_id = ntohl(rid);
            slave->full_end=ntohll(off);
            slave->file_offset=0;
            slave->master_offset=ntohll(master_off);
            INFO("全量重传数据数据流长度:%d(bytes)\n",slave->full_end);
            slave->status = 1; // 进入全量接收中
            slave->tmp_fp=fopen("aof_db_tmp","wb");
            return 24;
        }
        case CMD_CONTINUE: {
            if(buf_len<12){//控制包被TCP拆包，等数据凑齐再解析
                return WAIT;
            }
            uint64_t off;
            memcpy(&off, buf + 4, 8);
            slave->master_offset = ntohll(off);//补发截止偏移
            slave->status = 2; // 进入增量补发中
            return 12;
        }
        case CMD_SYNC_END: {
            if (slave->tmp_fp) {
            fclose(slave->tmp_fp);
            slave->tmp_fp = NULL;
        }

        // 2. 完整性校验：临时文件大小必须和FULLRESYNC约定的截止偏移完全一致
        struct stat st;
        if (stat("aof_db_tmp", &st) != 0 
            || (uint64_t)st.st_size != slave->full_end) {
            INFO("全量文件校验失败，预期%lu字节，实际%ld字节\n", 
                slave->full_end, st.st_size);
            // 校验失败直接删除临时文件，绝不覆盖正式文件
            unlink("aof_db_tmp");
            slave->status = 0; // 重置状态，等待重连重传
            return OVER;
        }

        // 3. 原子重命名：临时文件 覆盖 正式AOF文件
        // 同文件系统下rename是原子操作：要么成功替换，要么原文件完好无损
        if (rename("aof_db_tmp", "aof_db") != 0) {
            perror("全量文件替换失败");
            unlink("aof_db_tmp");
            slave->status = 0;
            return OVER;
        }

        // 4. 从正式AOF文件加载数据到内存引擎
        INFO("全量传输完成，调用aof函数加载数据\n");
        aof_recover();

        // 5. 偏移量直接对齐全量截止点，避免累加误差
        slave->slave_offset = slave->master_offset;
        //5.保存id和slave_offset
        FILE *file = fopen("repl_stat", "wb");
        if (file == NULL) {
            perror("Failed to open file");
            return -1; // 【注意】千万不要在这里 fclose(file)，因为file是NULL，会崩溃
        }

        // 3. 写入第一个数据
        // fwrite(数据地址, 单个元素大小, 元素个数, 文件指针)
        if (fwrite(&slave->master_id, 4, 1, file) != 1) {
            perror("Failed to write 4 byte");
            fclose(file);
            return -1;
        }

        // 4. 写入第二个数据
        // 此时文件指针已经自动指向
        if (fwrite(&slave->slave_offset, 8, 1, file) != 1) {
            perror("Failed to write 8 bytes");
            fclose(file);
            return -1;
        }
        fclose(file);
        slave->status = 3; 

        return 4;
        }
        default:
            // 普通写命令包：直接调用现有kv_protocol执行，关闭AOF写入
            // int ret = kv_protocol((char*)buf, buf_len, NULL, 0);
            // if (ret > 0) slave->slave_offset += ret;
            // return ret > 0 ? ret : 0;
            return 0; // 占位，替换为你的kv_protocol返回值
        }
    
}

/**
 * @brief 从节点发送心跳，上报本地偏移
 */
/**
 * @brief  从节点发送心跳，上报本地偏移（标准TLV帧格式）
 */
// void slave_send_heartbeat(Slave *slave) {
//     if (!slave || slave->master_fd < 0) return;

//     // 帧结构：4字节总长度 + 4字节命令码 + 8字节偏移 = 16字节
//     uint8_t buf[16];
//     uint32_t total_len = htonl(16);          // 总长度：包头8字节 + 负载8字节
//     uint32_t cmd = htonl(CMD_REPLCONF_ACK);  // 命令码
//     uint64_t off = htonll(slave->slave_offset); // 本地同步偏移量

//     memcpy(buf, &total_len, 4);    // 偏移0：总长度
//     memcpy(buf + 4, &cmd, 4);      // 偏移4：命令码
//     memcpy(buf + 8, &off, 8);      // 偏移8：负载-偏移量

//     send(slave->master_fd, buf, sizeof(buf), 0);
// }

