#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include<signal.h>
#include"kvstore.h"
#include"adapt.h"
#include"kvproto.h"
#include"tool.h"
#include"upload.h"
#include"resp.h"
#if LOG
#include"log.h"
#endif
#if AOF
#include"aof.h"
AOFcontext_ctx *ctx;
#endif
#if SYNC
#include"sync_new.h"
MasterMgr *mgr;
#endif

char ip[64]   = "127.0.0.1";
int s_or_c = 2;
extern long current_alloc;
extern current_free;

// 捕捉到 Ctrl+C 时执行的函数
void handle_sigint(int sig) {
    printf("\n\n[捕获到 Ctrl+C] 准备退出...\n");
    printf("总计分配: %ld, 总计释放: %ld\n", current_alloc, current_free);
    printf("最终遗留的 Diff (未释放的块数): %ld\n", current_alloc - current_free);
    exit(0);
}
int main(int argc, char *argv[]) {
    signal(SIGINT, handle_sigint);
    if (argc !=4) {
        fprintf(stderr,
                "usage: %s [ip] [port] [role]\n"
                "  role: standalone(默认) | master | slave\n"
                "  ex  : %s 192.168.72.129 2000 master\n"
                "        %s 192.168.72.128 8000 slave\n",
                argv[0], argv[0], argv[0]);
        return -1;
    }


    #if AOF
    ctx=aof_init("db_1","st_1");
    #endif

    #if SYNC
    printf("master_init\n");
    mgr=(MasterMgr* )kv_malloc(sizeof(MasterMgr));
    master_init(mgr);
    #endif

    #if LOG
    log_init("server.log", LOG_INFO); 
    LOG_INFO("服务端启动成功，监听端口: %d", 2000);
    #endif

    unsigned int port = (unsigned int)atoi(argv[2]);

        if      (strcmp(argv[3], "master") == 0) s_or_c = 0;
        else if (strcmp(argv[3], "slave")  == 0) s_or_c = 1;
        else                                     s_or_c = 2;
        strncpy(ip, argv[1], sizeof(ip) - 1);


    /* 4. 引擎初始化 + AOF 恢复 */
    init_kvengine();
    kv_upload_init();   
    kv_resp_init();     
    #if AOF
    aof_recover(ctx);
    #endif

    printf("[main] 角色 = 单机, port=%u\n", port);

    /* 6. 网络层入口 */
#if NETWORK_SELECT == ENABLE_REACTOR
    reactor_entry(ip,port);
#elif NETWORK_SELECT == ENABLE_PROACTOR
    uring_start(ip,port);
#elif NETWORK_SELECT == ENABLE_NTYCO
    nytco(ip,port);
#endif

    
    #if AOF
    aof_close(ctx);
    #endif
    #if SYNC
    master_destory(mgr);
    #endif
    kv_upload_abort_all();
    kv_resp_abort_all();
    dest_kvengine();
    log_close();
    return 0;
}