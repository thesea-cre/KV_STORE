// #include<stdio.h>
// #include<sys/socket.h>
// #include<arpa/inet.h>
// #include<string.h>
// #include<stdlib.h>
// #include"kvproto.h"
// #include<sys/epoll.h>
// #include<errno.h>
// #include<unistd.h>
// extern const char *command[];
// #define BUFFER_SIZE 1024
// #define MAX_KV_COUNT 64  // 单次最多支持的键值对数量
// enum{
//     KV_CMD_START,
//     KV_CMD_SET=KV_CMD_START,
//     KV_CMD_GET,
//     KV_CMD_DEL,
//     KV_CMD_MOD,
//     KV_CMD_EXIST,
//     KV_CMD_COUNT
// };

// void *kv_malloc(size_t size){
//     return malloc(size);
// }
// void kv_free(void* ptr){
//     return free(ptr);
// }
// int init_server(char *ip,int port){
//     int socketfd=socket(AF_INET,SOCK_STREAM,0);
//     struct sockaddr_in addr;
//     memset(&addr,0,sizeof(struct sockaddr_in));
//     addr.sin_family=AF_INET;
//     addr.sin_port=htons(port);
//     addr.sin_addr.s_addr=inet_addr(ip);
//     int ret=connect(socketfd,(struct sockaddr*)&addr,sizeof(struct sockaddr));
//     if(ret<0){
//         close(socketfd);
//         return -1;
//     }
//     return socketfd;
// }
// /**
//  * @brief 读取一行输入，自动去除末尾换行
//  * @param buf 输出缓冲区
//  * @param size 缓冲区大小
//  * @return 读取到的字符串长度
//  */
// int read_line(char *buf, int size) {
//     memset(buf, 0, size);
//     if (fgets(buf, size, stdin) == NULL) {
//         return -1;
//     }
//     // 去掉末尾换行符
//     size_t len = strlen(buf);
//     if (len > 0 && buf[len-1] == '\n') {
//         buf[len-1] = '\0';
//         len--;
//     }
//     return (int)len;
// }

// /**
//  * @brief 批量组包函数，支持多key/多kv对
//  * @param opcode 命令码
//  * @param wbuffer 输出缓冲区
//  * @param keys key数组
//  * @param values value数组（一元命令传NULL即可）
//  * @param kv_num 键值对数量
//  * @return 组包总长度，失败返回-1
//  */
// int send_to(int opcode, char *wbuffer, char *keys[], char *values[], int kv_num){
//     if (wbuffer == NULL || keys == NULL || kv_num <= 0) return -1;

//     kv_header_t header;
//     header.magic = KV_MAGIC;
//     header.version = KV_VERSION;
//     header.msg_type = MSG_TYPE_REQ;
//     header.opcode = opcode;

//     int offset = 0;
//     int wlength = BUFFER_SIZE;
//     uint32_t total_len = 0; // 
//     int is_write_cmd = (opcode == KV_CMD_SET || opcode == KV_CMD_MOD);

//     // 第一步：计算总长度
//     if (is_write_cmd) {
//         if (values == NULL) return -1;
//         for (int i = 0; i < kv_num; i++) {
//             total_len += 7 + strlen(keys[i]) + 1;   // KEY-TLV头7字节 + 数据
//             total_len += 7 + strlen(values[i]) + 1; // VALUE-TLV头7字节 + 数据
//         }
//     } else {
//         for (int i = 0; i < kv_num; i++) {
//             total_len += 7 + strlen(keys[i]) + 1;
//         }
//     }
//     header.length = total_len;
//     printf("header.length:%d\n",header.length);

//     // 第二步：编码头部
//     offset = kv_header_encode(&header, wbuffer + offset, wlength - offset);
//     if (offset <= 0) return -1;

//     // 第三步：批量追加TLV
//     TLV tlv;
//     if (is_write_cmd) {
//         for (int i = 0; i < kv_num; i++) {
//             // 追加KEY
//             tlv.tag = TAG_KEY;
//             tlv.type = TYPE_STRING;
//             tlv.length = strlen(keys[i]) + 1;
//             tlv.value = keys[i];
//             int ret = kv_tlv_append(wbuffer + offset, wlength - offset, tlv);
//             if (ret <= 0) return -1;
//             offset += ret;

