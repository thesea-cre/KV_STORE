#include "upload.h"
#include "tool.h"      /* kv_malloc / kv_free */
#include "adapt.h"     /* kv_set */
#include "kvstore.h"   /* AOF / SYNC 等编译开关 */
#include <stdio.h>
#include <string.h>

#if AOF
#include "aof.h"
/* 这三个都定义在 Core/kvstore.c；huge_buf 是数组不是指针，extern 必须写 char[] 才正确 */
extern AOFcontext_ctx *ctx;
extern char  huge_buf[];
extern int   huge_buf_len;
extern int   huge_num;
#endif

#if SYNC
/* 分片收齐后按一条普通 SET 记录复制给从节点 */
extern void sync(char *op,char *key,uint8_t *value,uint32_t value_size);
extern int  s_or_c;
#endif

typedef struct {
    int       in_use;
    char     *key;          /* 拷贝一份，含 '\0' */
    uint32_t  total;
    uint32_t  received;
    uint8_t  *buf;          /* kv_malloc(total) */
    time_t    last_active;
} upload_t;

static upload_t g_up[KV_UPLOAD_SLOTS];
static int      g_active_cnt = 0;

static void up_free(upload_t *u)
{
    if (!u->in_use) return;
    kv_free(u->buf);
    kv_free(u->key);
    memset(u, 0, sizeof(*u));
    g_active_cnt--;
}

static int up_expired(const upload_t *u, time_t now)
{
    return u->in_use && (now - u->last_active) > KV_UPLOAD_TIMEOUT;
}

/* 槽位/配额不够时，先回收已过期的上传 */
static void up_reap_expired(time_t now)
{
    for (int i = 0; i < KV_UPLOAD_SLOTS; i++) {
        if (up_expired(&g_up[i], now)) up_free(&g_up[i]);
    }
}

void kv_upload_init(void)
{
    memset(g_up, 0, sizeof(g_up));
    g_active_cnt = 0;
}

void kv_upload_abort(int fd)
{
    if (fd < 0 || fd >= KV_UPLOAD_SLOTS) return;
    up_free(&g_up[fd]);
}

void kv_upload_abort_all(void)
{
    for (int i = 0; i < KV_UPLOAD_SLOTS; i++) {
        if (g_up[i].in_use) up_free(&g_up[i]);
    }
}

int kv_upload_active(int fd)
{
    if (fd < 0 || fd >= KV_UPLOAD_SLOTS) return 0;
    return g_up[fd].in_use;
}

int kv_upload_feed(int fd, const char *key,
                   uint32_t total, uint32_t offset,
                   const uint8_t *data, uint32_t len)
{
    upload_t *u;
    time_t    now;

    /* ---------- 1. 参数校验 ---------- */
    if (fd < 0 || fd >= KV_UPLOAD_SLOTS)            return KV_UP_ERR;
    if (!key || !data || len == 0)                  return KV_UP_ERR;
    if (total == 0 || total > KV_MAX_VALUE_SIZE)    return KV_UP_ERR;
    if (offset > total || len > total - offset)     return KV_UP_ERR;
    if (strlen(key) + 1 > KV_UPLOAD_KEY_MAX)        return KV_UP_ERR;

    now = time(NULL);
    u   = &g_up[fd];

    /* 这个 fd 上一轮若已超时，先回收 */
    if (up_expired(u, now)) up_free(u);

    /* ---------- 2. 第一片：建上下文 ---------- */
    if (offset == 0) {
        size_t klen = strlen(key) + 1;

        if (u->in_use) up_free(u);          /* 没传完又来新的：丢弃旧的，重新开始 */

        if (g_active_cnt >= KV_UPLOAD_MAX_CONN) {
            up_reap_expired(now);
            if (g_active_cnt >= KV_UPLOAD_MAX_CONN) return KV_UP_ERR;
        }

        u->buf = (uint8_t *)kv_malloc(total);   /* 必须 kv_malloc，不能 malloc */
        if (!u->buf) return KV_UP_ERR;

        u->key = (char *)kv_malloc(klen);
        if (!u->key) {
            kv_free(u->buf);
            u->buf = NULL;
            return KV_UP_ERR;
        }
        memcpy(u->key, key, klen);

        u->in_use   = 1;
        u->total    = total;
        u->received = 0;
        g_active_cnt++;
    }
    /* ---------- 3. 中间/末片：必须和上下文严格对齐 ---------- */
    else {
        if (!u->in_use)                   return KV_UP_ERR;  /* 跳片，或上下文已超时 */
        if (u->total != total)            return KV_UP_ERR;  /* 总长变了 */
        if (strcmp(u->key, key) != 0)     return KV_UP_ERR;  /* 半路换 key */
        if (offset != u->received)        return KV_UP_ERR;  /* 必须连续 */
    }

    /* ---------- 4. 收数据 ---------- */
    memcpy(u->buf + offset, data, len);
    u->received   += len;
    u->last_active = now;

    if (u->received < u->total) return KV_UP_MORE;

    /* ---------- 5. 收齐：先写 AOF，再入库（与普通 SET 的顺序一致） ---------- */
    {
#if AOF
        aof_append(ctx, "SET", u->key, (char *)u->buf, u->total,
                   huge_buf, &huge_buf_len, &huge_num);
#endif
#if SYNC
        if (s_or_c == 0) {   /* 主节点才往外复制；从节点应用时不能再转发 */
            sync("SET", u->key, u->buf, u->total);
        }
#endif
        int ret = kv_set(u->key, u->buf, u->total);   /* 内部会加 adapt.c 的锁 */
        up_free(u);
        if (ret == 0) return KV_UP_OK;
        if (ret > 0)  return KV_UP_EXIST;
        return KV_UP_ERR;
    }
}