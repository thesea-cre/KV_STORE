#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include "log.h"

// 这是一个测试多线程并发的函数
void* worker_thread(void* arg) {
    int thread_id = *(int*)arg;
    
    // 模拟线程工作
    for (int i = 0; i < 3; i++) {
        LOG_DEBUG("线程 %d 在打印 DEBUG 日志，循环 %d", thread_id, i);
        LOG_INFO("线程 %d 做了一些正经事，循环 %d", thread_id, i);
        usleep(1000); // 睡 1 毫秒，模拟业务
    }
    return NULL;
}

int main() {
    // 1. 初始化日志
    // 参数1：日志文件名（如果传 NULL，就会输出到终端 stdout）
    // 参数2：日志级别（这里设为 LOG_INFO，意味着 DEBUG 会被过滤掉）
    log_init("my_server.log", LOG_INFO);

    printf("--- 开始测试日志模块 ---\n");

    // 2. 测试基础输出和格式化
    LOG_INFO("服务器正在启动...");
    LOG_WARN("内存使用率达到了 %d%%，请注意！", 80);
    LOG_ERROR("连接数据库失败，错误码：%d", 404);
    
    // 测试被过滤的 DEBUG 日志（因为 init 时 level 是 INFO，这句不会输出）
    LOG_DEBUG("这是一句 DEBUG 日志，你在日志文件里绝对看不到我！");

    // 3. 测试多线程并发写入（看看会不会乱序、崩溃）
    printf("--- 开始多线程并发写入测试 ---\n");
    pthread_t t1, t2, t3;
    int id1 = 1, id2 = 2, id3 = 3;

    pthread_create(&t1, NULL, worker_thread, &id1);
    pthread_create(&t2, NULL, worker_thread, &id2);
    pthread_create(&t3, NULL, worker_thread, &id3);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    pthread_join(t3, NULL);

    // 4. 关闭日志，释放资源
    LOG_INFO("服务器即将关闭，清理日志资源。");
    log_close();
    
    printf("--- 测试结束！请查看 my_server.log 文件 ---\n");
    return 0;
}