//             // 追加VALUE
//             tlv.tag = TAG_VALUE;
//             tlv.type = TYPE_STRING;
//             tlv.length = strlen(values[i]) + 1;
//             tlv.value = values[i];
//             ret = kv_tlv_append(wbuffer + offset, wlength - offset, tlv);
//             if (ret <= 0) return -1;
//             offset += ret;
//         }
//     } else {
//         for (int i = 0; i < kv_num; i++) {
//             tlv.tag = TAG_KEY;
//             tlv.type = TYPE_STRING;
//             tlv.length = strlen(keys[i]) + 1;
//             tlv.value = keys[i];
//             int ret = kv_tlv_append(wbuffer + offset, wlength - offset, tlv);
//             if (ret <= 0) return -1;
//             offset += ret;
//         }
//     }

//     return offset;
// }

// int parse_backage(char *buf){
//     kv_request_t req;
//     kv_request_t_init(&req);
//     kv_header_t header;
//     int ret=0;
//     int offset=0;
//     ret=kv_header_decode(buf,1024,&header);
//     offset+=ret;
//     ret=kv_payload_decode(buf+offset,1024-offset,&req);
//     offset+=ret;

//     printf("\n=== 服务端响应 ===\n");
//     for(int i=0;i<req.key_num;i++){
//         if(req.key[i]->data!=NULL){
//             printf("key[%d]: %s, len:%d\n", i, req.key[i]->data, req.key[i]->length);
//         }
//     }
//     for(int i=0;i<req.value_num;i++){
//         if(req.value[i]->data!=NULL){
//             printf("value[%d]: %s, len:%d\n", i, req.value[i]->data, req.value[i]->length);
//         }
//     }

//     kv_request_t_destory(&req);
//     return offset;
// }

// void hex_dump(const void *data, size_t size, const char *title) {
//     const uint8_t *buf = (const uint8_t *)data;
//     size_t i, j;

//     if (title != NULL) {
//         printf("--- %s (总长度: %zu 字节) ---\n", title, size);
//     }

//     for (i = 0; i < size; i += 16) {
//         printf("%04zx: ", i);
//         for (j = 0; j < 16; j++) {
//             if (i + j < size) {
//                 printf("%02X ", buf[i + j]);
//             } else {
//                 printf("   ");
//             }
//             if (j == 7) {
//                 printf(" ");
//             }
//         }
//         printf(" |");
//         for (j = 0; j < 16; j++) {
//             if (i + j >= size) break;
//             uint8_t c = buf[i + j];
//             if (c >= 0x20 && c <= 0x7E) {
//                 printf("%c", c);
//             } else {
//                 printf(".");
//             }
//         }
//         printf("|\n");
//     }
// }

// int main(int argc,char *argv[]){
//     if(argc<3){
//         printf("usage: %s <port>\n", argv[0]);
//         return -1;
//     }
//     int port=atoi(argv[2]);
//     char *ip=argv[1];
//     char buf[BUFFER_SIZE]={0};
//     int fd=init_server(ip,port);
//     if (fd < 0) {
//         perror("connect server failed");
//         return -1;
//     }

//     char line[BUFFER_SIZE] = {0};
//     char *keys[MAX_KV_COUNT] = {0};
//     char *values[MAX_KV_COUNT] = {0};
//     int kv_count = 0;

//     printf("KV客户端启动成功，支持命令：SET / GET / DEL / MOD / EXIST\n");
//     printf("输入规则：\n");
//     printf("  1. 先输入命令类型\n");
//     printf("  2. 按提示输入key，输入 00 结束输入\n");
//     printf("  3. SET/MOD 命令每个key对应输入一个value\n");
//     printf("  4. 全部输入完成后会预览键值对，确认后自动发送\n\n");

//     while(1){
//         // 重置计数和缓冲区
//         kv_count = 0;
//         for (int i = 0; i < MAX_KV_COUNT; i++) {
//             if (keys[i]) { free(keys[i]); keys[i] = NULL; }
//             if (values[i]) { free(values[i]); values[i] = NULL; }
//         }

//         printf("\n====================\n");
//         printf("输入命令：");
//         if (read_line(line, BUFFER_SIZE) <= 0) continue;

