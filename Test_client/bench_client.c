/*
 * Test_client/bench_client.c
 *
 * 批量压测客户端（只测吞吐，不做功能校验）
 *
 *  1. 一次 send() 发出 batch 个协议帧，消除“每包一次系统调用”的客户端开销；
 *  2. 收包严格按 9 字节头里的 length 字段切帧，不再假设回包固定长度
 *     （SET 首次回 "OK" 是 24 字节，重复 SET 回 "EXIST" 是 27 字节，
 *      一旦按固定长度凑字节，压测会少读几个字节然后卡死）；
 *  3. 输出每线程 QPS、每请求墙钟 µs、每请求 CPU µs，
 *     用来看究竟是“客户端 CPU 打满”还是“在等网络”。
 *
 * 编译（本目录已自带 kvproto.c，不需要链接 Tool/mempool/log）：
 *   gcc -O2 -g -Wall -o bench_client bench_client.c kvproto.c -lpthread
 *
 * 用法：
 *   ./bench_client <ip> <port> [线程数] [每线程请求数] [每批帧数]
 *   ./bench_client 192.168.149.128 2000 2 1000000 2000
 *
 * 说明：
 *  - 只对同一个 key 反复 SET：默认引擎是数组，最多 128 个 key，
 *    用不重复的 key 会触发 "ERROR" 回包，把回包长度搅乱；
 *  - batch 不要调得太大：回包总字节 ≈ batch × 27，要小于客户端接收缓冲区
 *    （默认约 128KB），否则服务端回包发不出去、客户端也不收，双方互相阻塞。
 *    batch=2000 时约 54KB，安全。
 *  - 服务端 rbuffer 现在是 1024 字节，一次 recv 只吃约 26 帧，这是正常的；
 *    批量带来的收益主要在客户端：100 万次 send 变成 500 次。
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "kvproto.h"

#define BENCH_KEY        "bench_key"
#define BENCH_VALUE      "bench_value"
#define RCV_BUF_SIZE     (1 * 1024 * 1024)   /* 客户端收包缓冲 */
#define RECV_TIMEOUT_S   10                  /* 防止服务端异常时永久阻塞 */
#define BATCH_MAX_FRAMES 4000                /* 回包 4000*27≈105KB，接近默认 rcvbuf */

static char          *g_ip;
static unsigned short g_port;

typedef struct {
    int            id;
    long           total;      /* 本线程要发的请求数 */
    long           batch;      /* 每批帧数 */
    const uint8_t *frame;      /* 单个请求帧 */
    int            frame_len;

    long long      wall_ns;    /* 墙钟耗时 */
    long long      cpu_ns;     /* 本线程 CPU 耗时 */
    long           resp_cnt;   /* 正确切出来的回包数 */
    long           bad_frame;  /* 魔数/长度异常的回包数 */
    int            err;        /* 0 正常，见 err_str() */
} worker_t;

static const char *err_str(int err)
{
    switch (err) {
    case 0:  return "ok";
    case 1:  return "socket/connect/malloc failed";
    case 2:  return "send failed";
    case 3:  return "recv failed (timeout or disconnect)";
    default: return "unknown";
    }
}

static long long mono_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static long long thread_cpu_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* 用协议的编码器造一个 SET 帧，保证和服务端完全一致 */
static int build_set_frame(uint8_t *out, size_t cap)
{
    char     valbuf[128];
    uint8_t *keys[1]  = { (uint8_t *)BENCH_KEY };
    uint8_t *vals[1]  = { (uint8_t *)valbuf };
    uint32_t vlens[1] = { 0 };

    int vlen = (int)strlen(BENCH_VALUE);
    if (vlen + 1 > (int)sizeof(valbuf)) return -1;

    valbuf[0] = (char)TYPE_STRING;            /* 值的第一字节是类型 */
    memcpy(valbuf + 1, BENCH_VALUE, (size_t)vlen);
    vlens[0] = (uint32_t)(vlen + 1);          /* 类型 + 数据 */

    RES in;
    memset(&in, 0, sizeof(in));
    in.op        = "SET";
    in.key       = keys;
    in.value     = vals;
    in.value_len = vlens;
    in.num       = 1;

    KV_encode(out, (uint32_t)cap, &in);
    if (in.status == ERROR || in.cons_ == 0) {
        fprintf(stderr, "KV_encode failed: %s\n", (char *)in.error_info);
        return -1;
    }
    return (int)in.cons_;
}

