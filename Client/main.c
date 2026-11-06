#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include "kvproto.h"

/* 服务端单帧上限（Core/kvstore.h 里 BUFFER_SIZE=1024） */
#define SERVER_FRAME_MAX 1024
/* 每个 SET_CHUNK 帧携带的数据字节数；帧总长约 27 + key + 512，能进 1024 */
#define UPLOAD_CHUNK     512
/* 客户端收帧缓冲 */
#define CLIENT_FRAME_MAX 4096

// =========================================================================
// 辅助功能：读取本地文件（支持图片和文本文章），使用标准 malloc
// =========================================================================
static int read_local_file(const char *file_path, uint8_t type, uint8_t** out_buf) {
    if (!file_path || !out_buf) return -2;

    // 【关键】必须用 "rb" 二进制模式
    FILE *fp = fopen(file_path, "rb");
    if (!fp) {
        printf("❌ 打开文件失败: %s\n", file_path);
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size <= 0) {
        printf("❌ 文件为空: %s\n", file_path);
        fclose(fp);
        return -1;
    }

    // 分配内存，预留 1 个字节给 type
    uint8_t *buf = (uint8_t *)malloc(size + 1);
    if (!buf) {
        printf("❌ 内存分配失败\n");
        fclose(fp);
        return -1;
    }

    buf[0] = type; // 写入 TYPE_IMAGE 或 TYPE_URL
    size_t bytes_read = fread(buf + 1, 1, size, fp);
    fclose(fp);

    *out_buf = buf;
    return (int)bytes_read;
}

// =========================================================================
// 辅助功能：解析并展示文章（URL 类型）
// =========================================================================
static void view_article(const uint8_t *data, uint32_t len) {
    if (!data || len == 0) return;
    printf("\n========== 文章内容开始 ==========\n");
    // 直接按二进制写入标准输出，不依赖 \0 截断
    fwrite(data, 1, len, stdout);
    printf("\n========== 文章内容结束 ==========\n\n");
}

/* ============================ 收发基础 ============================ */

static int send_all(int fd, const void *buf, int len) {
    const char *p = (const char *)buf;
    int off = 0;
    while (off < len) {
        int n = (int)send(fd, p + off, len - off, 0);
        if (n <= 0) return -1;
        off += n;
    }
    return 0;
}

static int recv_all(int fd, void *buf, int len) {
    char *p = (char *)buf;
    int off = 0;
    while (off < len) {
        int n = (int)recv(fd, p + off, len - off, 0);
        if (n <= 0) return -1;
        off += n;
    }
    return 0;
}

/* 读一整帧到 frame，返回整帧长度；-1 表示断开/超时/坏包 */
static int recv_frame(int fd, uint8_t *frame, int cap) {
    kv_header_t head;

    if (recv_all(fd, frame, HEAD_LEN) != 0) return -1;
    if (kv_header_decode(&head, frame, HEAD_LEN) != HEAD_LEN) return -1;

    uint32_t total = HEAD_LEN + head.length;
    if (total > (uint32_t)cap) return -1;
    if (total > (uint32_t)HEAD_LEN &&
        recv_all(fd, frame + HEAD_LEN, (int)(total - HEAD_LEN)) != 0) return -1;

    return (int)total;
}

/* 按业务类型展示/保存一个完整 value（data 不含首字节 type） */
static void show_value(uint8_t type, const uint8_t *data, uint32_t len) {
    if (type == TYPE_STRING) {
        if (len > 4096) {
            printf("✅ Result (字符串 %u 字节): %.120s...\n", len, (const char *)data);
        } else {
            printf("✅ Result: %.*s\n", (int)len, (const char *)data);
        }
    } else if (type == TYPE_IMGINE) {
        FILE *fp = fopen("recv_image.jpg", "wb");
        if (fp) {
            fwrite(data, 1, len, fp);
            fclose(fp);
            printf("✅ 收到图片 %u 字节，已保存 recv_image.jpg\n", len);
        } else {
            printf("❌ 保存图片失败\n");
        }
    } else if (type == TYPE_URL) {
        if (len > (4u * 1024 * 1024)) {   /* 大文件不往终端打印 */
            FILE *fp = fopen("recv_article.bin", "wb");
            if (fp) {
                fwrite(data, 1, len, fp);
                fclose(fp);
                printf("✅ 收到文章 %u 字节，已保存 recv_article.bin（太大，不打印）\n", len);
            } else {
                printf("❌ 保存文章失败\n");
            }
        } else {
            view_article(data, len);
        }
    } else {
        printf("ℹ️  类型 %u，%u 字节: %.80s\n", type, len, (const char *)data);
    }
}

/*
 * 收一条"逻辑响应"：
 *   - 普通帧：直接展示
 *   - TYPE_CHUNK 帧：按 [total][offset][data] 重组，收齐后展示
 * 返回 0 正常，-1 断开/超时/协议错。
 */