//         // 匹配命令码
//         int opcode = -1;
//         for(int i=0;i<KV_CMD_COUNT;i++){
//             if(strcmp(command[i], line) == 0){
//                 opcode = i;
//                 break;
//             }
//         }
//         if(opcode == -1){
//             printf("未知命令：%s，请重新输入\n", line);
//             continue;
//         }
//         int is_write_cmd = (opcode == KV_CMD_SET || opcode == KV_CMD_MOD);

//         // ========== 第一步：循环输入键值对 ==========
//         printf("\n--- 开始输入（输入key时输入 00 结束） ---\n");
//         while (kv_count < MAX_KV_COUNT) {
//             printf("\n输入key：");
//             if (read_line(line, BUFFER_SIZE) < 0) break;

//             // 输入00，结束输入
//             if (strcmp(line, "00") == 0) {
//                 break;
//             }

//             // 保存key
//             keys[kv_count] = strdup(line);
//             if (keys[kv_count] == NULL) {
//                 printf("内存分配失败\n");
//                 break;
//             }

//             // 二元命令：继续输入value
//             if (is_write_cmd) {
//                 printf("输入value：");
//                 if (read_line(line, BUFFER_SIZE) < 0) {
//                     free(keys[kv_count]);
//                     keys[kv_count] = NULL;
//                     break;
//                 }
//                 values[kv_count] = strdup(line);
//                 if (values[kv_count] == NULL) {
//                     printf("内存分配失败\n");
//                     free(keys[kv_count]);
//                     keys[kv_count] = NULL;
//                     break;
//                 }
//             }

//             kv_count++;
//         }

//         if (kv_count == 0) {
//             printf("未输入有效key，请重新操作\n");
//             continue;
//         }

//         // ========== 第二步：二元命令配对校验 + 补全 ==========
//         if (is_write_cmd) {
//             // 正常交替输入不会出现未配对，这里做兜底校验
//             int unpaired = 0;
//             for (int i = 0; i < kv_count; i++) {
//                 if (values[i] == NULL) {
//                     unpaired++;
//                 }
//             }

//             // 存在未配对的key，提示补全
//             while (unpaired > 0) {
//                 printf("\n⚠️  键值数量不匹配，存在 %d 个未配对的key：\n", unpaired);
                
//                 // 打印已配对
//                 printf("\n已配对的键值对：\n");
//                 int paired_idx = 0;
//                 for (int i = 0; i < kv_count; i++) {
//                     if (values[i] != NULL) {
//                         printf("  [%d] %s = %s\n", paired_idx++, keys[i], values[i]);
//                     }
//                 }

//                 // 打印未配对
//                 printf("\n未配对的key：\n");
//                 for (int i = 0; i < kv_count; i++) {
//                     if (values[i] == NULL) {
//                         printf("  → %s\n", keys[i]);
//                     }
//                 }

//                 // 逐个补全value
//                 printf("\n--- 开始补全value ---\n");
//                 for (int i = 0; i < kv_count; i++) {
//                     if (values[i] == NULL) {
//                         printf("为 key[%s] 输入value：", keys[i]);
//                         if (read_line(line, BUFFER_SIZE) > 0) {
//                             values[i] = strdup(line);
//                             unpaired--;
//                         }
//                     }
//                 }
//             }
//         }

//         // ========== 第三步：打印预览所有键值对 ==========
//         printf("\n====================\n");
//         printf("✅ 输入完成，本次提交的内容：\n");
//         if (is_write_cmd) {
//             for (int i = 0; i < kv_count; i++) {
//                 printf("  [%d] %s = %s\n", i, keys[i], values[i]);
//             }
//         } else {
//             for (int i = 0; i < kv_count; i++) {
//                 printf("  [%d] %s\n", i, keys[i]);
//             }
//         }
//         printf("====================\n");

//         // ========== 第四步：组包发送 ==========
//         int pktlen = send_to(opcode, buf, keys, values, kv_count);
//         if (pktlen <= 0) {
//             printf("组包失败\n");
//             continue;
//         }

//         int n=send(fd,buf,pktlen,0);
//         if(n>0){
//             printf("\nsend buf %d bytes\n",n);
//             hex_dump(buf,n,"发包结果");
//         }else if(n<0){
//             perror("send");
//             return -1;
//         }

