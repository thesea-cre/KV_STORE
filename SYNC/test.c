// SYNC 组件测试：master/slave 状态机 + 环形积压缓冲区 + 端到端 socketpair 回放
// 风格与 engine/protocol/mempool/AOF 测试一致：CHECK 宏断言 + ASan/LSan
// 注意：slave 侧函数会读写 cwd 下的 repl_stat / aof_db / aof_db_tmp，
//       所以整个测试跑在 mkdtemp 临时目录里，不碰仓库文件
#include "sync.h"
#include "kvstore.h"
#include "aof.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>

// kv_request 按 s_or_c 分派主/从/单机协议，测试二进制需要提供一个定义（单机模式）
int s_or_c = 2;

static int g_checks = 0, g_fails = 0;
#define CHECK(cond) do {                                                  \
    g_checks++;                                                           \
    if (!(cond)) {                                                        \
        g_fails++;                                                        \
        printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
    }                                                                     \
} while (0)

// 辅助：从 fd 读满 len 字节
static ssize_t read_full(int fd, void *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = read(fd, (char*)buf + total, len - total);
        if (n <= 0) return n;
        total += n;
    }
    return total;
}

// 辅助：构造一条 TLV 写命令包（10字节头 + TLV(key) + TLV(value)），返回包总长度
static int build_kv_cmd(uint8_t *buf, uint16_t opcode, const char *key, const char *value) {
    uint32_t klen = (uint32_t)strlen(key) + 1;
    uint32_t vlen = (uint32_t)strlen(value) + 1;
    kv_header_t h;
    h.magic = KV_MAGIC;
    h.version = KV_VERSION;
    h.msg_type = MSG_TYPE_REQ;
    h.opcode = opcode;
    h.length = 7 + klen + 7 + vlen;
    int offset = kv_header_encode(&h, buf, 256);
    TLV tlv;
    tlv.tag = TAG_KEY;   tlv.type = TYPE_STRING; tlv.length = klen; tlv.value = (uint8_t*)key;
    offset += kv_tlv_append(buf + offset, 256 - offset, tlv);
    tlv.tag = TAG_VALUE; tlv.type = TYPE_STRING; tlv.length = vlen; tlv.value = (uint8_t*)value;
    offset += kv_tlv_append(buf + offset, 256 - offset, tlv);
    return offset;
}

// 辅助：构造 PSYNC 帧（4命令 + 4id + 8offset + 4总长 = 20字节）
static void build_psync(uint8_t *buf, uint32_t rid, uint64_t off, uint32_t total) {
    uint32_t cmd = htonl(CMD_PSYNC);
    uint32_t r   = htonl(rid);
    uint64_t o   = ((uint64_t)htonl((uint32_t)(off >> 32)) | ((uint64_t)htonl((uint32_t)off) << 32));
    uint32_t t   = htonl(total);
    memcpy(buf, &cmd, 4);
    memcpy(buf + 4, &r, 4);
    memcpy(buf + 8, &o, 8);
    memcpy(buf + 16, &t, 4);
}

// 辅助：从 FULLRESYNC 24字节控制包解析字段
static void parse_fullresync(const uint8_t *buf, uint32_t *cmd, uint32_t *rid, uint64_t *off, uint64_t *moff) {
    uint32_t c, r; uint64_t o, m;
    memcpy(&c, buf, 4);     memcpy(&r, buf + 4, 4);
    memcpy(&o, buf + 8, 8); memcpy(&m, buf + 16, 8);
    *cmd  = ntohl(c);
    *rid  = ntohl(r);
    *off  = ((uint64_t)ntohl((uint32_t)(o >> 32)) | ((uint64_t)ntohl((uint32_t)o) << 32));
    *moff = ((uint64_t)ntohl((uint32_t)(m >> 32)) | ((uint64_t)ntohl((uint32_t)m) << 32));
}

