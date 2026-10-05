#include "sync.h"
#include "kvstore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <assert.h>

// kv_request 按 s_or_c 分派主/从/单机协议，测试二进制需要提供一个定义（单机模式）
int s_or_c = 2;

// 辅助：从 socket 读取指定字节数
static ssize_t read_full(int fd, void *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = read(fd, (char*)buf + total, len - total);
        if (n <= 0) return n;
        total += n;
    }
    return total;
}

// 辅助：构造一条 TLV 写命令包（SET/MOD），返回包总长度
// 线上格式：10字节头 + TLV(key) + TLV(value)，header.length = payload 长度
static int build_kv_cmd(uint8_t *buf, uint16_t opcode, const char *key, const char *value) {
    uint32_t klen = (uint32_t)strlen(key) + 1;
    uint32_t vlen = (uint32_t)strlen(value) + 1;
    kv_header_t h;
    h.magic = KV_MAGIC;
    h.version = KV_VERSION;
    h.msg_type = MSG_TYPE_REQ;
    h.opcode = opcode;
    h.length = 7 + klen + 7 + vlen;  // payload 长度，kv_protocol 按它定包边界
    int offset = kv_header_encode(&h, buf, 128);
    TLV tlv;
    tlv.tag = TAG_KEY;   tlv.type = TYPE_STRING; tlv.length = klen; tlv.value = (uint8_t*)key;
    offset += kv_tlv_append(buf + offset, 128 - offset, tlv);
    tlv.tag = TAG_VALUE; tlv.type = TYPE_STRING; tlv.length = vlen; tlv.value = (uint8_t*)value;
    offset += kv_tlv_append(buf + offset, 128 - offset, tlv);
    return offset;
}

