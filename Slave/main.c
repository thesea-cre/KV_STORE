// #include"server.h"

// int main(int argc,char *argv[]){
//     if(argc!=2) return -1;
//     #if 1
//     mempool=Managerpool_GetInstance();
//     #endif
//     aof_init("aof_db");
//     unsigned int port=atoi(argv[1]);
//     init_kvengine();
//     aof_recover();
//     #if NETWORK_SELECT == ENABLE_REACTOR
//     reactor_entry(port);
//     #elif NETWORK_SELECT == ENABLE_PROACTOR
//     uring_start(port);
//     #elif NETWORK_SELECT == ENABLE_NTYCO
//     nytco(port);
//     #endif
//     // aof_close();
//     dest_kvengine();
 

// }
#include "server.h"
#include "sync.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* 全局角色标志：0-主节点  1-从节点  2-单机（网络层 reactor.c 已 extern 引用） */
int s_or_c = 2;

/* 从节点连接主服务器的目标地址（可从命令行覆盖） */
char ip[64]   = "127.0.0.1";
int main(int argc, char *argv[]) {
    if (argc !=4) {
        fprintf(stderr,
                "usage: %s [ip] [port] [role]\n"
                "  role: standalone(默认) | master | slave\n"
                "  ex  : %s 192.168.72.129 2000 master\n"
                "        %s 192.168.72.128 8000 slave\n",
                argv[0], argv[0], argv[0]);
        return -1;
    }

    /* 1. 内存池 */
#if 1
    mempool = Managerpool_GetInstance();
#endif

    /* 2. AOF 文件初始化 */
    aof_init("aof_db");
    /* 3. 解析命令行 */
    unsigned int port = (unsigned int)atoi(argv[2]);

        if      (strcmp(argv[3], "master") == 0) s_or_c = 0;
        else if (strcmp(argv[3], "slave")  == 0) s_or_c = 1;
        else                                     s_or_c = 2;
        strncpy(ip, argv[1], sizeof(ip) - 1);


    /* 4. 引擎初始化 + AOF 恢复 */
    init_kvengine();
    aof_recover();

    /* 5. 复制模块初始化（★ 老版本缺的就是这一步） */
    if (s_or_c == 0) {
        /* 主节点：初始化全局 MasterMgr */
        if (master_init(&mgr) != 0) {
            fprintf(stderr, "master_init failed\n");
            return -1;
        }
        printf("[main] 角色 = 主节点, port=%u\n", port);
    } else if (s_or_c == 1) {
        /* 从节点：初始化本地 Slave 上下文 */
        if (slave_init(&slave) != 0) {
            fprintf(stderr, "slave_init failed\n");
            return -1;
        }
        /* master_fd 由 reactor_entry 里的 connect_server() 建立后再回填 */
        printf("[main] 角色 = 从节点, port=%u\n",port);
    } else {
        printf("[main] 角色 = 单机, port=%u\n", port);
    }

    /* 6. 网络层入口 */
#if NETWORK_SELECT == ENABLE_REACTOR
    reactor_entry(ip,port);
#elif NETWORK_SELECT == ENABLE_PROACTOR
    uring_start(port);
#elif NETWORK_SELECT == ENABLE_NTYCO
    nytco(port);
#endif

    /* 7. 收尾（网络层返回说明已退出） */
    if (s_or_c == 0) master_destory(&mgr);
    if (s_or_c == 1) slave_destory(&slave);

    aof_close();
    dest_kvengine();
    return 0;
}