// ---------------- S1 master_init / master_destory ----------------
static void test_master_init_destory(void) {
    printf("== master_init / master_destory ==\n");
    CHECK(master_init(NULL) == -1);

    MasterMgr m;
    CHECK(master_init(&m) == 0);
    CHECK(m.slave_head != NULL);            // 虚拟头节点
    CHECK(m.slave_head->fd == -1);
    CHECK(m.slave_head->next == NULL);
    CHECK(m.slave_count == 0);
    CHECK(m.master_offset == 0);
    CHECK(m.buf != NULL);
    CHECK(m.buf->size == REPL_BACKLOG_SIZE);
    CHECK(m.buf->start == 0 && m.buf->end == 0);

    master_destory(&m);
    CHECK(m.slave_head == NULL);
    CHECK(m.buf == NULL);
    master_destory(&m);   // 重复销毁不应崩
    master_destory(NULL); // NULL 不应崩
}

// ---------------- S2 master_append_cmd ----------------
static void test_master_append_cmd(void) {
    printf("== master_append_cmd ==\n");
    uint8_t tmp[64] = {0};
    MasterMgr m;
    master_init(&m);
    CHECK(master_append_cmd(NULL, tmp, 1) == ERROR);
    CHECK(master_append_cmd(&m, NULL, 1) == ERROR);
    CHECK(master_append_cmd(&m, tmp, 0) == ERROR);

    CHECK(master_append_cmd(&m, (uint8_t*)"hello", 5) == 5);
    CHECK(m.master_offset == 5);
    CHECK(m.buf->end == 5);

    // 写入超过 16KB：环形覆盖，start 必须前推，窗口恒为 16KB
    uint8_t *big = (uint8_t*)malloc(20480);
    memset(big, 0x41, 20480);
    int w = master_append_cmd(&m, big, 20480);
    CHECK(w == REPL_BACKLOG_SIZE);          // 超容量截断为容量大小
    CHECK(m.master_offset == 5 + 20480);
    CHECK(m.buf->end - m.buf->start == REPL_BACKLOG_SIZE);
    free(big);
    master_destory(&m);
}

// ---------------- S3 master_online_sync（环形读回 / 淘汰） ----------------
static void test_master_online_sync(void) {
    printf("== master_online_sync ==\n");
    uint8_t p1[128] = {0};
    int l1 = build_kv_cmd(p1, OP_SET, "online_k", "online_v");
    uint8_t buf[1024];

    MasterMgr m;
    master_init(&m);
    CHECK(master_online_sync(NULL, NULL, buf, 1024) == ERROR);
    CHECK(master_online_sync(&m, NULL, buf, 1024) == ERROR);

    master_append_cmd(&m, p1, l1);

    Slaveconn sc;
    memset(&sc, 0, sizeof(sc));
    sc.fd = 1001; sc.status = 3; sc.slave_offset = 0; sc.aof_fd = -1;
    uint64_t n = master_online_sync(&m, &sc, buf, sizeof(buf));
    CHECK(n == (uint64_t)l1);
    CHECK(memcmp(buf, p1, l1) == 0);        // 读回的数据和写入的一致
    CHECK(sc.slave_offset == (uint64_t)l1);
    CHECK(master_online_sync(&m, &sc, buf, sizeof(buf)) == OVER);  // 已追平

    // 掉线太久：积压区把它的偏移淘汰了 → status=4，由网络层摘除
    Slaveconn sc2;
    memset(&sc2, 0, sizeof(sc2));
    sc2.fd = 1002; sc2.status = 3; sc2.slave_offset = 0; sc2.aof_fd = -1;
    uint8_t *big = (uint8_t*)malloc(20480);
    memset(big, 0x42, 20480);
    master_append_cmd(&m, big, 20480);      // 把 offset 0 挤出缓冲区
    CHECK(master_online_sync(&m, &sc2, buf, sizeof(buf)) == OVER);
    CHECK(sc2.status == 4);
    free(big);
    master_destory(&m);
}

