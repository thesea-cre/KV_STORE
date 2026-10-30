#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "kvproto.h"

static char* type_[] = {"start", "string", "image", "url", "else"};

static void hex_dump(const void *data, uint32_t size, const char *title) {
    const uint8_t *buf = (const uint8_t *)data;
    if (title != NULL) printf("--- %s (总长度: %u 字节) ---\n", title, size);

    for (uint32_t i = 0; i < size; i += 16) {
        printf("%04x: ", i);
        for (uint32_t j = 0; j < 16; j++) {
            if (i + j < size) printf("%02X ", buf[i + j]);
            else printf("   ");
            if (j == 7) printf(" ");
        }
        printf(" |");
        for (uint32_t j = 0; j < 16; j++) {
            if (i + j >= size) break;
            uint8_t c = buf[i + j];
            printf("%c", (c >= 0x20 && c <= 0x7E) ? c : '.');
        }
        printf("|\n");
    }
}

// 封装一个安全的收发逻辑（处理粘包/半包）
static int send_and_recv(int sockfd, char *wbuf, uint32_t wlen, char *rbuf, uint32_t rbuf_size) {
    if (send(sockfd, wbuf, wlen, 0) < 0) {
        perror("send failed");
        return -1;
    }

    // 先接收 9 字节的包头
    int total_recv = 0;
    while (total_recv < HEAD_LEN) {
        int n = recv(sockfd, rbuf + total_recv, rbuf_size - total_recv, 0);
        if (n <= 0) return -1;
        total_recv += n;
    }

    // 从包头解析出包体长度（假设你的协议包头里包含 length 字段）
    // 注意：这里需要根据你的 KV_HEADER_LEN 结构来解，假设你的 kv_header_t 可以用 kv_header_decode 解析
    kv_header_t head;
    kv_header_decode(&head, (uint8_t *)rbuf, HEAD_LEN);
    uint32_t expected_len = head.length + HEAD_LEN;

    // 继续接收剩下的包体
    while (total_recv < expected_len) {
        int n = recv(sockfd, rbuf + total_recv, rbuf_size - total_recv, 0);
        if (n <= 0) return -1;
        total_recv += n;
    }
    return total_recv;
}

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
        perror("connect");
        return -1;
    }
    printf("Connected to %s:%s. 输入格式: [OP] [KEY] [TYPE] [VALUE] (例如: SET king string 123)\n", argv[1], argv[2]);

    char line[256];
    char wbuf[1024] = {0};
    char rbuf[1024] = {0};

    // 2. 交互主循环
    while (1) {
        printf("> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) break;
        line[strcspn(line, "\n")] = '\0'; // 去掉换行符

        if (strlen(line) == 0) continue;
        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) break;

        // 3. 解析输入 (用 strtok 切分)
        char *op = strtok(line, " ");
        char *key = strtok(NULL, " ");
        char *type_str = strtok(NULL, " ");
        char *val_str = strtok(NULL, " ");

        // 构造底层的 RES
        RES res;
        memset(&res, 0, sizeof(RES));
        res.op = op;

        // 对于 GET / DEL / EXIST，只需要 key
        if (key) {
            res.key = (uint8_t **)&key; // 指向栈上的指针
            res.num = 1;
        }

        // 对于 SET 等需要 value 的命令
        char value_buf[128] = {0};
        uint32_t v_len = 0;
        uint32_t v_len_arr[1] = {0};
        
        if (val_str && type_str) {
            // 查找 type 枚举
            uint8_t type_byte = 0;
            for (int j = KV_TYPE_START; j < KV_TYPE_COUNT; j++) {
                if (strcmp(type_str, type_[j]) == 0) {
                    type_byte = (uint8_t)j;
                    break;
                }
            }
            // 拼接 [type] + [value]
            value_buf[0] = type_byte;
            strncpy(value_buf + 1, val_str, sizeof(value_buf) - 2);
            v_len = strlen(val_str) + 1 + 1; // type + 字符串 + \0（看你的编码层需要）

            // 挂到 res 上
            char *v_ptr = value_buf;
            res.value = (uint8_t **)&v_ptr;
            v_len_arr[0] = v_len;
            res.value_len = v_len_arr;
            res.num = 1;
        }

        // 4. 编码并发送
        memset(wbuf, 0, sizeof(wbuf));
        KV_encode((uint8_t *)wbuf, sizeof(wbuf), &res);
        if (res.status == ERROR) {
            printf("Encode Error: %s\n", res.error_info);
            continue;
        }

        // 5. 接收并解码响应
        memset(rbuf, 0, sizeof(rbuf));
        int rlen = send_and_recv(socketfd, wbuf, res.cons_, rbuf, sizeof(rbuf));
        if (rlen <= 0) {
            printf("Connection closed by server.\n");
            break;
        }

        RES rsp;
        memset(&rsp, 0, sizeof(RES));
        KV_decode((uint8_t *)rbuf, rlen, &rsp);

        // 6. 打印响应
        if (rsp.status == ERROR) {
            printf("Decode Error: %s\n", rsp.error_info);
        } else {
            for (int i = 0; i < rsp.num; i++) {
                if (rsp.value[i]) {
                    printf("Result: %s\n", (char *)(rsp.value[i] + 1)); // 跳过第一字节的 type
                }
            }
        }

        // 清理内存（防止 decode 漏内存）
        if (rsp.key) kv_RES_t_destory(&rsp); 
    }

    close(socketfd);
    return 0;
}