#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<arpa/inet.h>
#include<sys/time.h>
#include<sys/socket.h>
#include<pthread.h>
#include<unistd.h>
#include"kvproto.h"
#define MAX_MSG_LENGTH 1024
#define TIME_SUB_MS(tv1, tv2)  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)
static char* type_[]={
    "start","string","image","url","else"
};
char *ip;
unsigned short port;
static void* kv_malloc(int size){
    return malloc(size);
}
static void kv_free(void *ptr){
    return free(ptr);
}
int recv_msg(int fd,char *msg,int length){
    int ret=recv(fd,msg,length,0);
    if(ret<0){
        perror("recv");
        return -1;
    }
    return ret;
}
int send_msg(int fd,char *msg,int length){
    int ret=send(fd,msg,length,0);
    if(ret<0){
        perror("recv");
        return -1;
    }
    return ret;
}
int connect_server(const char *ip,unsigned short port){
    int connfd=socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in addr;
    addr.sin_family=AF_INET;
    addr.sin_port=htons(port);
    addr.sin_addr.s_addr=inet_addr(ip);
    if(0!=connect(connfd,(struct sockaddr*)&addr,sizeof(struct sockaddr_in))){
        perror("connect");
        exit(1);
    }
    return connfd;

}
// 在 test_kvstore.c 里写，而不是 kvproto.c
void client_build_RES(RES* res, char *op, char **key, char **raw_value, char **type_str, int *value_len, int num) {
    if (!res || !op || num <= 0) return;
    
    memset(res, 0, sizeof(RES));
    res->op = op;
    res->key = (uint8_t **)key;
    res->num = num;
    
    // 如果是 GET 等不需要 value 的命令，直接返回
    if (!raw_value || !type_str || !value_len) return;

    res->value = (uint8_t **)kv_malloc(num * sizeof(uint8_t *));
    res->value_len = (uint32_t *)kv_malloc(num * sizeof(uint32_t));

    for (int i = 0; i < num; i++) {
        // 1. 转换 type
        uint8_t type_byte = 0;
        for (int j = KV_TYPE_START; j < KV_TYPE_COUNT; j++) {
            if (strcmp(type_str[i], type_[j]) == 0) {
                type_byte = (uint8_t)j;
                break;
            }
        }
        
        // 2. 分配内存并组装
        res->value_len[i] = value_len[i] + 1; // +1 是因为要放 type
        res->value[i] = (uint8_t *)kv_malloc(res->value_len[i]);
        
        res->value[i][0] = type_byte; // 写入类型
        memcpy(res->value[i] + 1, raw_value[i], value_len[i]); // 拷贝纯数据
    }
}
// 建议的签名
void testcase(int connfd, const char *cmd_line, const char *expected_res, const char *casename) {
    char op[16] = {0};
    char key[64] = {0};
    char value[256] = {0};
    
    // 解析命令，比如 "SET Teacher King"
    int count = sscanf(cmd_line, "%s %s %s", op, key, value);
    if (count < 2) return;

    RES in;
    memset(&in, 0, sizeof(RES));
    in.op = op;
    
    // 动态构造 RES
    char *keys[] = {key};
    char *vals[] = {count == 3 ? value : NULL};
    int v_lens[] = {count == 3 ? (int)strlen(value) : 0};
    
    if (count == 3) {
        client_build_RES(&in, op, keys, vals, (char*[]){"string"}, v_lens, 1);
    } else {
        client_build_RES(&in, op, keys, NULL, NULL, NULL, 1);
    }

    char msg[1024] = {0};
    KV_encode((uint8_t*)msg, 1024, &in);
    
    // 发送请求
    send_msg(connfd, msg, in.cons_);

    // 接收响应
    char result[1024] = {0};
    int rlen = recv_msg(connfd, result, MAX_MSG_LENGTH);
    
    // 必须解码！
    RES out;
    memset(&out, 0, sizeof(RES));
    KV_decode((uint8_t*)result, rlen, &out);

    // 提取响应中的数据部分（跳过第一字节的 TYPE）
    if (out.num > 0 && out.value && out.value[0]) {
        char *actual_res = (char *)(out.value[0] + 1); 
        if (strcmp(actual_res, expected_res) != 0) {
            printf("==>failed->%s, '%s' != '%s' \n", casename, actual_res, expected_res);
            exit(1);
        }
    }
    
    // 注意释放 out！
}
void array_testcase(int connfd) {
	testcase(connfd, "SET Teacher King", "OK\r\n", "SET-Teacher");
	testcase(connfd, "GET Teacher", "King", "GET-Teacher");
	testcase(connfd, "MOD Teacher Darren", "OK\r\n", "MOD-Teacher");
	testcase(connfd, "GET Teacher", "Darren", "GET-Teacher");
	testcase(connfd, "EXIST Teacher", "EXIST\r\n", "EXIST-Teacher");
	testcase(connfd, "DEL Teacher", "OK\r\n", "DEL-Teacher");
	testcase(connfd, "GET Teacher", "NOT EXIST\r\n", "GET-Teacher");
	testcase(connfd, "MOD Teacher KING", "NOT EXIST\r\n", "MOD-Teacher");
	testcase(connfd, "EXIST Teacher", "NOT EXIST\r\n", "EXIST-Teacher");
}
void array_testcase_1w(int connfd,int count) {

	int i = 0;
	struct timeval tv_begin;
	gettimeofday(&tv_begin, NULL);
    while(i<count){
        array_testcase(connfd);
        i++;
    }
	struct timeval tv_end;
	gettimeofday(&tv_end, NULL);
	int time_used = TIME_SUB_MS(tv_end, tv_begin); // ms
	printf("array testcase --> time_used: %d, qps: %d\n", time_used, 9*count*1000 / time_used);
}
// 在 testcase.c 中追加这个函数
void pipeline_testcase_1w(int connfd, int batch_size, int total_rounds) {
    struct timeval tv_begin, tv_end;
    gettimeofday(&tv_begin, NULL);
    
    char msg[1024] = {0};
    char result[1024] = {0};
    
    // 构造一个固定的 SET 包
    RES in;
    memset(&in, 0, sizeof(RES));
    char *keys[] = {"Teacher"};
    char *vals[] = {"King"};
    int v_lens[] = {5};
    client_build_RES(&in, "SET", keys, vals, (char*[]){"string"}, v_lens, 1);
    KV_encode((uint8_t*)msg, 1024, &in);

    int total_pkts = batch_size * total_rounds;

    // 1. 连续发送 batch_size 个包（不等回包）
    for(int i=0;i<total_rounds;i++){
        for (int j = 0; j < batch_size; j++) {
            send_msg(connfd, msg, in.cons_);
        }
        //printf("发包\n");
        int expected_resp_len = in.cons_ > 0 ? 27 : 0; // 这里简化，默认每个回包 27 字节
        int total_expected_bytes = batch_size * expected_resp_len;
        int current_recv_bytes = 0;

        // 只要没收够预期的字节数，就一直读
        int received_bytes = 0; // 新增
        while (received_bytes < total_expected_bytes) {
            int rlen = recv_msg(connfd, result, MAX_MSG_LENGTH); 
            if (rlen <= 0) {
                printf("recv error or disconnected!\n");
                break;
            }
            received_bytes += rlen; // 累加真实收到的字节数
        }
        //printf("Expect: %d bytes, Actually recv: %d bytes\n", total_expected_bytes, received_bytes);
    }
    gettimeofday(&tv_end, NULL);
    int time_used = TIME_SUB_MS(tv_end, tv_begin);
    printf("Pipeline(total_pkts=%d) --> time_used: %d ms, qps: %d\n", 
           total_pkts, time_used, (total_pkts * 1000) / time_used);
}
void* Worker(void *arg){
    int connfd=connect_server(ip,port);
    //array_testcase_1w(connfd,10000);
    // 每次发 100 个，发 100 轮（总共 100 万个包）
    pipeline_testcase_1w(connfd, 1000, 1000);
    close(connfd);
    return 0;
}
int main(int argc,char *argv[]){
    if(argc!=3){
        return -1;
    }
    ip=argv[1];
    port=atoi(argv[2]);
    // int connfd=connect_server(ip,port);
    // //array_testcase_1w(connfd,10000);
    // // 每次发 100 个，发 100 轮（总共 100 万个包）
    // pipeline_testcase_1w(connfd, 1000, 1);
    // return 0;
    pthread_t t[2];
    for(int i=0;i<2;i++){
        pthread_create(&t[i],NULL,Worker,NULL);
    }
    for(int i=0;i<2;i++){
        pthread_join(t[i],NULL);
    }
    return 0;
}
