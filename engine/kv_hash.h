#ifndef _KV_HASH_H_
#define _KV_HASH_H_
#define INIT_CAP 4
#define MAX_TABLE_SIZE 128
typedef struct KVNode{
    char *key;
    char *value;
    struct KVNode *next;
}KVNode;
typedef struct {
    KVNode **table;
    int count;//哈希表存入键数量
    int cap;//底层数组容量
}KV_Map;
int KV_Map_create(KV_Map *hash);
void KV_Map_destory(KV_Map *hash);
int KV_Map_set(KV_Map *hash,char *key,char *value);
char *KV_Map_get(KV_Map *hash,char *key);
int KV_Map_mod(KV_Map *hash,char *key,char *value);
int KV_Map_del(KV_Map*hash,char *key);
int KV_Map_exist(KV_Map*hash,char *key);















#endif