// ---------------- S4 master_handle_sync ----------------
static void test_master_handle_sync(void) {
    printf("== master_handle_sync ==\n");
    uint8_t buf[64] = {0};
    MasterMgr m;
    master_init(&m);
    CHECK(master_handle_sync(NULL, 1, buf, 20) == ERROR);
    CHECK(master_handle_sync(&m, -1, buf, 20) == ERROR);
    CHECK(master_handle_sync(&m, 1, NULL, 20) == ERROR);
    CHECK(master_handle_sync(&m, 1, buf, 0) == OVER);      // 没数据
    CHECK(master_handle_sync(&m, 1, buf, 10) == WAIT);     // 不足20字节帧头

    // 凭证不对的 PSYNC → 全量同步（status=1），并挂进链表
    build_psync(buf, (uint32_t)m.id + 1, 0, 20);
    CHECK(master_handle_sync(&m, 1002, buf, 20) == 20);
    Slaveconn *n1 = slave_find_by_fd(&m, 1002);
    CHECK(n1 != NULL);
    CHECK(n1->status == 1);
    CHECK(n1->slave_offset == 0);
    CHECK(m.slave_count == 1);

    // 凭证对 + 偏移在积压区内 → 增量补发（status=2）
    build_psync(buf, (uint32_t)m.id, 0, 20);
    CHECK(master_handle_sync(&m, 1003, buf, 20) == 20);
    Slaveconn *n2 = slave_find_by_fd(&m, 1003);
    CHECK(n2 != NULL);
    CHECK(n2->status == 2);

    // 凭证对 + 偏移超出积压区末端 → 降级全量（status=1）
    build_psync(buf, (uint32_t)m.id, 999999, 20);
    CHECK(master_handle_sync(&m, 1004, buf, 20) == 20);
    CHECK(slave_find_by_fd(&m, 1004)->status == 1);
    CHECK(m.slave_count == 3);

    // 不认识的命令：长度字段不可信 → ERROR；整包没到 → WAIT；到齐 → 按包长跳过
    build_psync(buf, 0, 0, 10);
    memcpy(buf, &(uint32_t){htonl(999)}, 4);
    CHECK(master_handle_sync(&m, 1, buf, 20) == ERROR);
    build_psync(buf, 0, 0, 30);
    memcpy(buf, &(uint32_t){htonl(999)}, 4);
    CHECK(master_handle_sync(&m, 1, buf, 10) == WAIT);
    CHECK(master_handle_sync(&m, 1, buf, 30) == 30);
    CHECK(m.slave_count == 3);               // 不认识的命令不产生节点

    master_destory(&m);
}

// ---------------- S5 master_full_sync ----------------
static void test_master_full_sync(void) {
    printf("== master_full_sync ==\n");
    uint8_t buf[1024];
    MasterMgr m;
    master_init(&m);
    Slaveconn sc;
    memset(&sc, 0, sizeof(sc));
    sc.fd = 1009; sc.aof_fd = -1;

    CHECK(master_full_sync(NULL, &sc, "x", buf, 1024) == ERROR);
    CHECK(master_full_sync(&m, NULL, "x", buf, 1024) == ERROR);
    CHECK(master_full_sync(&m, &sc, NULL, buf, 1024) == ERROR);
    CHECK(master_full_sync(&m, &sc, "x", NULL, 1024) == ERROR);
    CHECK(master_full_sync(&m, &sc, "x", buf, 0) == ERROR);

    // 准备 5000 字节的伪 AOF 文件
    uint8_t *content = (uint8_t*)malloc(5000);
    for (int i = 0; i < 5000; i++) content[i] = (uint8_t)(i * 7 + 3);
    int fd = open("src.aof", O_WRONLY | O_CREAT | O_TRUNC, 0664);
    write(fd, content, 5000);
    close(fd);

    // 第1档：FULLRESYNC 控制包 24 字节
    uint64_t n = master_full_sync(&m, &sc, "src.aof", buf, sizeof(buf));
    CHECK(n == 24);
    uint32_t cmd, rid; uint64_t off, moff;
    parse_fullresync(buf, &cmd, &rid, &off, &moff);
    CHECK(cmd == CMD_FULLRESYNC);
    CHECK(rid == (uint32_t)m.id);
    CHECK(off == 5000);                      // full_end = 文件实际大小
    CHECK(moff == 0);                        // master_offset 原样下发
    CHECK(sc.full_send == 1);
    CHECK(sc.full_end == 5000);
    CHECK(sc.aof_fd >= 0);
    CHECK(sc.slave_offset == 0);             // 逻辑偏移对齐全量起点
    CHECK(sc.file_offset == 0);

    // 第2档：分块读文件（buf 给 1000，逼出 5 次块读）
    uint8_t *collected = (uint8_t*)malloc(5000);
    uint64_t got = 0, guard = 0;
    while (sc.full_send != 0 && guard++ < 20) {
        n = master_full_sync(&m, &sc, "src.aof", buf, 1000);
        if (n == 4) break;                   // SYNC_END
        CHECK(n <= 1000);
        memcpy(collected + got, buf, n);
        got += n;
    }
    CHECK(got == 5000);
    CHECK(memcmp(collected, content, 5000) == 0);
    CHECK(sc.full_send == 0);
    CHECK(sc.status == 3);                   // 转增量在线
    CHECK(sc.aof_fd == -1);
    free(collected);
    free(content);

    // 空文件：FULLRESYNC(full_end=0) 之后下一次调用直接 SYNC_END
    fd = open("empty.aof", O_WRONLY | O_CREAT | O_TRUNC, 0664);
    close(fd);
    Slaveconn sc2;
    memset(&sc2, 0, sizeof(sc2));
    sc2.fd = 1010; sc2.aof_fd = -1;
    CHECK(master_full_sync(&m, &sc2, "empty.aof", buf, sizeof(buf)) == 24);
    parse_fullresync(buf, &cmd, &rid, &off, &moff);
    CHECK(off == 0);
    CHECK(master_full_sync(&m, &sc2, "empty.aof", buf, sizeof(buf)) == 4);
    CHECK(sc2.status == 3);

    unlink("src.aof");
    unlink("empty.aof");
    master_destory(&m);
}

