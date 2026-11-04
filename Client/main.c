#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "kvproto.h"

// static char* type_[] = {"start", "string", "image", "url", "else"};

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
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[2]));
    addr.sin_addr.s_addr = inet_addr(argv[1]);

    if (connect(socketfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect failed");
        return -1;
    }
    printf("✅ Connected to %s:%s\n", argv[1], argv[2]);
    printf("指令格式:\n");
    printf("  - SET key string value\n");
    printf("  - SET key image /path/to/pic.jpg\n");
    printf("  - SET key url /path/to/article.txt\n");
    printf("  - GET key / DEL key / EXIST key\n");
    printf("  - exit / quit\n\n");

    char line[1024];
    char wbuf[64*1024];
    char rbuf[64*1024];

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
        char *key = strtok(NULL, " ");
        char *type_str = strtok(NULL, " ");
        char *val_str = strtok(NULL, " ");

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
                printf("发送的值的长度:%d \n",v_lens_arr[0]);
            } 
            else if (strcmp(type_str, "image") == 0) {
                if (!val_str) { printf("❌ 缺少图片路径\n"); continue; }
                int ret = read_local_file(val_str, TYPE_IMGINE, &dyn_value_buf);
                if (ret < 0) { continue; }
                v_lens_arr[0] = ret + 1; // 加上 type 字节
                printf("发送的值的长度:%d \n",v_lens_arr[0]);
            }
            else if (strcmp(type_str, "url") == 0) {
                if (!val_str) { printf("❌ 缺少文章路径\n"); continue; }
                int ret = read_local_file(val_str, TYPE_URL, &dyn_value_buf);
                if (ret < 0) { continue; }
                v_lens_arr[0] = ret + 1; 
                printf("发送的值的长度:%d \n",v_lens_arr[0]);
            } 
            else {
                printf("❌ 不支持的类型: %s\n", type_str);
                continue;
            }

            values_arr[0] = dyn_value_buf;
            res.value = values_arr;
            res.value_len = v_lens_arr;
           
        }

        // 4. 编码并发送
        memset(wbuf, 0, sizeof(wbuf));
        KV_encode((uint8_t *)wbuf, sizeof(wbuf), &res);
        
        // 释放动态分配的值内存，防止内存泄漏
        if (dyn_value_buf) free(dyn_value_buf);

        if (res.status == ERROR) {
            printf("❌ Encode Error: %s\n", res.error_info);
            continue;
        }
        printf("发送\n");
        if (send(socketfd, wbuf, res.cons_, 0) < 0) {
            printf("🔌 发送失败，连接可能已断开\n");
            break;
        }
        printf("发送成功\n");

        // 5. 接收响应
        int total_recv = 0;
        // 先收头
        while (total_recv < HEAD_LEN) {
            int n = recv(socketfd, rbuf + total_recv, sizeof(rbuf) - total_recv, 0);
            if (n <= 0) {
                printf("\n🔌 服务器断开了连接！\n");
                goto disconnect;
            }
            total_recv += n;
        }

        kv_header_t head;
        kv_header_decode(&head, (uint8_t *)rbuf, HEAD_LEN);
        uint32_t expected_len = head.length + HEAD_LEN;

        // 收包体
        while (total_recv < expected_len) {
            int n = recv(socketfd, rbuf + total_recv, sizeof(rbuf) - total_recv, 0);
            if (n <= 0) {
                printf("\n🔌 服务器断开了连接！\n");
                goto disconnect;
            }
            total_recv += n;
        }

        // 6. 解码并展示
        RES rsp;
        memset(&rsp, 0, sizeof(RES));
        KV_decode((uint8_t *)rbuf, total_recv, &rsp);

        if (rsp.status == ERROR) {
            printf("❌ Decode Error: %s\n", rsp.error_info);
        } else {
            for (int i = 0; i < rsp.num; i++) {
                if (rsp.value[i] && rsp.value_len[i] > 1) {
                    uint8_t type = rsp.value[i][0];
                    uint8_t *data = rsp.value[i] + 1;
                    uint32_t data_len = rsp.value_len[i] - 1;

                    if (type == TYPE_STRING) {
                        printf("✅ Result: %s\n", (char *)data);
                    } else if (type == TYPE_IMGINE) {
                        printf("✅ 收到图片数据 (大小: %u 字节)\n", data_len);
                        FILE *fp = fopen("recv_image.jpg", "wb");
                        if (fp) { 
                            fwrite(data, 1, data_len, fp); 
                            fclose(fp); 
                            printf("   已保存为 recv_image.jpg\n"); 
                        }
                    } else if (type == TYPE_URL) {
                        view_article(data, data_len);
                    }
                } else if (rsp.value[i]) {
                    // 跳过 type 字节打印普通消息（比如 NOT EXIST）
                    printf("ℹ️  %s\n", (char *)(rsp.value[i] + 1));
                }
            }
        }
        
        // 清理内存
        if (rsp.key) kv_RES_t_destory(&rsp); 
    }

disconnect:
    close(socketfd);
    printf("客户端已退出。\n");
    return 0;
}