static int recv_response(int fd) {
    uint8_t frame[CLIENT_FRAME_MAX];
    uint8_t *big = NULL;
    uint32_t big_total = 0, big_got = 0;
    int done = 0;

    while (!done) {
        int flen = recv_frame(fd, frame, sizeof(frame));
        if (flen < 0) {
            printf("\n🔌 服务器断开或读取超时\n");
            free(big);
            return -1;
        }

        RES rsp;
        memset(&rsp, 0, sizeof(rsp));
        KV_decode(frame, (uint32_t)flen, &rsp);

        if (rsp.status == ERROR) {
            printf("❌ Decode Error: %s\n", (char *)rsp.error_info);
            kv_RES_t_destory(&rsp);
            free(big);
            return -1;
        }

        for (uint32_t i = 0; i < rsp.num && !done; i++) {
            uint8_t *v  = rsp.value[i];
            uint32_t vl = rsp.value_len[i];

            if (!v || vl == 0) { done = 1; break; }

            if (v[0] == TYPE_CHUNK && vl >= 9) {
                uint32_t total, offset, n;

                memcpy(&total,  v + 1, 4); total  = ntohl(total);
                memcpy(&offset, v + 5, 4); offset = ntohl(offset);
                n = vl - 9;

                if (total == 0 || total > (128u * 1024 * 1024)) {
                    printf("❌ 分片总长异常: %u\n", total);
                    done = 1;
                    break;
                }
                if (offset == 0) {
                    free(big);
                    big = (uint8_t *)malloc(total);
                    big_total = total;
                    big_got   = 0;
                }
                if (!big || offset != big_got || offset + n > big_total) {
                    printf("❌ 分片顺序异常 (offset=%u got=%u)\n", offset, big_got);
                    done = 1;
                    break;
                }
                memcpy(big + offset, v + 9, n);
                big_got += n;

                if (big_got == big_total) {
                    show_value(big[0], big + 1, big_total - 1);
                    free(big);
                    big = NULL;
                    done = 1;
                }
            } else {
                show_value(v[0], v + 1, vl - 1);
                done = 1;
            }
        }
        kv_RES_t_destory(&rsp);
    }

    free(big);
    return 0;
}

/*
 * 分片上传：把 value（[type][data]，共 total 字节）切成 UPLOAD_CHUNK 大小的片，
 * 每片单独编成一个 SET_CHUNK 帧发出去；中间片服务端不回包，最后一片才回 OK/EXIST/ERROR。
 */
static int set_chunked(int fd, const char *key, const uint8_t *value, uint32_t total) {
    uint8_t  frame[CLIENT_FRAME_MAX];
    uint8_t *chunk = (uint8_t *)malloc(1 + 8 + UPLOAD_CHUNK);
    uint32_t offset = 0;

    if (!chunk) return -1;

    while (offset < total) {
        uint32_t n = total - offset;
        if (n > UPLOAD_CHUNK) n = UPLOAD_CHUNK;

        chunk[0] = TYPE_CHUNK;
        uint32_t t = htonl(total);
        uint32_t o = htonl(offset);
        memcpy(chunk + 1, &t, 4);
        memcpy(chunk + 5, &o, 4);
        memcpy(chunk + 9, value + offset, n);

        char     *keys[1]  = { (char *)key };
        uint8_t  *vals[1]  = { chunk };
        uint32_t  vlens[1] = { 9 + n };          /* type + total + offset + data */

        RES req;
        memset(&req, 0, sizeof(req));
        req.op        = "SET_CHUNK";
        req.key       = (uint8_t **)keys;
        req.value     = vals;
        req.value_len = vlens;
        req.num       = 1;

        KV_encode(frame, sizeof(frame), &req);
        if (req.status == ERROR) {
            printf("❌ 分片编码失败: %s\n", (char *)req.error_info);
            free(chunk);
            return -1;
        }
        if (send_all(fd, frame, (int)req.cons_) != 0) {
            printf("❌ 分片发送失败\n");
            free(chunk);
            return -1;
        }
        offset += n;
    }

    free(chunk);
    return 0;
}