// ---------------- S6 master_incremental_sync ----------------
static void test_master_incremental_sync(void) {
    printf("== master_incremental_sync ==\n");
    uint8_t p1[128] = {0}, p2[128] = {0};
    int l1 = build_kv_cmd(p1, OP_SET, "inc_k1", "v1");
    int l2 = build_kv_cmd(p2, OP_MOD, "inc_k1", "v2");
    uint8_t buf[1024];

    MasterMgr m;
    master_init(&m);
    CHECK(master_incremental_sync(NULL, NULL, buf, 1024) == ERROR);

    master_append_cmd(&m, p1, l1);
    master_append_cmd(&m, p2, l2);

    Slaveconn sc;
    memset(&sc, 0, sizeof(sc));
    sc.fd = 1011; sc.status = 2; sc.slave_offset = 0; sc.aof_fd = -1;

    // 第1档：CONTINUE 12 字节，携带补发截止偏移
    uint64_t n = master_incremental_sync(&m, &sc, buf, sizeof(buf));
    CHECK(n == 12);
    uint32_t cmd; memcpy(&cmd, buf, 4);
    CHECK(ntohl(cmd) == CMD_CONTINUE);
    CHECK(sc.incremental_send == 1);

    // 第2档：数据补发直到追平
    uint8_t *collected = (uint8_t*)malloc(l1 + l2);
    uint64_t got = 0, guard = 0;
    while (sc.incremental_send != 0 && guard++ < 10) {
        n = master_incremental_sync(&m, &sc, buf, sizeof(buf));
        if (n == 4) break;                   // SYNC_END
        memcpy(collected + got, buf, n);
        got += n;
    }
    CHECK(got == (uint64_t)(l1 + l2));
    CHECK(memcmp(collected, p1, l1) == 0);
    CHECK(memcmp(collected + l1, p2, l2) == 0);
    free(collected);
    CHECK(sc.incremental_send == 0);
    CHECK(sc.status == 3);
    CHECK(sc.slave_offset == m.master_offset);

    // 补发中途数据被积压区淘汰：当前实现是静默收尾（丢数据！见问题清单 Q5）
    MasterMgr m2;
    master_init(&m2);
    master_append_cmd(&m2, p1, l1);
    uint8_t *big = (uint8_t*)malloc(20480);
    memset(big, 0x43, 20480);
    master_append_cmd(&m2, big, 20480);      // 把 offset 0 挤出去
    Slaveconn sc2;
    memset(&sc2, 0, sizeof(sc2));
    sc2.fd = 1012; sc2.status = 2; sc2.slave_offset = 0; sc2.aof_fd = -1;
    CHECK(master_incremental_sync(&m2, &sc2, buf, sizeof(buf)) == 12);
    CHECK(master_incremental_sync(&m2, &sc2, buf, sizeof(buf)) == 4);  // 读不到数据→直接END
    CHECK(sc2.status == 3);                  // 【现状】从节点会误以为追平
    free(big);
    master_destory(&m2);
    master_destory(&m);
}

