#ifndef _KV_UPLOAD_H_
#define _KV_UPLOAD_H_

#include <stdint.h>
#include <stddef.h>
#include <time.h>

/* ---- 可调参数 ---- */
#define KV_MAX_VALUE_SIZE   (64u * 1024u * 1024u)  /* 单个 value 上限：64MB */
#define KV_UPLOAD_SLOTS     4096                   /* 按 fd 索引的表长，别用 MAX_CONNECT */
#define KV_UPLOAD_MAX_CONN  4                      /* 同时进行的上传数，防 50MB×N 吃光内存 */
#define KV_UPLOAD_TIMEOUT   30                     /* 秒：这么久没新分片就丢弃 */
#define KV_UPLOAD_KEY_MAX   4096                   /* key 最大长度（含 '\0'） */

/* ---- 返回值（就是语义，调用方直接映射成回包） ---- */
#define KV_UP_MORE    0   /* 本片收好，继续；调用方不要回包 */
#define KV_UP_OK      1   /* 收齐且入库成功 */
#define KV_UP_EXIST   2   /* 收齐但 key 已存在 */
#define KV_UP_ERR    -1   /* 参数/状态错误 */

void kv_upload_init(void);                 /* 进程启动调一次 */
void kv_upload_abort(int fd);              /* 断连/出错时释放该 fd 的上下文 */
void kv_upload_abort_all(void);            /* 退出时清理 */
int  kv_upload_active(int fd);             /* 调试用：0 无上传，1 正在传 */

/*
 * 喂一片数据。
 *   fd     : 连接 fd（状态按 fd 存）
 *   key    : 逻辑 key，NUL 结尾
 *   total  : 整个 value 的总字节数（含第一个类型字节），全部分片必须一致
 *   offset : 本片在 value 中的起始偏移，必须等于已收字节数
 *   data   : 本片数据；data[0] 在 offset==0 时必须是 TYPE_xxx
 *   len    : 本片数据长度（> 0，且 offset+len <= total）
 * 返回 KV_UP_*。
 */
int  kv_upload_feed(int fd, const char *key,
                    uint32_t total, uint32_t offset,
                    const uint8_t *data, uint32_t len);

#endif