int main() {
    // 0. 初始化内存引擎：增量命令执行和 aof_recover 都依赖它
    init_kvengine();

    // 1. 准备临时 AOF 文件：内容是一条合法的 SET 命令包，作为全量同步数据源
    const char *aof_path = "test_aof.dat";
    uint8_t aof_buf[128] = {0};
    size_t aof_len = build_kv_cmd(aof_buf, OP_SET, "fs_key", "fs_val");
    FILE *fp = fopen(aof_path, "wb");
    assert(fp != NULL);
    fwrite(aof_buf, 1, aof_len, fp);
    fclose(fp);

    // 2. 初始化主/从上下文（mgr、slave 为 sync.c 定义的全局实例）
    assert(master_init(&mgr) == 0);
    assert(slave_init(&slave) == 0);
    printf("[OK] master_init / slave_init\n");

    // 3. socketpair 模拟主从连接
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    int master_fd = sv[0];  // 主节点侧
    int slave_fd  = sv[1];  // 从节点侧
    slave.master_fd = slave_fd;

    // 4. 从节点发 PSYNC(20B) → 主节点决策全量同步
    uint8_t req_buf[20];
    assert(slave_send_sync(&slave, (char*)req_buf, sizeof(req_buf)) == 20);
    write(slave_fd, req_buf, 20);
    uint8_t handle_buf[32];
    assert(read_full(master_fd, handle_buf, 20) == 20);
    assert(master_handle_sync(&mgr, master_fd, handle_buf, 20) == 20);
    Slaveconn *slave_conn = slave_find_by_fd(&mgr, master_fd);
    assert(slave_conn != NULL);
    assert(slave_conn->status == 1);  // 新从节点 → 全量同步
    printf("[OK] master_handle_sync：新从节点，决策全量同步\n");

    // 5. 主节点全量同步：FULLRESYNC(24B,含full_end+master_offset) → 文件流 → SYNC_END(4B)
    uint8_t sync_buf[1024];
    uint64_t total_sent = 0;
    while (slave_conn->status != 3) {
        uint64_t n = master_full_sync(&mgr, slave_conn, aof_path, sync_buf, sizeof(sync_buf));
        if (n > 0) {
            write(master_fd, sync_buf, n);
            total_sent += n;
        }
    }
    assert(total_sent == 24 + aof_len + 4);
    printf("[OK] 全量同步完成，主节点共发送 %lu 字节\n", total_sent);

    // 6. 从节点按固定帧接收：控制包24B → 文件aof_len字节 → SYNC_END 4B
    uint8_t recv_buf[1024];
    assert(read_full(slave_fd, recv_buf, 24) == 24);
    assert(slave_process(&slave, recv_buf, 24) == 24);
    assert(slave.status == 1);
    assert(slave.full_end == aof_len);
    assert(read_full(slave_fd, recv_buf, aof_len) == (ssize_t)aof_len);
    assert(slave_process(&slave, recv_buf, aof_len) == (uint64_t)aof_len);
    assert(slave.status == 0);  // 文件收完，等待随后的 CMD_SYNC_END
    assert(read_full(slave_fd, recv_buf, 4) == 4);
    assert(slave_process(&slave, recv_buf, 4) == 4);
    assert(slave.status == 3);  // SYNC_END 处理完 → 增量在线
    assert(slave.slave_offset == 0);  // 对齐 FULLRESYNC 下发的 master_offset
    printf("[OK] 从节点完成全量接收\n");

    // 7. 校验从节点落盘文件与主节点 AOF 一致，且引擎已通过 aof_recover 加载
    FILE *check = fopen("aof_db", "rb");
    assert(check != NULL);
    uint8_t check_buf[256] = {0};
    size_t rd = fread(check_buf, 1, sizeof(check_buf), check);
    fclose(check);
    assert(rd == aof_len);
    assert(memcmp(check_buf, aof_buf, aof_len) == 0);
    assert(kv_get((char*)"fs_key") != NULL && strcmp(kv_get((char*)"fs_key"), "fs_val") == 0);
    printf("[OK] 全量数据一致，引擎加载成功\n");

    // 8. 增量补发：主节点写入一条 SET，从节点二次 PSYNC → 决策增量补发
    uint8_t set_buf[128] = {0};
    size_t set_len = build_kv_cmd(set_buf, OP_SET, "k", "v1");
    master_append_cmd(&mgr, set_buf, set_len);
    assert(mgr.master_offset == set_len);
    slave.status = 0;        
    slave.file_offset = 0;
    assert(slave_send_sync(&slave, (char*)req_buf, sizeof(req_buf)) == 20);
    write(slave_fd, req_buf, 20);
    assert(read_full(master_fd, handle_buf, 20) == 20);
    assert(master_handle_sync(&mgr, master_fd, handle_buf, 20) == 20);
    assert(slave_conn->status == 2);  // 偏移在积压缓冲区内 → 增量补发
    printf("[OK] master_handle_sync：已登记从节点，决策增量补发\n");

    // 9. 主节点补发：CONTINUE(12B,含master_offset) → 命令数据 → SYNC_END(4B)
    while (slave_conn->status != 3) {
        uint64_t n = master_incremental_sync(&mgr, slave_conn, sync_buf, sizeof(sync_buf));
        if (n > 0) write(master_fd, sync_buf, n);
    }
    assert(slave_conn->incremental_send == 0);

    // 10. 从节点接收并执行：CONTINUE → 数据(执行SET) → SYNC_END(在线态识别后吃掉)
    assert(read_full(slave_fd, recv_buf, 12) == 12);
    assert(slave_process(&slave, recv_buf, 12) == 12);
    assert(slave.status == 2);
    assert(slave.master_offset == set_len);
    assert(read_full(slave_fd, recv_buf, set_len) == (ssize_t)set_len);
    assert(slave_process(&slave, recv_buf, set_len) == set_len);
    assert(slave.status == 3);          // 追平 master_offset，转增量在线
    assert(slave.slave_offset == set_len);
    assert(kv_get((char*)"k") != NULL && strcmp(kv_get((char*)"k"), "v1") == 0);
    assert(read_full(slave_fd, recv_buf, 4) == 4);
    assert(slave_process(&slave, recv_buf, 4) == 4);  // 补发收尾 SYNC_END 直接吃掉
    assert(slave.status == 3);
    printf("[OK] 增量补发完成，SET 已在从节点执行\n");

    // 11. 增量在线：无握手无收尾标记，主节点直接从积压区转发新写入
    uint8_t mod_buf[128] = {0};
    size_t mod_len = build_kv_cmd(mod_buf, OP_MOD, "k", "v2");
    master_append_cmd(&mgr, mod_buf, mod_len);
    assert(mgr.master_offset == set_len + mod_len);
    uint64_t on = master_online_sync(&mgr, slave_conn, sync_buf, sizeof(sync_buf));
    assert(on == mod_len);
    write(master_fd, sync_buf, on);
    assert(master_online_sync(&mgr, slave_conn, sync_buf, sizeof(sync_buf)) == 0);  // 已追平
    assert(read_full(slave_fd, recv_buf, mod_len) == (ssize_t)mod_len);
    assert(slave_process(&slave, recv_buf, mod_len) == mod_len);
    assert(slave.slave_offset == set_len + mod_len);
    assert(kv_get((char*)"k") != NULL && strcmp(kv_get((char*)"k"), "v2") == 0);
    printf("[OK] 增量在线转发完成，MOD 已在从节点执行\n");

    // 12. 清理（连接 fd 由 destory 关闭）
    master_destory(&mgr);
    slave_destory(&slave);
    dest_kvengine();
    unlink(aof_path);
    unlink("aof_db");
    unlink("aof_db_tmp");
    unlink("repl.state");

    printf("所有测试通过！\n");
    return 0;
}
