// QPS 压测程序：模拟多客户端并发请求 KV 服务器
// 用法：
//   ./bench <ip> <port> <线程数> <每线程请求数> [SET:GET比例，默认1:0]
//   ./bench 127.0.0.1 8888 4 100000        # 4线程×10万，纯SET
//   ./bench 127.0.0.1 8888 8 200000 1:1     # 8线程，SET/GET各一半
// 模型：每线程一条长连接，批量流水线——一次发 PIPELINE 条请求，
//       收齐本批全部响应再发下一批，消除RTT等待，测的是服务器真实吞吐。
// 组包直接用项目的 kv_header_encode/kv_tlv_append（编译时带上 ../protocol/kvproto.c），
// 保证客户端与服务端线格式永远一致。
#include "kvproto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <time.h>

// kvproto.o 引用的分配包装，压测里直接转发 malloc/free
void *kv_malloc(size_t size) { return malloc(size); }
void  kv_free(void *ptr)     { free(ptr); }

#define PIPELINE   100      // 每批流水线深度
#define VALUE_LEN  64       // value 固定长度
#define MAX_PKT    512      // 单条请求包上限（key短，够用）

typedef struct {
    const char *ip;
    int port;
    int tid;
    long ops;
    int set_ratio;      // SET 概率分子，分母=set+get
    int get_ratio;
    long ok;            // 收到的合法响应数
    long err;           // 坏包/断链数
    double start, end;  // 本线程起止时间（秒）
} bench_arg;

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// 构造一条请求包，返回包长
static int build_req(uint8_t *buf, uint16_t opcode, const char *key, const char *value) {
    uint32_t klen = (uint32_t)strlen(key) + 1;
    uint32_t vlen = (value ? strlen(value) : 0) + 1;
    kv_header_t h;
    h.magic = KV_MAGIC;
    h.version = KV_VERSION;
    h.msg_type = MSG_TYPE_REQ;
    h.opcode = opcode;
    h.length = 7 + klen + (value ? 7 + vlen : 0);  // 请求语义：length=纯payload
    int off = kv_header_encode(&h, buf, MAX_PKT);
    TLV tlv;
    tlv.tag = TAG_KEY; tlv.type = TYPE_STRING; tlv.length = klen; tlv.value = (uint8_t*)key;
    off += kv_tlv_append(buf + off, MAX_PKT - off, tlv);
    if (value) {
        tlv.tag = TAG_VALUE; tlv.type = TYPE_STRING; tlv.length = vlen; tlv.value = (uint8_t*)value;
        off += kv_tlv_append(buf + off, MAX_PKT - off, tlv);
    }
    return off;
}

static int write_all(int fd, const uint8_t *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, buf + sent, len - sent);
        if (n <= 0) return -1;
        sent += n;
    }
    return 0;
}

// 读满 len 字节
static int read_all(int fd, uint8_t *buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        ssize_t n = read(fd, buf + got, len - got);
        if (n <= 0) return -1;
        got += n;
    }
    return 0;
}