// =========================================================================
// 主函数
// =========================================================================
int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("用法: %s <IP> <PORT>\n", argv[0]);
        return -1;
    }

    // 1. 连接服务端
    int socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketfd < 0) {
        perror("socket");
        return -1;
    }

    int one = 1;
    setsockopt(socketfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    struct timeval tv_rcv = { 10, 0 };   /* 收超时：防止卡死 */
    struct timeval tv_snd = { 30, 0 };   /* 发超时：大文件上传别设太小 */
    setsockopt(socketfd, SOL_SOCKET, SO_RCVTIMEO, &tv_rcv, sizeof(tv_rcv));
    setsockopt(socketfd, SOL_SOCKET, SO_SNDTIMEO, &tv_snd, sizeof(tv_snd));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[2]));
    addr.sin_addr.s_addr = inet_addr(argv[1]);

    if (connect(socketfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect failed");
        close(socketfd);
        return -1;
    }
    printf("✅ Connected to %s:%s\n", argv[1], argv[2]);
    printf("指令格式:\n");
    printf("  - SET key string value\n");
    printf("  - SET key image /path/to/pic.jpg\n");
    printf("  - SET key url /path/to/article.txt\n");
    printf("  - GET key / DEL key / EXIST key\n");
    printf("  - 大 value（>约 1KB）自动走分片上传\n");
    printf("  - exit / quit\n\n");

    char line[1024];
    char wbuf[64*1024];

    // 2. 交互主循环
    while (1) {
        printf("KV> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) break;

        line[strcspn(line, "\n")] = '\0';
        if (strlen(line) == 0) continue;
        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) break;

        // 3. 解析输入
        char *op = strtok(line, " ");
        if (!op) continue;
        char *key      = strtok(NULL, " ");
        char *type_str = strtok(NULL, " ");
        char *val_str  = strtok(NULL, " ");

        RES res;
        memset(&res, 0, sizeof(RES));
        res.op = op;
        res.num = 1;

        // 声明这些变量在外部，保证生命周期存活到 KV_encode 结束
        uint8_t *dyn_value_buf = NULL;
        char *keys_arr[1] = {0};
        uint8_t *values_arr[1] = {0};
        uint32_t v_lens_arr[1] = {0};

        if (key) {
            keys_arr[0] = key;
            res.key = (uint8_t **)keys_arr;
        }

        // 处理带有 Value 的命令 (SET / MOD)
        if ((strcmp(op, "SET") == 0 || strcmp(op, "MOD") == 0)) {
            if (!key || !type_str) {
                printf("❌ 参数不足: 需要输入 key 和 type\n");
                continue;
            }

            if (strcmp(type_str, "string") == 0) {
                if (!val_str) { printf("❌ 缺少字符串内容\n"); continue; }
                dyn_value_buf = (uint8_t *)malloc(strlen(val_str) + 2);
                dyn_value_buf[0] = TYPE_STRING;
                strcpy((char *)dyn_value_buf + 1, val_str);
                v_lens_arr[0] = strlen(val_str) + 2; // type + 字符串 + \0
                printf("发送的值的长度:%u \n", v_lens_arr[0]);
            }
            else if (strcmp(type_str, "image") == 0) {
                if (!val_str) { printf("❌ 缺少图片路径\n"); continue; }
                int ret = read_local_file(val_str, TYPE_IMGINE, &dyn_value_buf);
                if (ret < 0) { continue; }
                v_lens_arr[0] = ret + 1; // 加上 type 字节
                printf("发送的值的长度:%u \n", v_lens_arr[0]);
            }
            else if (strcmp(type_str, "url") == 0) {
                if (!val_str) { printf("❌ 缺少文章路径\n"); continue; }
                int ret = read_local_file(val_str, TYPE_URL, &dyn_value_buf);
                if (ret < 0) { continue; }
                v_lens_arr[0] = ret + 1;
                printf("发送的值的长度:%u \n", v_lens_arr[0]);
            }
            else {
                printf("❌ 不支持的类型: %s\n", type_str);
                continue;
            }

            values_arr[0] = dyn_value_buf;
            res.value = values_arr;
            res.value_len = v_lens_arr;
        }

        /* ---- 4. 大 value 走分片上传（普通 SET 帧装不下时） ---- */
        if ((strcmp(op, "SET") == 0 || strcmp(op, "MOD") == 0) &&
            (uint32_t)(18 + strlen(key) + v_lens_arr[0]) > SERVER_FRAME_MAX) {

            if (strcmp(op, "SET") != 0) {
                printf("❌ MOD 暂不支持分片更新；请先 DEL 再 SET\n");
                free(dyn_value_buf);
                continue;
            }
            if (v_lens_arr[0] > (64u * 1024 * 1024)) {
                printf("❌ value 超过服务端 64MB 上限\n");
                free(dyn_value_buf);
                continue;
            }

            printf("📦 分片上传 %u 字节（每片 %u）...\n", v_lens_arr[0], UPLOAD_CHUNK);
            int rc = set_chunked(socketfd, key, dyn_value_buf, v_lens_arr[0]);
            free(dyn_value_buf);
            dyn_value_buf = NULL;

            if (rc != 0) {
                printf("🔌 上传失败，连接可能已断开\n");
                break;
            }
            if (recv_response(socketfd) != 0) break;
            continue;
        }

        /* ---- 5. 普通请求：编码发送 ---- */
        memset(wbuf, 0, sizeof(wbuf));
        KV_encode((uint8_t *)wbuf, sizeof(wbuf), &res);

        if (dyn_value_buf) free(dyn_value_buf);

        if (res.status == ERROR) {
            printf("❌ Encode Error: %s\n", (char *)res.error_info);
            continue;
        }
        if (send_all(socketfd, wbuf, (int)res.cons_) != 0) {
            printf("🔌 发送失败，连接可能已断开\n");
            break;
        }

        // 6. 接收响应（普通一帧，或分片流重组）
        if (recv_response(socketfd) != 0) break;
    }

    close(socketfd);
    printf("客户端已退出。\n");
    return 0;
}