//         // ========== 第五步：接收响应 ==========
//         memset(buf,0,BUFFER_SIZE);
//         n=recv(fd,buf,BUFFER_SIZE,0);
//         if(n>0){
//             hex_dump(buf, n, "收包结果");
//             parse_backage(buf);
//         }else if(n==0){
//             printf("服务器断开连接\n");
//             close(fd);
//             return 0;
//         }else if(n<0){
//             perror("recv");
//             return -1;
//         }
//     }

//     // 释放资源
//     for (int i = 0; i < MAX_KV_COUNT; i++) {
//         if (keys[i]) free(keys[i]);
//         if (values[i]) free(values[i]);
//     }
//     close(fd);
//     return 0;
// }
#include<stdio.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<string.h>
#include<stdlib.h>
#include"kvproto.h"
#include<sys/epoll.h>
#include<errno.h>
#include<unistd.h>

extern const char *command[];
#define BUFFER_SIZE 1024
#define MAX_KV_COUNT 64

enum{
    KV_CMD_START,
    KV_CMD_SET=KV_CMD_START,
    KV_CMD_GET,
    KV_CMD_DEL,
    KV_CMD_MOD,
    KV_CMD_EXIST,
    KV_CMD_COUNT
};

void *kv_malloc(size_t size){
    return malloc(size);
}

void kv_free(void* ptr){
    return free(ptr);
}

// 将名字改为 connect_to_server 更符合其实际功能
int connect_to_server(char *ip, int port){
    int socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if(socketfd < 0) {
        perror("socket");
        return -1;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);
    
    int ret = connect(socketfd, (struct sockaddr*)&addr, sizeof(struct sockaddr));
    if(ret < 0){
        perror("connect");
        close(socketfd);
        return -1;
    }
    return socketfd;
}

int read_line(char *buf, int size) {
    memset(buf, 0, size);
    if (fgets(buf, size, stdin) == NULL) {
        return -1;
    }
    size_t len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') {
        buf[len-1] = '\0';
        len--;
    }
    return (int)len;
}

int send_to(int opcode, char *wbuffer, char *keys[], char *values[], int kv_num){
    if (wbuffer == NULL || keys == NULL || kv_num <= 0) return -1;

    kv_header_t header;
    header.magic = KV_MAGIC;
    header.version = KV_VERSION;
    header.msg_type = MSG_TYPE_REQ;
    header.opcode = opcode;

    int offset = 0;
    int wlength = BUFFER_SIZE;
    uint32_t total_len = 0; 
    
    // 【修复Bug】将 KV_CMD_DEL 也加入写命令判断
    int is_write_cmd = (opcode == KV_CMD_SET || opcode == KV_CMD_MOD || opcode == KV_CMD_DEL);

    if (is_write_cmd) {
        if (values == NULL) return -1;
        for (int i = 0; i < kv_num; i++) {
            total_len += 7 + strlen(keys[i]) + 1;   
            total_len += 7 + strlen(values[i]) + 1; 
        }
    } else {
        for (int i = 0; i < kv_num; i++) {
            total_len += 7 + strlen(keys[i]) + 1;
        }
    }
    header.length = total_len;
    printf("header.length:%d\n",header.length);

    offset = kv_header_encode(&header, wbuffer + offset, wlength - offset);
    if (offset <= 0) return -1;

    TLV tlv;
    if (is_write_cmd) {
        for (int i = 0; i < kv_num; i++) {
            tlv.tag = TAG_KEY;
            tlv.type = TYPE_STRING;
            tlv.length = strlen(keys[i]) + 1;
            tlv.value = keys[i];
            int ret = kv_tlv_append(wbuffer + offset, wlength - offset, tlv);
            if (ret <= 0) return -1;
            offset += ret;

            tlv.tag = TAG_VALUE;
            tlv.type = TYPE_STRING;
            tlv.length = strlen(values[i]) + 1;
            tlv.value = values[i];
            ret = kv_tlv_append(wbuffer + offset, wlength - offset, tlv);
            if (ret <= 0) return -1;
            offset += ret;
        }
    } else {
        for (int i = 0; i < kv_num; i++) {
            tlv.tag = TAG_KEY;
            tlv.type = TYPE_STRING;
            tlv.length = strlen(keys[i]) + 1;
            tlv.value = keys[i];
            int ret = kv_tlv_append(wbuffer + offset, wlength - offset, tlv);
            if (ret <= 0) return -1;
            offset += ret;
        }
    }
    return offset;
}