// ---------------- S7 slave_send_sync ----------------
static void test_slave_send_sync(void) {
    printf("== slave_send_sync ==\n");
    Slave s;
    memset(&s, 0, sizeof(s));
    s.master_fd = 3;
    uint8_t buf[20];
    CHECK(slave_send_sync(NULL, (char*)buf, 20) == ERROR);
    s.master_fd = -1;
    CHECK(slave_send_sync(&s, (char*)buf, 20) == ERROR);
    s.master_fd = 3;
    CHECK(slave_send_sync(&s, (char*)buf, 0) == OVER);

    s.master_id = 0x1234;
    s.slave_offset = 0x1122334455667788ULL;
    CHECK(slave_send_sync(&s, (char*)buf, sizeof(buf)) == 20);
    uint32_t cmd, rid, total; uint64_t off;
    memcpy(&cmd, buf, 4); memcpy(&rid, buf + 4, 4);
    memcpy(&off, buf + 8, 8); memcpy(&total, buf + 16, 4);
    CHECK(ntohl(cmd) == CMD_PSYNC);
    CHECK(ntohl(rid) == 0x1234);
    CHECK(((uint64_t)ntohl((uint32_t)(off >> 32)) | ((uint64_t)ntohl((uint32_t)off) << 32)) == 0x1122334455667788ULL);
    CHECK(ntohl(total) == 20);
}

// ---------------- S8 slave_init / slave_destory ----------------
static void test_slave_init_destory(void) {
    printf("== slave_init / slave_destory ==\n");
    CHECK(slave_init(NULL) == -1);
    unlink("repl_stat");

    Slave s;
    CHECK(slave_init(&s) == 0);
    CHECK(s.master_fd == -1);
    CHECK(s.master_id == 0);                 // 无持久化文件 → 凭证为空
    CHECK(s.slave_offset == 0);
    CHECK(s.status == 0);

    // 写一份 repl_stat 再读回
    FILE *fp = fopen("repl_stat", "wb");
    uint32_t id = 77; uint64_t off = 888;
    fwrite(&id, 4, 1, fp);
    fwrite(&off, 8, 1, fp);
    fclose(fp);
    CHECK(slave_init(&s) == 0);
    CHECK(s.master_id == 77);
    CHECK(s.slave_offset == 888);

    slave_destory(NULL);                     // 不应崩
    s.master_fd = 1013;
    slave_destory(&s);
    CHECK(s.master_fd == -1);
    CHECK(s.master_id == 0);
    CHECK(s.slave_offset == 0);
    CHECK(s.status == 0);
    unlink("repl_stat");
}

