#ifndef _KV_ARRAY_H_
#define _KV_ARRAY_H_
#define KV_MAX_ARRAY 128
typedef struct kv_array_item_s{
    char * key;
    char * value;
}kv_array_item_t;
typedef struct kv_array_s{
    kv_array_item_t * table;
    int total;
    int idx;
}kv_array_t;
int kv_array_create(kv_array_t *init);
int kv_array_destory(kv_array_t *init);
int kv_array_set(kv_array_t *init,char *key,char*value);
char* kv_array_get(kv_array_t *init,char *key);
int kv_array_del(kv_array_t *init,char *key);
int kv_array_mod(kv_array_t *init,char *key,char*value);
int kv_array_exist(kv_array_t *init,char *key);
void kv_array_tra(kv_array_t *init);
#endif