static void *bench_thread(void *argp) {
    bench_arg *a = (bench_arg*)argp;
    // 1. 建连
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { a->err = a->ops; return NULL; }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(a->port);
    if (inet_pton(AF_INET, a->ip, &addr.sin_addr) != 1
        || connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "[线程%d] 连接失败\n", a->tid);
        a->err = a->ops;
        close(fd);
        return NULL;
    }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));  // 关掉Nagle，小包立即发

    // 2. 预生成批量请求缓冲区（key带线程号和序号，避免线程间互踩）
    static __thread uint8_t batch_buf[PIPELINE * MAX_PKT];
    static __thread char key_buf[64];
    static __thread char val_buf[VALUE_LEN];
    memset(val_buf, 'V', VALUE_LEN - 1);

    uint8_t hdr[KV_HEADER_LEN];
    a->start = now_sec();
    for (long done = 0; done < a->ops; ) {
        int batch = (a->ops - done) < PIPELINE ? (int)(a->ops - done) : PIPELINE;
        // 组包：按 set:get 比例轮流决定每条是SET还是GET
        size_t buf_len = 0;
        for (int i = 0; i < batch; i++) {
            snprintf(key_buf, sizeof(key_buf), "bench_k%02d_%08ld", a->tid, done + i);
            int is_set = ((done + i) % (a->set_ratio + a->get_ratio)) < a->set_ratio;
            buf_len += build_req(batch_buf + buf_len,
                                 is_set ? OP_SET : OP_GET,
                                 key_buf, is_set ? val_buf : NULL);
        }
        if (write_all(fd, batch_buf, buf_len) != 0) {
            a->err += batch;
            break;
        }
        // 收响应：本批应收到 batch 条
        for (int r = 0; r < batch; r++) {
            if (read_all(fd, hdr, KV_HEADER_LEN) != 0) {
                a->err += batch - r;
                done = a->ops;  // 跳出外层
                goto out;
            }
            // 注意：kv_response 里 h.length = 整个响应包大小（含10字节头），
            //       和请求语义（length=纯payload）不同——按包长=h.length跳过
            uint32_t pkt_len;
            memcpy(&pkt_len, hdr + 6, 4);
            pkt_len = ntohl(pkt_len);
            uint16_t magic, opcode;
            memcpy(&magic, hdr, 2); magic = ntohs(magic);
            memcpy(&opcode, hdr + 4, 2); opcode = ntohs(opcode);
            if (magic != KV_MAGIC || opcode != OP_RES || pkt_len < KV_HEADER_LEN) {
                a->err++;
                goto out;
            }
            // 响应体不参与校验，读掉丢弃
            uint8_t sink[1024];
            uint32_t rest = pkt_len - KV_HEADER_LEN;
            while (rest > 0) {
                uint32_t chunk = rest > sizeof(sink) ? sizeof(sink) : rest;
                if (read_all(fd, sink, chunk) != 0) { a->err += batch - r; goto out; }
                rest -= chunk;
            }
            a->ok++;
        }
        done += batch;
    }
out:
    a->end = now_sec();
    close(fd);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr, "用法: %s <ip> <port> <线程数> <每线程请求数> [SET:GET比例]\n", argv[0]);
        fprintf(stderr, "例:  %s 127.0.0.1 8888 4 100000 1:1\n", argv[0]);
        return 1;
    }
    const char *ip = argv[1];
    int port = atoi(argv[2]);
    int threads = atoi(argv[3]);
    long per_thread = atol(argv[4]);
    int set_r = 1, get_r = 0;
    if (argc > 5 && sscanf(argv[5], "%d:%d", &set_r, &get_r) != 2) {
        fprintf(stderr, "比例格式应为 SET:GET，如 1:1\n");
        return 1;
    }
    if (threads <= 0 || per_thread <= 0 || set_r + get_r <= 0) {
        fprintf(stderr, "参数不合法\n");
        return 1;
    }

    pthread_t tids[256];
    bench_arg args[256];
    if (threads > 256) { fprintf(stderr, "线程数最多256\n"); return 1; }

    printf("压测目标 %s:%d | %d线程 × %ld请求 | SET:GET = %d:%d\n",
           ip, port, threads, per_thread, set_r, get_r);
    printf("...\n");

    double wall_start = now_sec();
    for (int i = 0; i < threads; i++) {
        args[i] = (bench_arg){ip, port, i, per_thread, set_r, get_r, 0, 0, 0, 0};
        if (pthread_create(&tids[i], NULL, bench_thread, &args[i]) != 0) {
            fprintf(stderr, "线程%d创建失败\n", i);
            return 1;
        }
    }
    long total_ok = 0, total_err = 0;
    double min_start = 1e30, max_end = 0;
    for (int i = 0; i < threads; i++) {
        pthread_join(tids[i], NULL);
        total_ok += args[i].ok;
        total_err += args[i].err;
        if (args[i].start < min_start) min_start = args[i].start;
        if (args[i].end > max_end) max_end = args[i].end;
        double t = args[i].end - args[i].start;
        printf("线程%-3d 完成 %-9ld 错误 %-6ld 耗时 %.3fs  QPS=%.0f\n",
               i, args[i].ok, args[i].err, t, t > 0 ? args[i].ok / t : 0);
    }
    double wall = max_end - min_start;
    printf("----------------------------------------------------------\n");
    printf("总请求 %ld | 成功 %ld | 错误 %ld | 墙钟 %.3fs\n",
           threads * per_thread, total_ok, total_err, wall);
    printf("整体 QPS = %.0f\n", wall > 0 ? total_ok / wall : 0);
    (void)wall_start;
    return total_err == 0 ? 0 : 2;
}
