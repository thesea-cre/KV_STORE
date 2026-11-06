#include"resp.h"
#include"queue.h"
#include"tool.h"
#include"kvproto.h"
#include<string.h>
#include<arpa/inet.h>

/* 单帧开销：
 *   普通 RES 帧 = 9(头) + 1(type) + 4(key_len) + 1(key) + 4(value_len) + (L-1)
 *                = 18 + L
 *   分片帧     = 9(头) + 1(type) + 4(key_len) + 1(key) + 4(value_len)
 *                + 8(total/offset) + n = 27 + n
 */
#define NORMAL_OVERHEAD 18u
#define CHUNK_OVERHEAD  27u

typedef struct {
    uint8_t *value;    /* [type][data]，所有权归队列 */
    uint32_t total;    /* 含类型字节 */
    uint32_t sent;
    uint8_t  stream;   /* 0=普通帧 1=分片流 */
} resp_item_t;

static Queue g_q[KV_RESP_SLOTS];

void kv_resp_init(void)
{
    for (int i = 0; i < KV_RESP_SLOTS; i++) queue_init(&g_q[i]);
}

int kv_resp_pending(int fd)
{
    if (fd < 0 || fd >= KV_RESP_SLOTS) return 0;
    return !queue_is_empty(&g_q[fd]);
}

void kv_resp_abort(int fd)
{
    resp_item_t *it;

    if (fd < 0 || fd >= KV_RESP_SLOTS) return;

    while ((it = (resp_item_t *)queue_poll(&g_q[fd])) != NULL) {
        if (it->value) kv_free(it->value);
        kv_free(it);
    }
}

void kv_resp_abort_all(void)
{
    for (int i = 0; i < KV_RESP_SLOTS; i++) kv_resp_abort(i);
}

/* 普通响应：交给 KV_encode 编成一帧 */
static int build_normal(uint8_t *buf, uint32_t cap, uint8_t *value, uint32_t total)
{
    RES r;

    memset(&r, 0, sizeof(r));
    r.op        = "RES";
    r.num       = 1;
    r.value     = &value;
    r.value_len = &total;

    KV_encode(buf, cap, &r);
    if (r.status == ERROR) return -1;
    return (int)r.cons_;
}

/* 分片响应：手工拼帧，value = [total][offset][data] */
static int build_chunk(uint8_t *buf, uint32_t cap,
                       const uint8_t *value, uint32_t total,
                       uint32_t offset, uint32_t n)
{
    uint32_t payload = 1 + 4 + 1 + 4 + 8 + n;
    uint32_t frame   = HEAD_LEN + payload;
    uint8_t *p = buf;

    if (frame > cap) return -1;

    uint16_t magic = htons(KV_MAGIC);
    uint16_t op    = htons(KV_CMD_RES);
    uint32_t plen  = htonl(payload);
    uint32_t one   = htonl(1);
    uint32_t vlen  = htonl(8 + n);
    uint32_t t     = htonl(total);
    uint32_t o     = htonl(offset);

    memcpy(p, &magic, 2); p += 2;
    *p++ = KV_VERSION;
    memcpy(p, &op, 2);    p += 2;
    memcpy(p, &plen, 4);  p += 4;

    *p++ = TYPE_CHUNK;                 /* TLV type：标识这是分片 */
    memcpy(p, &one, 4);   p += 4;      /* key_length = 1 */
    *p++ = 0;                          /* 占位 key */
    memcpy(p, &vlen, 4);  p += 4;      /* value_length = 8 + n */
    memcpy(p, &t, 4);     p += 4;
    memcpy(p, &o, 4);     p += 4;
    memcpy(p, value + offset, n);

    return (int)frame;
}

int kv_resp_begin(int fd, kvstore_t *ctx, uint32_t cap)
{
    int added = 0;

    if (fd < 0 || fd >= KV_RESP_SLOTS || !ctx) return -1;
    if (!queue_is_empty(&g_q[fd])) return -1;   /* 上一批还没发完 */

    for (int i = 0; i < ctx->num; i++) {
        resp_item_t *it;

        if (!ctx->res[i] || !ctx->res_len[i]) continue;

        it = (resp_item_t *)kv_malloc(sizeof(*it));
        if (!it) return -1;
        memset(it, 0, sizeof(*it));

        it->value  = ctx->res[i];
        it->total  = ctx->res_len[i];
        it->sent   = 0;
        it->stream = (it->total + NORMAL_OVERHEAD > cap) ? 1 : 0;

        if (queue_push(&g_q[fd], it) != 0) {
            kv_free(it);
            return -1;
        }

        ctx->res[i]     = NULL;   /* 所有权转移 */
        ctx->res_len[i] = 0;
        added++;
    }

    return added > 0 ? 0 : 1;
}

int kv_resp_next(int fd, uint8_t *buf, uint32_t cap)
{
    resp_item_t *it;
    int n;

    if (fd < 0 || fd >= KV_RESP_SLOTS) return -1;
    if (cap <= CHUNK_OVERHEAD) return -1;

    it = (resp_item_t *)queue_peek(&g_q[fd]);
    if (!it) return 0;

    if (!it->stream) {
        n = build_normal(buf, cap, it->value, it->total);
        if (n < 0) return -1;

        kv_free(it->value);
        it->value = NULL;
        (void)queue_poll(&g_q[fd]);
        kv_free(it);
        return n;
    }

    {
        uint32_t left = it->total - it->sent;
        uint32_t data = left;

        if (data > cap - CHUNK_OVERHEAD) data = cap - CHUNK_OVERHEAD;

        n = build_chunk(buf, cap, it->value, it->total, it->sent, data);
        if (n < 0) return -1;

        it->sent += data;
        if (it->sent >= it->total) {
            kv_free(it->value);
            it->value = NULL;
            (void)queue_poll(&g_q[fd]);
            kv_free(it);
        }
        return n;
    }
}
