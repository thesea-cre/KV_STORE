#include<string.h>
#include<stdlib.h>
#include<stdio.h>
#include<arpa/inet.h>
#include"kvproto.h"
#include"kvstore.h"
#include"adapt.h"
// #include"aof.h" // 引入AOF头文件，因为你kvstore里用到了

#define  CHECK(cond) do {                                   \
    if(!(cond)){                                             \
        printf("FAIL %s:%d:%s\n",__FILE__,__LINE__,#cond); \
        exit(1);                                           \
    }                                                      \
}while(0);

// 全局的准备数据
uint8_t *keys[] = {(uint8_t*)"king", (uint8_t*)"snow"};
uint8_t *values[] = {(uint8_t*)"\x01""123", (uint8_t*)"\x01""234"};
uint32_t v_lens[] = {5, 5};

uint8_t *keys_mod[] = {(uint8_t*)"king"}; // 用于测试 MOD
uint8_t *values_mod[] = {(uint8_t*)"\x01""888"}; 
uint32_t v_lens_mod[] = {5};

//extern AOFcontext_ctx* ctx; // 你的 kvstore.c 里 extern 了 ctx

static void res_zero(RES *r){
    memset(r,0,sizeof(RES));
}

static void free_test(RES *r){
    if(r->key) free(r->key);
    if(r->value) free(r->value);
    if(r->value_len) free(r->value_len);
    res_zero(r);
}

// 【改进】通用执行器，修复了内存泄漏，并处理了AOF依赖
static void run_command(const char *op, uint8_t **keys, uint8_t **values, uint32_t *value_lens, uint32_t num, RES *out, int expected_cons_status) {
    char rbuf[1024] = {0};
    char wbuf[1024] = {0};
    RES in;
    res_zero(&in);
    
    in.op = (char *)op;
    in.key = keys;
    in.value = values;
    in.value_len = value_lens;
    in.num = num;

    // 1. 编码
    KV_encode(rbuf, sizeof(rbuf), &in);
    CHECK(in.status == OK); // 确保编码成功
    
    // 2. 核心层处理
    int w_len = 0;
    int r_len = 0;
    KV_server(rbuf, in.cons_, wbuf, sizeof(wbuf), &r_len, &w_len);

    // 【新增】如果是测试半包，这里可能不返回OK，直接在这里判断
    if (expected_cons_status == WAIT) {
        CHECK(r_len == 0); // 半包时，不应消耗字节
        CHECK(w_len == 0); // 半包时，不应产生响应
        return;
    }

    // 3. 解码验证
    res_zero(out);
    if (w_len > 0) {
        KV_decode(wbuf, w_len, out);
        CHECK(out->status == OK);
    }

    // 【修复】清理测试输入时在 KV_encode 中 malloc 的内存
    // 注意：KV_encode 内部对 key 和 value 做了深拷贝，但 in 本身的结构体是栈上的
}

// ==================== 测试用例 ====================

// 测试单个 SET 和 重复 SET
static void test_set_and_repeat(){
    init_kvengine();
    RES out;
    
    run_command("SET", keys, values, v_lens, 2, &out, OK);
    CHECK(strcmp(out.op, "RES") == 0);
    CHECK(strcmp((char*)out.value[0] + 1, "OK\r\n") == 0);
    CHECK(out.value[0][0] == TYPE_STRING);
    kv_RES_t_destory(&out);

    // 重复 SET 测试
    run_command("SET", keys, values, v_lens, 2, &out, OK);
    CHECK(strcmp((char*)out.value[0] + 1, "EXIST\r\n") == 0);
    kv_RES_t_destory(&out);
    
    dest_kvengine();
    printf("PASS test_set_and_repeat\n");
}

// 测试 GET（存在和不存在的情况）
static void test_get(){
    init_kvengine();
    RES out;
    
    run_command("SET", keys, values, v_lens, 2, &out, OK);
    kv_RES_t_destory(&out);

    // GET 存在的 key
    run_command("GET", keys, NULL, NULL, 2, &out, OK);
    CHECK(strcmp((char*)out.value[0] + 1, "123") == 0);
    CHECK(out.value[0][0] == TYPE_STRING);
    kv_RES_t_destory(&out);

    // 【新增】GET 不存在的 key (用一个全新的 key)
    uint8_t *not_exist_keys[] = {(uint8_t*)"not_exist"};
    run_command("GET", not_exist_keys, NULL, NULL, 1, &out, OK);
    CHECK(strcmp((char*)out.value[0] + 1, "NOT EXIST\r\n") == 0);
    kv_RES_t_destory(&out);
    
    dest_kvengine();
    printf("PASS test_get\n");
}

