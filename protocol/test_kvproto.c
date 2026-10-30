#include<string.h>
#include<stdlib.h>
#include<stdio.h>
#include<arpa/inet.h>
#include"kvproto.h"

#define  CHECK(cond) do {                                   \
    if(!(cond)){                                            \
        printf("FAIL %s:%d:%s\n",__FILE__,__LINE__,#cond); \
        exit(1);                                           \
    }                                                      \
}while(0);

static void res_zero(RES *r){
    memset(r,0,sizeof(RES));
}

// 【改进】统一释放构造测试用例时申请的内存
static void free_test(RES *r){
    if (r->key) free(r->key);
    if (r->value) free(r->value);
    if (r->value_len) free(r->value_len);
    res_zero(r);
}

// ==================== 1. 正常往返测试 (SET/GET/DEL/MOD/EXIST) ====================

static void make_set_res(RES *r){
    res_zero(r);
    r->key=calloc(2,sizeof(uint8_t*));
    r->value=calloc(2,sizeof(uint8_t*));
    r->value_len=calloc(2,sizeof(uint32_t));
    r->op="SET";
    r->key[0]=(uint8_t*)"king";
    r->key[1]=(uint8_t*)"snow";
    r->value[0]=(uint8_t*)"\x01""123";
    r->value[1]=(uint8_t*)"\x01""234";
    r->value_len[0]=1+4;
    r->value_len[1]=1+4;
    r->num=2;
}

static void test_set_roundtrip(){
    uint8_t buf[1024];
    RES in,out;
    make_set_res(&in);
    KV_encode(buf,sizeof(buf),&in);
    CHECK(in.status == OK);
    
    res_zero(&out);
    KV_decode(buf,in.cons_,&out);
    CHECK(out.status == OK);
    CHECK(strcmp(out.op,"SET")==0);
    CHECK(out.num == 2);
    CHECK(strcmp((char*)out.key[0],"king")==0);
    CHECK(out.value[0][0] == TYPE_STRING);
    CHECK(strcmp((char*)out.value[0]+1,"123")==0);
    CHECK(out.value_len[0] == 5); // 注意：你的decode里给value_length+1了
    
    kv_RES_t_destory(&out);
    free_test(&in);
    printf("PASS test_set_roundtrip\n");
}

// 【新增】测试 DEL 命令
static void test_del_roundtrip(){
    uint8_t buf[1024];
    RES in,out;
    res_zero(&in);
    in.op="DEL";
    in.num=1;
    in.key=calloc(1,sizeof(uint8_t*));
    in.key[0]=(uint8_t*)"king";
    
    KV_encode(buf,sizeof(buf),&in);
    CHECK(in.status == OK);
    
    res_zero(&out);
    KV_decode(buf,in.cons_,&out);
    CHECK(out.status == OK);
    CHECK(strcmp(out.op,"DEL")==0);
    CHECK(strcmp((char*)out.key[0],"king")==0);
    // DEL命令 decode 出来的 value 和 value_len 应该是固定的 dummy
    CHECK(out.value[0][0] == 0); 
    CHECK(out.value_len[0] == 2);

    kv_RES_t_destory(&out);
    free_test(&in);
    printf("PASS test_del_roundtrip\n");
}

// 【新增】测试 EXIST 命令
static void test_exist_roundtrip(){
    uint8_t buf[1024];
    RES in,out;
    res_zero(&in);
    in.op="EXIST";
    in.num=1;
    in.key=calloc(1,sizeof(uint8_t*));
    in.key[0]=(uint8_t*)"snow";
    
    KV_encode(buf,sizeof(buf),&in);
    CHECK(in.status == OK);
    
    res_zero(&out);
    KV_decode(buf,in.cons_,&out);
    CHECK(out.status == OK);
    CHECK(strcmp(out.op,"EXIST")==0);
    CHECK(strcmp((char*)out.key[0],"snow")==0);

    kv_RES_t_destory(&out);
    free_test(&in);
    printf("PASS test_exist_roundtrip\n");
}

// 之前写好的 RES 测试保持不动，稍微修正 status 判断
static void make_res_res(RES *r){
    res_zero(r);
    r->key=calloc(2,sizeof(uint8_t*));
    r->value=calloc(2,sizeof(uint8_t*));
    r->value_len=calloc(2,sizeof(uint32_t));
    r->op="RES";
    r->key[0]=NULL;
    r->key[1]=NULL;
    r->value[0]=(uint8_t*)"\x01""OK\r\n";
    r->value[1]=(uint8_t*)"\x01""ERROR\r\n";
    r->value_len[0]=1+5;
    r->value_len[1]=1+8;
    r->num=2;
}

static void test_res_roundtrip(){
    uint8_t buf[1024];
    RES in,out;
    make_res_res(&in);
    KV_encode(buf,sizeof(buf),&in);
    CHECK(in.status == OK);
    
    res_zero(&out);
    KV_decode(buf,in.cons_,&out);
    CHECK(out.status == OK);
    CHECK(strcmp(out.op,"RES")==0);
    CHECK(strcmp((char*)out.value[0]+1,"OK\r\n")==0);

    kv_RES_t_destory(&out);
    free_test(&in);
    printf("PASS test_res_roundtrip\n");
}

// ==================== 2. 异常与边界测试 ====================

// 【新增】测试解码半包 (WAIT 状态)
static void test_decode_wait(){
    uint8_t buf[1024];
    RES in;
    make_set_res(&in);
    KV_encode(buf,sizeof(buf),&in); // 先正常编码出完整包

    RES out;
    res_zero(&out);
    // 故意只传一半的数据，应该返回 WAIT
    KV_decode(buf, in.cons_ / 2, &out); 
    CHECK(out.status == WAIT);
    
    free_test(&in);
    printf("PASS test_decode_wait\n");
}

// 【新增】测试解码坏包 (Magic 错误)
static void test_decode_bad_magic(){
    uint8_t buf[1024] = {0};
    RES out;
    res_zero(&out);
    // 全填 0，Magic 肯定不对
    KV_decode(buf, sizeof(buf), &out);
    CHECK(out.status == ERROR);
    printf("PASS test_decode_bad_magic\n");
}

// 【新增】测试编码参数非法 (防御性编程)
static void test_encode_invalid(){
    uint8_t buf[1024];
    RES in;
    
    // 1. 测空 op
    res_zero(&in);
    in.op = NULL;
    in.num = 1;
    KV_encode(buf, sizeof(buf), &in);
    CHECK(in.status == ERROR);

    // 2. 测 num 为 0
    res_zero(&in);
    in.op = "SET";
    in.num = 0;
    KV_encode(buf, sizeof(buf), &in);
    CHECK(in.status == ERROR);

    // 3. 测 SET 但 value 为空
    res_zero(&in);
    in.op = "SET";
    in.num = 1;
    in.key = calloc(1, sizeof(uint8_t*));
    in.value = NULL; // value 为空
    KV_encode(buf, sizeof(buf), &in);
    CHECK(in.status == ERROR);
    free(in.key);

    printf("PASS test_encode_invalid\n");
}

int main(){
    printf("--- 开始测试协议层 ---\n");
    test_set_roundtrip();
    test_del_roundtrip();
    test_exist_roundtrip();
    test_res_roundtrip();
    
    test_decode_wait();
    test_decode_bad_magic();
    test_encode_invalid();
    
    printf("--- 所有测试通过！---\n");
    return 0;
}