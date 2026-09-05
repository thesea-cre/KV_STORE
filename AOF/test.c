#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "kvstore.h"
#include "aof.h"
#include "kv_array.h"

typedef struct tlv tlv_t;
#define TLV_KEY TYPE_STRING

/* 全局存储引擎实例 */
kv_array_t global_array;

/**
 * 辅助函数：构造SET命令完整协议包
 * 注：你的 h.length 当前只计算了TLV总长度，未包含10字节固定头部
 *     如果协议定义 length 为总包长，请自行加上 10
 */
static int build_set_pkt(uint8_t *buf, int buf_size, char *key, char *value) {
    kv_header_t h;
    h.magic = KV_MAGIC;
    h.version = KV_VERSION;
    h.msg_type = MSG_TYPE_REQ;
    h.opcode = OP_SET;

    int key_len = (int)strlen(key) + 1;
    int val_len = (int)strlen(value) + 1;
    h.length = (7 + key_len) + (7 + val_len);

    int offset = kv_header_encode(&h, buf, buf_size);
    if (offset <= 0) return -1;

    tlv_t key_tlv;
    key_tlv.type = TLV_KEY;
    key_tlv.length = key_len;
    key_tlv.value = (uint8_t *)key;  // 强转消除符号性警告
    int ret = kv_tlv_append(buf + offset, buf_size - offset, key_tlv);
    if (ret <= 0) return -1;
    offset += ret;

    tlv_t val_tlv;
    val_tlv.type = TLV_KEY;
    val_tlv.length = val_len;
    val_tlv.value = (uint8_t *)value; // 强转消除符号性警告
    ret = kv_tlv_append(buf + offset, buf_size - offset, val_tlv);
    if (ret <= 0) return -1;
    offset += ret;

    return offset;
}

/**
 * 辅助函数：构造DEL命令完整协议包
 */
static int build_del_pkt(uint8_t *buf, int buf_size, const char *key) {
    kv_header_t h;
    h.magic = KV_MAGIC;
    h.version = KV_VERSION;
    h.msg_type = MSG_TYPE_REQ;
    h.opcode = OP_DEL;

    int key_len = (int)strlen(key) + 1;
    h.length = (7 + key_len);

    int offset = kv_header_encode(&h, buf, buf_size);
    if (offset <= 0) return -1;

    tlv_t key_tlv;
    key_tlv.type = TLV_KEY;
    key_tlv.length = key_len;
    key_tlv.value = (uint8_t *)key; // 强转同时消除const警告
    int ret = kv_tlv_append(buf + offset, buf_size - offset, key_tlv);
    if (ret <= 0) return -1;
    offset += ret;

    return offset;
}

int main() {
    printf("===== AOF 持久化测试开始 =====\n");

    /* 1. 清理历史日志文件，保证测试环境干净 */
    unlink("aof_db");

    /* 2. 初始化KV存储引擎 */
    init_kvengine();
    printf("KV存储引擎初始化完成\n");

    /* ========== 第一阶段：写入数据并持久化 ========== */
    printf("\n--- 第一阶段：写入持久化 ---\n");
    if (aof_init("aof_db") < 0) {
        printf("AOF初始化失败，测试终止\n");
        return -1;
    }

    uint8_t pkt_buf[1024];
    int pkt_len;

    /* 写入 SET king 123：每个包单独构造、单独追加，避免覆盖 */
    pkt_len = build_set_pkt(pkt_buf, sizeof(pkt_buf), "king", "123");
    if (pkt_len > 0) {
        aof_append((char *)pkt_buf, pkt_len);
        printf("写入 SET king 123，包长 %d 字节\n", pkt_len);
    }

    /* 写入 SET test hello */
    pkt_len = build_set_pkt(pkt_buf, sizeof(pkt_buf), "test", "hello");
    if (pkt_len > 0) {
        aof_append((char *)pkt_buf, pkt_len);
        printf("写入 SET test hello，包长 %d 字节\n", pkt_len);
    }

    /* 写入 DEL king */
    pkt_len = build_del_pkt(pkt_buf, sizeof(pkt_buf), "king");
    if (pkt_len > 0) {
        aof_append((char *)pkt_buf, pkt_len);
        printf("写入 DEL king，包长 %d 字节\n", pkt_len);
    }

    /* 强制刷盘，确保数据落地 */
    aof_fsync();
    printf("所有数据已刷入磁盘\n");

    /* 3. 模拟服务宕机：关闭日志、销毁内存数据 */
    aof_close();
    dest_kvengine();
    printf("模拟服务退出，内存数据已清空\n");

    /* ========== 第二阶段：重启恢复与校验 ========== */
    printf("\n--- 第二阶段：重启恢复 ---\n");
    init_kvengine();

    int ret = aof_recover();
    if (ret < 0) {
        printf("AOF恢复失败，测试终止\n");
        dest_kvengine();
        return -1;
    }
    printf("AOF恢复执行完成\n");

    /* 4. 数据正确性校验 */
    printf("\n--- 数据校验 ---\n");

    char *value = kv_array_get(&global_array, "king");
    printf("key=king, value:%s\n", value == NULL ? "NULL(已删除)" : value);

    value = kv_array_get(&global_array, "test");
    printf("key=test, value:%s\n", value == NULL ? "NULL(不存在)" : value);

    /* 5. 资源清理 */
    dest_kvengine();
    printf("\n===== 测试结束 =====\n");
    return 0;
}