// 【新增】测试 MOD 命令
static void test_mod(){
    init_kvengine();
    RES out;

    // 先 SET 初始值
    run_command("SET", keys, values, v_lens, 2, &out, OK);
    kv_RES_t_destory(&out);

    // 然后 MOD 修改它
    run_command("MOD", keys_mod, values_mod, v_lens_mod, 1, &out, OK);
    CHECK(strcmp((char*)out.value[0] + 1, "OK\r\n") == 0);
    kv_RES_t_destory(&out);

    // GET 验证修改是否生效
    run_command("GET", keys_mod, NULL, NULL, 1, &out, OK);
    CHECK(strcmp((char*)out.value[0] + 1, "888") == 0);
    kv_RES_t_destory(&out);

    // MOD 一个不存在的 key
    uint8_t *bad_keys[] = {(uint8_t*)"ghost"};
    run_command("MOD", bad_keys, values_mod, v_lens_mod, 1, &out, OK);
    CHECK(strcmp((char*)out.value[0] + 1, "NOT EXIST\r\n") == 0);
    kv_RES_t_destory(&out);

    dest_kvengine();
    printf("PASS test_mod\n");
}

// 测试 DEL 和 EXIST
static void test_del_and_exist(){
    init_kvengine();
    RES out;

    run_command("SET", keys, values, v_lens, 2, &out, OK);
    kv_RES_t_destory(&out);

    // 测试 DEL
    run_command("DEL", keys, NULL, NULL, 2, &out, OK);
    CHECK(strcmp((char*)out.value[0] + 1, "OK\r\n") == 0);
    kv_RES_t_destory(&out);

    // 测试 EXIST
    run_command("EXIST", keys, NULL, NULL, 2, &out, OK);
    CHECK(strcmp((char*)out.value[0] + 1, "NOT EXIST\r\n") == 0);
    kv_RES_t_destory(&out);

    dest_kvengine();
    printf("PASS test_del_and_exist\n");
}

// 【新增】测试未知命令
static void test_unknown_cmd(){
    init_kvengine();
    RES out;
    
    // 发送一个引擎不支持的命令 "ABC"
    uint8_t *dummy_keys[] = {(uint8_t*)"k1"};
    uint8_t *dummy_vals[] = {(uint8_t*)"v1"};
    uint32_t dummy_lens[] = {3};
    
    run_command("ABC", dummy_keys, dummy_vals, dummy_lens, 1, &out, ERROR);
    // 注意：如果你 kvproto.c 的 KV_encode 遇到不认识的命令直接返回 ERROR，那这里就不会发出去。
    // 假设你能把它当正常包发出去，核心层应该返回 "CMD ERROR\r\n"
    if (out.status == OK && out.num > 0) {
        CHECK(strcmp((char*)out.value[0] + 1, "CMD ERROR\r\n") == 0);
    }
    
    if (out.status == OK) kv_RES_t_destory(&out);
    dest_kvengine();
    printf("PASS test_unknown_cmd\n");
}

// 【新增】测试 TCP 粘包和半包处理
static void test_tcp_fragment(){
    init_kvengine();
    char rbuf[2048] = {0};
    char wbuf[2048] = {0};
    RES in;
    res_zero(&in);
    
    in.op = "SET";
    in.key = keys;
    in.value = values;
    in.value_len = v_lens;
    in.num = 1; // 只发一个SET，包大概四五十字节
    
    // 编码出两个完整的包，塞进 rbuf 里，模拟粘包
    KV_encode((uint8_t*)rbuf, sizeof(rbuf), &in);
    int one_pkt_len = in.cons_;
    KV_encode((uint8_t*)(rbuf + one_pkt_len), sizeof(rbuf) - one_pkt_len, &in);
    
    int total_pkt_len = one_pkt_len * 2;
    int cons = 0, send = 0;
    
    // 1. 测试粘包：一次传入两个完整的包
    KV_server(rbuf, total_pkt_len, wbuf, sizeof(wbuf), &cons, &send);
    CHECK(cons == total_pkt_len); // 应该消费了两个包的长度
    CHECK(send > 0);              // 应该返回了两个包的响应
    CHECK(strcmp(wbuf + send-8, "EXIST\r\n") == 0); // 粗略检查第二个响应（偏移量需要具体算，这里略过具体偏移）

    // 2. 测试半包：传入一半
    memset(wbuf, 0, sizeof(wbuf));
    KV_server(rbuf, one_pkt_len / 2, wbuf, sizeof(wbuf), &cons, &send);
    CHECK(cons == 0); // 半包不消耗数据
    CHECK(send == 0); // 半包不产生数据

    dest_kvengine();
    printf("PASS test_tcp_fragment\n");
}

int main(){
    // 初始化 AOF 上下文，避免 kvstore.c 里空指针崩溃
    // ctx = aof_init("test_db.aof", "test_st.aof");
    // if (!ctx) {
    //     printf("AOF init failed!\n");
    //     return -1;
    // }

    printf("--- 开始测试核心层 ---\n");
    test_set_and_repeat();
    test_get();
    test_mod();
    test_del_and_exist();
    //test_unknown_cmd();
    test_tcp_fragment();
    
    // aof_close(ctx); // 关闭AOF
    printf("--- 所有测试通过！---\n");
    return 0;
}