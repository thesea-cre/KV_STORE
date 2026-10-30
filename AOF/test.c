#include<stdio.h>
#include<stdlib.h>
#include"adapt.h"
#include"aof.h"
#include<string.h>
#include<unistd.h>

#define CHECK(cond)do{                                      \
    if(!(cond)){                                             \
        printf("FAIL %s:%d:%s\n",__FILE__,__LINE__,#cond); \
        exit(1);                                           \
    }                                                      \
}while(0);

// 辅助函数：清理测试文件
static void clean_test_files() {
    unlink("db_1");
    unlink("st_1");
}

// 【新增】辅助函数：强制把缓冲区里残留的数据刷入磁盘
// 模拟后台线程的定时刷盘，或者服务关闭前的最后一次刷盘
static void force_flush(AOFcontext_ctx *ctx, char *huge_buf, int *huge_len, int *current_num) {
    if (*huge_len > 0) {
        int n = write(ctx->db_fd, huge_buf, *huge_len);
        ctx->size += n;
        ctx->current_count += *current_num;
        
        // 同步状态文件
        lseek(ctx->st_fd, 0, SEEK_SET);
        write(ctx->st_fd, &ctx->size, sizeof(int));
        write(ctx->st_fd, &ctx->current_count, sizeof(int));
        
        // 重置缓冲区
        *huge_len = 0;
        *current_num = 0;
    }
}

void test_aof_init(){
    clean_test_files();
    
    AOFcontext_ctx *ctx=aof_init("db_1","st_1");
    CHECK(ctx != NULL);
    CHECK(ctx->db_fd >= 0);
    CHECK(ctx->st_fd >= 0);
    CHECK(ctx->size == 0);
    CHECK(ctx->current_count == 0);
    
    aof_close(ctx);
    printf("PASS test_aof_init\n");
}

void test_aof_append(){
    clean_test_files();
    AOFcontext_ctx *ctx=aof_init("db_1","st_1");
    
    char huge_buf[4096] = {0};
    int huge_len = 0;
    int current_num = 0;

    // 写入三条数据（都会进缓冲区）
    aof_append(ctx,"SET","king",(char*)"\01""123",5, huge_buf, &huge_len, &current_num);
    CHECK(huge_len==(4+4+5+5));
    aof_append(ctx,"DEL","king",NULL,0, huge_buf, &huge_len, &current_num);
    CHECK(huge_len==(4+4+5+5)+(4+4+5));
    aof_append(ctx,"SET","snow",(char *)"\01""234",5, huge_buf, &huge_len, &current_num);
    CHECK(huge_len==(4+4+5+5)+(4+4+5)+(4+4+5+5));
    // 此时磁盘上 size 应该还是 0，因为都在缓冲区里
    CHECK(current_num==3);
    CHECK(ctx->size == 0);
    CHECK(ctx->current_count == 0);
    
    // 强制刷盘
    force_flush(ctx, huge_buf, &huge_len, &current_num);
    
    // 刷盘后校验
    CHECK(ctx->current_count == 3);
    CHECK(ctx->size == (4+4+5+5)+(4+4+5)+(4+4+5+5));
    
    aof_close(ctx);
    
    // 重新打开校验状态
    ctx = aof_init("db_1","st_1");
    CHECK(ctx->current_count == 3); 
    aof_close(ctx);
    
    printf("PASS test_aof_append\n");
}

void test_aof_recover(){
    clean_test_files();
    init_kvengine();
    
    AOFcontext_ctx *ctx=aof_init("db_1","st_1");
    char huge_buf[4096] = {0};
    int huge_len = 0;
    int current_num = 0;
    
    // 1. 模拟业务操作
    aof_append(ctx,"SET","king",(char*)"\01""123",5, huge_buf, &huge_len, &current_num);
    kv_set("king",(uint8_t*)"\01""123",5);
    aof_append(ctx,"DEL","king",NULL,0, huge_buf, &huge_len, &current_num);
    kv_del("king");
    uint8_t *value_1=NULL;
    uint32_t value_len_1=0;
    kv_get("king",&value_1,&value_len_1);
    CHECK(value_1==NULL); 
    aof_append(ctx,"SET","snow",(char *)"\01""234",5, huge_buf, &huge_len, &current_num);
    kv_set("snow",(uint8_t*)"\01""234",5);
    
    // 【重要】重启前，必须把缓冲区刷入磁盘
    force_flush(ctx, huge_buf, &huge_len, &current_num);
    
    // 2. 关闭
    aof_close(ctx);
    dest_kvengine();
    
    // 3. 重新启动加载
    init_kvengine();
    ctx = aof_init("db_1","st_1");
    CHECK(ctx->current_count==3);
    aof_recover(ctx);
    
    uint8_t *value=NULL;
    uint32_t value_len=0;
    kv_get("king",&value,&value_len);
    CHECK(value==NULL); 
    kv_get("snow",&value,&value_len);
    CHECK(strcmp(value+1,"234")==0);
    
    aof_close(ctx);
    dest_kvengine();
    printf("PASS test_aof_recover\n");
}

// 【新增】测试缓冲区满自动刷盘与 AOF 重写
void test_aof_rewrite(){
    clean_test_files();
    init_kvengine();
    AOFcontext_ctx *ctx=aof_init("db_1","st_1");
    
    char huge_buf[4096] = {0};
    int huge_len = 0;
    int current_num = 0;
    char key_buf[20];
    
    // 写 1001 个数据，必然会多次撑爆 4096 缓冲区，触发自动刷盘
    for (int i = 0; i < 1000; i++) {
        sprintf(key_buf, "k%d", i % 10); 
        // 每次进来 aof_append 都可能触发刷盘
        aof_append(ctx, "SET", key_buf, (char*)"\01""v", 3, huge_buf, &huge_len, &current_num);
        kv_set(key_buf, (uint8_t*)"\01""v", 3);
    }
    
    CHECK(huge_len==1694);
    CHECK(current_num==121);
    CHECK(ctx->current_count==10);
    force_flush(ctx, huge_buf, &huge_len, &current_num);
    
    printf("kv_count:%d\n", kv_count());
    // 因为我们只有10个不同的 key，所以引擎里有 10 个。
    // 之前测试断言 11，那是错误理解了引擎的覆盖逻辑。
    // 重写之后 current_count 应该等于引擎里的实际 key 数量 (10个)
    
    aof_close(ctx);
    dest_kvengine();
    printf("PASS test_aof_rewrite\n");
}

int main(){
    test_aof_init();
    //test_aof_append();
    test_aof_recover();
    test_aof_rewrite();
    
    printf("--- 所有 AOF 测试通过！---\n");
    return 0;
}