/*
 * 检查 buf 里是否有一个完整回包。
 * 返回 >0 完整帧长度；0 数据不够（半包）；-1 坏帧。
 * 头部布局：magic(2) version(1) opcode(2) length(4) = 9 字节
 */
static int frame_size(const uint8_t *buf, int avail)
{
    if (avail < HEAD_LEN) return 0;

    uint16_t magic;
    memcpy(&magic, buf, 2);
    if (ntohs(magic) != KV_MAGIC) return -1;

    uint32_t len;
    memcpy(&len, buf + 5, 4);
    uint32_t total = (uint32_t)HEAD_LEN + ntohl(len);
    if (total > (uint32_t)avail) return 0;
    return (int)total;
}

static int connect_server(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(g_port);
    addr.sin_addr.s_addr = inet_addr(g_ip);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return -1;
    }
    return fd;
}

static void *worker(void *arg)
{
    worker_t *w = (worker_t *)arg;
    uint8_t  *sbuf = NULL;
    uint8_t  *rbuf = NULL;
    int       fd   = -1;
    int       rlen = 0;

    fd = connect_server();
    if (fd < 0) {
        w->err = 1;
        return NULL;
    }

    struct timeval tv = { RECV_TIMEOUT_S, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sbuf = (uint8_t *)malloc((size_t)w->batch * (size_t)w->frame_len);
    rbuf = (uint8_t *)malloc(RCV_BUF_SIZE);
    if (!sbuf || !rbuf) {
        w->err = 1;
        goto out;
    }

    long sent = 0;
    long need = 0;
    long got  = 0;

    long long t0 = mono_ns();
    long long c0 = thread_cpu_ns();

    while (sent < w->total) {
        long n = w->total - sent;
        if (n > w->batch) n = w->batch;

        for (long i = 0; i < n; i++) {
            memcpy(sbuf + (size_t)i * (size_t)w->frame_len,
                   w->frame, (size_t)w->frame_len);
        }

        long want = n * (long)w->frame_len;
        long off  = 0;
        while (off < want) {
            ssize_t k = send(fd, sbuf + off, (size_t)(want - off), 0);
            if (k <= 0) {
                w->err = 2;
                goto finish;
            }
            off += (long)k;
        }
        sent += n;
        need += n;

        /* 把本批的回包读齐，按头部 length 切帧 */
        while (got < need) {
            ssize_t k = recv(fd, rbuf + rlen, (size_t)(RCV_BUF_SIZE - rlen), 0);
            if (k <= 0) {
                w->err = 3;
                goto finish;
            }
            rlen += (int)k;

            int pos = 0;
            while (pos < rlen) {
                int fs = frame_size(rbuf + pos, rlen - pos);
                if (fs == 0) break;        /* 半包，等下一段 */
                if (fs < 0) {              /* 坏帧：本批数据作废 */
                    w->bad_frame++;
                    pos = rlen;
                    break;
                }
                pos += fs;
                got++;
            }
            if (pos > 0) {
                rlen -= pos;
                if (rlen > 0) memmove(rbuf, rbuf + pos, (size_t)rlen);
            }
        }
    }

finish:
    w->wall_ns  = mono_ns() - t0;
    w->cpu_ns   = thread_cpu_ns() - c0;
    w->resp_cnt = got;

out:
    free(sbuf);
    free(rbuf);
    if (fd >= 0) close(fd);
    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr,
                "usage: %s <ip> <port> [threads] [total_per_thread] [batch]\n"
                "  default: threads=2 total=1000000 batch=2000\n"
                "  ex   : %s 192.168.149.128 2000 2 1000000 2000\n",
                argv[0], argv[0]);
        return -1;
    }

    signal(SIGPIPE, SIG_IGN);

    g_ip   = argv[1];
    g_port = (unsigned short)atoi(argv[2]);

    long threads = (argc > 3) ? atol(argv[3]) : 2;
    long total   = (argc > 4) ? atol(argv[4]) : 1000000;
    long batch   = (argc > 5) ? atol(argv[5]) : 2000;

    if (threads < 1) threads = 1;
    if (total   < 1) total   = 1;
    if (batch   < 1) batch   = 1;
    if (batch > BATCH_MAX_FRAMES) batch = BATCH_MAX_FRAMES;

    uint8_t frame[1024];
    int frame_len = build_set_frame(frame, sizeof(frame));
    if (frame_len <= 0) {
        fprintf(stderr, "build request frame failed\n");
        return -1;
    }

    printf("[bench] %s:%u  threads=%ld  total/thread=%ld  batch=%ld  frame=%dB\n",
           g_ip, g_port, threads, total, batch, frame_len);
    printf("[bench] 每线程流量 ≈ %.1f MB，每批 %.1f KB，预计回包 ≈ %.1f KB/批\n",
           (double)(total * frame_len) / (1024.0 * 1024.0),
           (double)(batch * frame_len) / 1024.0,
           (double)(batch * 27) / 1024.0);

    pthread_t *tid = (pthread_t *)calloc((size_t)threads, sizeof(pthread_t));
    worker_t  *w   = (worker_t  *)calloc((size_t)threads, sizeof(worker_t));
    if (!tid || !w) {
        fprintf(stderr, "calloc failed\n");
        return -1;
    }

    long long wall0 = mono_ns();
    for (long i = 0; i < threads; i++) {
        w[i].id        = (int)i;
        w[i].total     = total;
        w[i].batch     = batch;
        w[i].frame     = frame;
        w[i].frame_len = frame_len;
        if (pthread_create(&tid[i], NULL, worker, &w[i]) != 0) {
            fprintf(stderr, "pthread_create failed at %ld\n", i);
            w[i].err = 1;
        }
    }

    long   req_total = 0;
    long   resp_total = 0;
    long   bad_total = 0;
    long long cpu_total = 0;
    int    fail_cnt = 0;

    for (long i = 0; i < threads; i++) {
        pthread_join(tid[i], NULL);
        req_total  += total;
        resp_total += w[i].resp_cnt;
        bad_total  += w[i].bad_frame;
        cpu_total  += w[i].cpu_ns;
        if (w[i].err) fail_cnt++;

        double qps = (w[i].wall_ns > 0)
                   ? (double)total * 1e9 / (double)w[i].wall_ns : 0.0;
        printf("[thread %ld] %-10s qps=%9.0f  wall=%7.0f us/req  cpu=%7.2f us/req  resp=%ld/%ld\n",
               i, err_str(w[i].err), qps,
               (double)w[i].wall_ns / 1000.0 / (double)total,
               (double)w[i].cpu_ns / 1000.0 / (double)total,
               w[i].resp_cnt, total);
    }

    long long wall_ns = mono_ns() - wall0;
    double    sec     = (double)wall_ns / 1e9;

    printf("\n[summary] threads=%ld  requests=%ld  responses=%ld%s\n",
           threads, req_total, resp_total,
           (resp_total < req_total) ? "  <-- 回包不足，结果不可信" : "");
    printf("[summary] wall=%.3f s  aggregate QPS=%.0f  cpu=%.2f us/req  bad_frame=%ld  failed_threads=%d\n",
           sec, (sec > 0) ? (double)req_total / sec : 0.0,
           (req_total > 0) ? (double)cpu_total / 1000.0 / (double)req_total : 0.0,
           bad_total, fail_cnt);

    free(tid);
    free(w);
    return (fail_cnt || resp_total < req_total) ? 1 : 0;
}