int parse_backage(char *buf){
    kv_request_t req;
    kv_request_t_init(&req);
    kv_header_t header;
    int ret=0;
    int offset=0;
    ret=kv_header_decode(buf,1024,&header);
    offset+=ret;
    ret=kv_payload_decode(buf+offset,1024-offset,&req);
    offset+=ret;

    printf("\n=== 服务端响应 ===\n");
    for(int i=0;i<req.key_num;i++){
        if(req.key[i]->data!=NULL){
            printf("key[%d]: %s, len:%d\n", i, req.key[i]->data, req.key[i]->length);
        }
    }
    for(int i=0;i<req.value_num;i++){
        if(req.value[i]->data!=NULL){
            printf("value[%d]: %s, len:%d\n", i, req.value[i]->data, req.value[i]->length);
        }
    }
    kv_request_t_destory(&req);
    return offset;
}

void hex_dump(const void *data, size_t size, const char *title) {
    const uint8_t *buf = (const uint8_t *)data;
    size_t i, j;
    if (title != NULL) {
        printf("--- %s (总长度: %zu 字节) ---\n", title, size);
    }
    for (i = 0; i < size; i += 16) {
        printf("%04zx: ", i);
        for (j = 0; j < 16; j++) {
            if (i + j < size) {
                printf("%02X ", buf[i + j]);
            } else {
                printf("   ");
            }
            if (j == 7) {
                printf(" ");
            }
        }
        printf(" |");
        for (j = 0; j < 16; j++) {
            if (i + j >= size) break;
            uint8_t c = buf[i + j];
            if (c >= 0x20 && c <= 0x7E) {
                printf("%c", c);
            } else {
                printf(".");
            }
        }
        printf("|\n");
    }
}

