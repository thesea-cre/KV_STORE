#ifndef _KV_RESP_H_
#define _KV_RESP_H_

#include<stdint.h>
#include"kvstore.h"

/*
 * 待发响应队列（每个连接一个）
 *
 * 背景：一次请求可能产生多条响应，或者一条响应本身就是几十 MB 的 value，
 * 都放不进 1KB 的 wbuffer。这里把响应转成"一段一段可取的流"：
 *   - 小响应：整帧编码，一次取完
 *   - 大响应：切成 TYPE_CHUNK 分片，每片一帧
 * 网络层在 wlength 发空之后调 kv_resp_next() 取下一段。
 *
 * 所有权：kv_resp_begin() 成功入队的条目归队列所有，队列在发完/中断时负责释放。
 */

#define KV_RESP_SLOTS 4096

/* 把本次请求产生的响应（ctx->res / ctx->res_len）转成队列。
 * cap 为单帧可用空间（通常等于 BUFFER_SIZE）。
 * 返回 0=已入队，1=没有可发的响应，-1=失败 */
int  kv_resp_begin(int fd,kvstore_t *ctx,uint32_t cap);

/* 取下一段要发的字节写入 buf。
 * 返回 >0 字节数；0 队列空；<0 出错 */
int  kv_resp_next(int fd,uint8_t *buf,uint32_t cap);

int  kv_resp_pending(int fd);
void kv_resp_init(void);
void kv_resp_abort(int fd);        /* 断连/出错时释放该连接所有待发数据 */
void kv_resp_abort_all(void);

#endif