// ---------------- S9 slave_process ----------------
static void test_slave_process(void) {
    printf("== slave_process ==\n");
    uint8_t buf[256] = {0};
    CHECK(slave_process(NULL, buf, 4) == ERROR);
    Slave s;
    memset(&s, 0, sizeof(s));

    // 增量在线：SYNC_END 只是补发收尾标记，识别后吃掉
    s.status = 3;
    uint32_t end = htonl(CMD_SYNC_END);
    memcpy(buf, &end, 4);
    CHECK(slave_process(&s, buf, 4) == 4);
    CHECK(s.status == 3);

    // 增量在线：正常写命令包 → 执行 + 推进偏移 + 落盘（aof 已在 main 里 init 成 unit_aof.db）
    uint8_t p1[128] = {0};
    int l1 = build_kv_cmd(p1, OP_SET, "unit_k", "unit_v");
    CHECK(slave_process(&s, p1, l1) == (uint64_t)l1);
    CHECK(s.slave_offset == (uint64_t)l1);
    CHECK(kv_get((char*)"unit_k") != NULL && strcmp(kv_get((char*)"unit_k"), "unit_v") == 0);
    struct stat st;
    CHECK(stat("unit_aof.db", &st) == 0);
    CHECK(st.st_size == l1);                 // 增量命令也写进了本地 AOF

    // 全量：FULLRESYNC 控制包被拆包 → 不足24字节返回 WAIT(-1 的 uint64 形态)
    s.status = 0; s.slave_offset = 0;
    uint8_t fr[24] = {0};
    uint32_t cmd = htonl(CMD_FULLRESYNC), rid = htonl(55);
    uint64_t off = 10, moff = 0;
    memcpy(fr, &cmd, 4); memcpy(fr + 4, &rid, 4);
    uint64_t no = ((uint64_t)htonl((uint32_t)(off >> 32)) | ((uint64_t)htonl((uint32_t)off) << 32));
    memcpy(fr + 8, &no, 8);
    uint64_t nm = ((uint64_t)htonl((uint32_t)(moff >> 32)) | ((uint64_t)htonl((uint32_t)moff) << 32));
    memcpy(fr + 16, &nm, 8);
    CHECK(slave_process(&s, fr, 10) == (uint64_t)WAIT);
    CHECK(slave_process(&s, fr, 24) == 24);
    CHECK(s.status == 1);
    CHECK(s.full_end == 10);
    CHECK(s.master_id == 55);
    CHECK(s.tmp_fp != NULL);

    // 全量：文件数据写入；写满后 status 归 0 等 SYNC_END
    uint8_t fdata[10] = {1,2,3,4,5,6,7,8,9,10};
    CHECK(slave_process(&s, fdata, 10) == 10);
    CHECK(s.file_offset == 10);
    CHECK(s.status == 0);

    // 临时文件被篡改 → SYNC_END 校验失败：不覆盖正式文件，状态归零
    // 注意：tmp_fp 是全缓冲的，10字节文件数据要等 SYNC_END 里的 fclose 才落盘，
    //       所以垃圾必须写 100 字节——fclose 只刷 10 字节到 0~9，10~99 盖不掉，文件必然超长
    FILE *fp2 = fopen("aof_db_tmp", "ab");
    for (int i = 0; i < 100; i++) fputc(0xEE, fp2);
    fclose(fp2);
    memcpy(buf, &end, 4);
    CHECK(slave_process(&s, buf, 4) == OVER);
    CHECK(s.status == 0);
    CHECK(access("aof_db_tmp", F_OK) != 0);  // 临时文件被删除
    CHECK(access("aof_db", F_OK) != 0);      // 正式文件没被覆盖（不存在就是不覆盖）

    // 空闲态收到不认识的命令：当前实现返回 0（占位，见问题清单 Q9）
    s.status = 0;
    uint32_t junk = htonl(0xDEADBEEF);
    memcpy(buf, &junk, 4);
    CHECK(slave_process(&s, buf, 20) == 0);
}

// ---------------- S10 链表操作 ----------------
static void test_slave_list(void) {
    printf("== slave_find_by_fd / slave_remove_by_fd ==\n");
    uint8_t buf[64];
    MasterMgr m;
    master_init(&m);
    for (int i = 0; i < 3; i++) {
        build_psync(buf, (uint32_t)m.id + 1, 0, 20);   // 凭证不对→全量，反正只是建节点
        CHECK(master_handle_sync(&m, 1101 + i, buf, 20) == 20);
    }
    CHECK(m.slave_count == 3);
    CHECK(slave_find_by_fd(&m, 1101) != NULL);
    CHECK(slave_find_by_fd(&m, 1102) != NULL);
    CHECK(slave_find_by_fd(&m, 1103) != NULL);
    CHECK(slave_find_by_fd(&m, 9999) == NULL);
    // 注意：slave_find_by_fd(NULL,...) 当前实现无 NULL 守卫会崩，见问题清单 Q10，这里不触发

    slave_remove_by_fd(&m, 1102);                      // 删中间
    CHECK(m.slave_count == 2);
    CHECK(slave_find_by_fd(&m, 1102) == NULL);
    slave_remove_by_fd(&m, 1101);                      // 删第一个真实节点
    CHECK(m.slave_count == 1);
    slave_remove_by_fd(&m, 9999);                      // 删不存在的：无操作
    CHECK(m.slave_count == 1);
    // 注意：slave_remove_by_fd(NULL,...) 同样无 NULL 守卫，见问题清单 Q10
    master_destory(&m);
}