int main(int argc, char *argv[]){
    char write_ip[64] = {0};
    char read_ip[64] = {0};
    char tmp_buf[64] = {0};
    int write_port = 0;
    int read_port = 0;

    // 【新增】启动时提示输入两个节点的信息
    printf("========== 分布式KV客户端 ==========\n");
    printf("请输入写节点IP (如 127.0.0.1): ");
    read_line(write_ip, sizeof(write_ip));
    printf("请输入写节点端口 (如 4000): ");
    read_line(tmp_buf, sizeof(tmp_buf));
    write_port = atoi(tmp_buf);

    printf("请输入读节点IP (如 127.0.0.1): ");
    read_line(read_ip, sizeof(read_ip));
    printf("请输入读节点端口 (如 4001): ");
    read_line(tmp_buf, sizeof(tmp_buf));
    read_port = atoi(tmp_buf);

    // 【新增】分别建立写连接和读连接
    int write_fd = connect_to_server(write_ip, write_port);
    if (write_fd < 0) {
        fprintf(stderr, "连接写节点失败\n");
        return -1;
    }

    int read_fd = connect_to_server(read_ip, read_port);
    if (read_fd < 0) {
        fprintf(stderr, "连接读节点失败\n");
        close(write_fd);
        return -1;
    }

    char buf[BUFFER_SIZE] = {0};
    char line[BUFFER_SIZE] = {0};
    char *keys[MAX_KV_COUNT] = {0};
    char *values[MAX_KV_COUNT] = {0};
    int kv_count = 0;

    printf("KV客户端启动成功，支持命令：SET / GET / DEL / MOD / EXIST\n");
    printf("输入规则：\n");
    printf("  1. 先输入命令类型\n");
    printf("  2. 按提示输入key，输入 00 结束输入\n");
    printf("  3. SET/MOD 命令每个key对应输入一个value\n\n");

    while(1){
        kv_count = 0;
        for (int i = 0; i < MAX_KV_COUNT; i++) {
            if (keys[i]) { free(keys[i]); keys[i] = NULL; }
            if (values[i]) { free(values[i]); values[i] = NULL; }
        }

        printf("\n====================\n");
        printf("输入命令：");
        if (read_line(line, BUFFER_SIZE) <= 0) continue;

        int opcode = -1;
        for(int i=0;i<KV_CMD_COUNT;i++){
            if(strcmp(command[i], line) == 0){
                opcode = i;
                break;
            }
        }
        if(opcode == -1){
            printf("未知命令：%s，请重新输入\n", line);
            continue;
        }
        
        // 【修复Bug】DEL 也是写命令
        int is_write_cmd = (opcode == KV_CMD_SET || opcode == KV_CMD_MOD || opcode == KV_CMD_DEL);

        printf("\n--- 开始输入（输入key时输入 00 结束） ---\n");
        while (kv_count < MAX_KV_COUNT) {
            printf("\n输入key：");
            if (read_line(line, BUFFER_SIZE) < 0) break;
            if (strcmp(line, "00") == 0) break;

            keys[kv_count] = strdup(line);
            if (keys[kv_count] == NULL) break;

            if (is_write_cmd) {
                printf("输入value：");
                if (read_line(line, BUFFER_SIZE) < 0) {
                    free(keys[kv_count]); keys[kv_count] = NULL; break;
                }
                values[kv_count] = strdup(line);
                if (values[kv_count] == NULL) {
                    free(keys[kv_count]); keys[kv_count] = NULL; break;
                }
            }
            kv_count++;
        }

        if (kv_count == 0) {
            printf("未输入有效key，请重新操作\n");
            continue;
        }

        // 配对校验补全逻辑（原样保留）
        if (is_write_cmd) {
            int unpaired = 0;
            for (int i = 0; i < kv_count; i++) {
                if (values[i] == NULL) unpaired++;
            }
            while (unpaired > 0) {
                printf("\n⚠️  键值数量不匹配，存在 %d 个未配对的key：\n", unpaired);
                printf("\n已配对的键值对：\n");
                int paired_idx = 0;
                for (int i = 0; i < kv_count; i++) {
                    if (values[i] != NULL) printf("  [%d] %s = %s\n", paired_idx++, keys[i], values[i]);
                }
                printf("\n未配对的key：\n");
                for (int i = 0; i < kv_count; i++) {
                    if (values[i] == NULL) printf("  → %s\n", keys[i]);
                }
                printf("\n--- 开始补全value ---\n");
                for (int i = 0; i < kv_count; i++) {
                    if (values[i] == NULL) {
                        printf("为 key[%s] 输入value：", keys[i]);
                        if (read_line(line, BUFFER_SIZE) > 0) {
                            values[i] = strdup(line);
                            unpaired--;
                        }
                    }
                }
            }
        }

        printf("\n====================\n");
        printf("✅ 输入完成，本次提交的内容：\n");
        if (is_write_cmd) {
            for (int i = 0; i < kv_count; i++) printf("  [%d] %s = %s\n", i, keys[i], values[i]);
        } else {
            for (int i = 0; i < kv_count; i++) printf("  [%d] %s\n", i, keys[i]);
        }
        printf("====================\n");

        int pktlen = send_to(opcode, buf, keys, values, kv_count);
        if (pktlen <= 0) {
            printf("组包失败\n");
            continue;
        }

        // 【核心修改】根据命令类型，选择对应的 fd
        int target_fd = is_write_cmd ? write_fd : read_fd;
        const char *target_name = is_write_cmd ? "写节点" : "读节点";

        int n = send(target_fd, buf, pktlen, 0);
        if(n > 0){
            printf("\n向 [%s] 发送数据 %d bytes\n", target_name, n);
            hex_dump(buf, n, "发包结果");
        } else {
            perror("send failed");
            // 这里可以选择重连或者直接退出
            if (is_write_cmd) close(write_fd); else close(read_fd);
            return -1;
        }

        memset(buf, 0, BUFFER_SIZE);
        n = recv(target_fd, buf, BUFFER_SIZE, 0);
        if(n > 0){
            hex_dump(buf, n, "收包结果");
            parse_backage(buf);
        } else if(n == 0){
            printf("%s断开连接\n", target_name);
            if (is_write_cmd) close(write_fd); else close(read_fd);
            return 0;
        } else {
            perror("recv");
            return -1;
        }
    }

    for (int i = 0; i < MAX_KV_COUNT; i++) {
        if (keys[i]) free(keys[i]);
        if (values[i]) free(values[i]);
    }
    close(write_fd);
    close(read_fd);
    return 0;
}
