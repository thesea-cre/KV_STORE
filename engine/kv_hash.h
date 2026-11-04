#ifndef _KV_HASH_H_
#define _KV_HASH_H_
#define INIT_CAP 4
#define MAX_TABLE_SIZE 128
#include<stdint.h>
#include<stddef.h>
typedef struct KVNode{
    char *key;
    uint8_t *value;
    uint32_t value_len;
    struct KVNode *next;
}KVNode;
typedef struct {
    KVNode **table;
    int count;//哈希表存入键数量
    int cap;//底层数组容量
}KV_Map;
int KV_Map_create(KV_Map *hash);
void KV_Map_destory(KV_Map *hash);

int KV_Map_set(KV_Map *hash,char *key,uint8_t *value,uint32_t value_len);
int KV_Map_get(KV_Map *hash,char *key,uint8_t **value,uint32_t *value_eln);
int KV_Map_mod(KV_Map *hash,char *key,uint8_t *value,uint32_t value_len);
int KV_Map_del(KV_Map*hash,char *key);
int KV_Map_exist(KV_Map*hash,char *key);

void KV_Map_tra(KV_Map* hash,char **key,uint8_t **value,uint32_t *value_len);
int KV_Map_count(KV_Map *hash);















#endif