// ---------------- S11 端到端：socketpair 回放 全量→补发→在线 ----------------
static void test_end_to_end(void) {
    printf("== 端到端：socketpair 全量→增量补发→增量在线 ==\n");
    // 主节点侧数据源：一条 SET 命令包
    uint8_t aof_buf[128] = {0};
    size_t aof_len = build_kv_cmd(aof_buf, OP_SET, "fs_key", "fs_val");
    FILE *fp = fopen("e2e_src.aof", "wb");
    fwrite(aof_buf, 1, aof_len, fp);
    fclose(fp);

    CHECK(master_init(&mgr) == 0);
    unlink("repl_stat");
    CHECK(slave_init(&slave) == 0);

    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    int master_fd = sv[0], slave_fd = sv[1];
    slave.master_fd = slave_fd;

    // PSYNC（凭证空→全量）
    uint8_t req_buf[20];
    CHECK(slave_send_sync(&slave, (char*)req_buf, sizeof(req_buf)) == 20);
    write(slave_fd, req_buf, 20);
    uint8_t hbuf[32];
    CHECK(read_full(master_fd, hbuf, 20) == 20);
    CHECK(master_handle_sync(&mgr, master_fd, hbuf, 20) == 20);
    Slaveconn *conn = slave_find_by_fd(&mgr, master_fd);
    CHECK(conn != NULL);
    CHECK(conn->status == 1);

    // 主节点推全量
    uint8_t sbuf[1024];
    uint64_t total_sent = 0, guard = 0;
    while (conn->status != 3 && guard++ < 50) {
        uint64_t n = master_full_sync(&mgr, conn, "e2e_src.aof", sbuf, sizeof(sbuf));
        if (n > 0) { write(master_fd, sbuf, n); total_sent += n; }
    }
    CHECK(total_sent == 24 + aof_len + 4);

    // 从节点收全量：24B控制包 → aof_len文件数据 → 4B SYNC_END
    uint8_t rbuf[1024];
    CHECK(read_full(slave_fd, rbuf, 24) == 24);
    CHECK(slave_process(&slave, rbuf, 24) == 24);
    CHECK(slave.status == 1);
    CHECK(slave.full_end == aof_len);
    CHECK(read_full(slave_fd, rbuf, aof_len) == (ssize_t)aof_len);
    CHECK(slave_process(&slave, rbuf, aof_len) == (uint64_t)aof_len);
    CHECK(slave.status == 0);
    CHECK(read_full(slave_fd, rbuf, 4) == 4);
    CHECK(slave_process(&slave, rbuf, 4) == 4);
    CHECK(slave.status == 3);
    CHECK(slave.slave_offset == 0);

    // 全量落盘一致性 + 引擎加载 + 凭证/偏移持久化
    FILE *check = fopen("aof_db", "rb");
    CHECK(check != NULL);
    uint8_t cbuf[256] = {0};
    size_t rd = fread(cbuf, 1, sizeof(cbuf), check);
    fclose(check);
    CHECK(rd == aof_len);
    CHECK(memcmp(cbuf, aof_buf, aof_len) == 0);
    CHECK(kv_get((char*)"fs_key") != NULL && strcmp(kv_get((char*)"fs_key"), "fs_val") == 0);
    FILE *rs = fopen("repl_stat", "rb");
    CHECK(rs != NULL);
    uint32_t fid; uint64_t foff;
    CHECK(fread(&fid, 4, 1, rs) == 1);
    CHECK(fread(&foff, 8, 1, rs) == 1);
    fclose(rs);
    CHECK(fid == (uint32_t)mgr.id);
    CHECK(foff == 0);

    // 增量补发：主节点写一条 SET，从节点重新 PSYNC
    aof_close();          // 关掉 unit_aof.db 的句柄（aof_init 幂等，不关的话不会切换文件）
    aof_init("aof_db");   // 从节点 AOF 句柄指向重命名后的正式文件
    uint8_t set_buf[128] = {0};
    size_t set_len = build_kv_cmd(set_buf, OP_SET, "k", "v1");
    master_append_cmd(&mgr, set_buf, set_len);
    CHECK(mgr.master_offset == set_len);
    slave.status = 0; slave.file_offset = 0;
    CHECK(slave_send_sync(&slave, (char*)req_buf, sizeof(req_buf)) == 20);
    write(slave_fd, req_buf, 20);
    CHECK(read_full(master_fd, hbuf, 20) == 20);
    CHECK(master_handle_sync(&mgr, master_fd, hbuf, 20) == 20);
    Slaveconn *conn2 = slave_find_by_fd(&mgr, master_fd);
    CHECK(conn2 != NULL);
    CHECK(conn2->status == 2);               // 凭证对+偏移在缓冲区内 → 补发

    guard = 0;
    while (conn2->status != 3 && guard++ < 50) {
        uint64_t n = master_incremental_sync(&mgr, conn2, sbuf, sizeof(sbuf));
        if (n > 0) write(master_fd, sbuf, n);
    }
    CHECK(conn2->incremental_send == 0);

    CHECK(read_full(slave_fd, rbuf, 12) == 12);
    CHECK(slave_process(&slave, rbuf, 12) == 12);
    CHECK(slave.status == 2);
    CHECK(slave.master_offset == set_len);
    CHECK(read_full(slave_fd, rbuf, set_len) == (ssize_t)set_len);
    CHECK(slave_process(&slave, rbuf, set_len) == set_len);
    CHECK(slave.status == 3);
    CHECK(slave.slave_offset == set_len);
    CHECK(kv_get((char*)"k") != NULL && strcmp(kv_get((char*)"k"), "v1") == 0);
    CHECK(read_full(slave_fd, rbuf, 4) == 4);
    CHECK(slave_process(&slave, rbuf, 4) == 4);   // 补发收尾 SYNC_END 吃掉
    CHECK(slave.status == 3);

    // 增量在线：无握手无收尾，直接转发
    uint8_t mod_buf[128] = {0};
    size_t mod_len = build_kv_cmd(mod_buf, OP_MOD, "k", "v2");
    master_append_cmd(&mgr, mod_buf, mod_len);
    uint64_t on = master_online_sync(&mgr, conn2, sbuf, sizeof(sbuf));
    CHECK(on == mod_len);
    write(master_fd, sbuf, on);
    CHECK(master_online_sync(&mgr, conn2, sbuf, sizeof(sbuf)) == 0);
    CHECK(read_full(slave_fd, rbuf, mod_len) == (ssize_t)mod_len);
    CHECK(slave_process(&slave, rbuf, mod_len) == mod_len);
    CHECK(slave.slave_offset == set_len + mod_len);
    CHECK(slave.slave_offset == mgr.master_offset);
    CHECK(kv_get((char*)"k") != NULL && strcmp(kv_get((char*)"k"), "v2") == 0);

    // 从节点 AOF 应包含：全量包 + 补发 SET + 在线 MOD
    struct stat st;
    CHECK(stat("aof_db", &st) == 0);
    CHECK(st.st_size == (off_t)(aof_len + set_len + mod_len));

    close(slave_fd);   // master_fd 由 master_destory 关闭
    master_destory(&mgr);
    slave_destory(&slave);
    aof_close();
    unlink("e2e_src.aof");
}

int main(void) {
    // 全部文件操作限定在临时目录
    char dir[] = "/tmp/sync_test_XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    if (chdir(dir) != 0) { perror("chdir"); return 1; }

    init_kvengine();
    CHECK(aof_init("unit_aof.db") == 0);   // 从节点增量落盘的目标

    test_master_init_destory();
    test_master_append_cmd();
    test_master_online_sync();
    test_master_handle_sync();
    test_master_full_sync();
    test_master_incremental_sync();
    test_slave_send_sync();
    test_slave_init_destory();
    test_slave_process();
    test_slave_list();
    test_end_to_end();

    dest_kvengine();
    aof_close();

    // 清理临时目录
    unlink("unit_aof.db");
    unlink("aof_db");
    unlink("aof_db_tmp");
    unlink("repl_stat");
    if (chdir("..") == 0) rmdir(dir);

    printf("========== 总计: %d 项检查, %d 项失败 ==========\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
