#ifndef _KV_ARRAY_H_
#define _KV_ARRAY_H_
#define KV_MAX_ARRAY 128
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<stdint.h>
#include<stddef.h>
typedef struct kv_array_item_s{
    char * key;
    uint8_t * value;
    uint32_t value_len;
}kv_array_item_t;
typedef struct kv_array_s{
    kv_array_item_t * table;
    int total;
    int idx;
}kv_array_t;
int kv_array_create(kv_array_t *init);
int kv_array_destory(kv_array_t *init);

int kv_array_set(kv_array_t *init,char *key,uint8_t *value,uint32_t value_len);
int kv_array_get(kv_array_t *init,char *key,uint8_t **value,uint32_t *value_size);
int kv_array_del(kv_array_t *init,char *key);
int kv_array_mod(kv_array_t *init,char *key,uint8_t *value,uint32_t value_size);
int kv_array_exist(kv_array_t *init,char *key);
void kv_array_tra(kv_array_t *init,char **key,uint8_t **value,uint32_t *value_size);
int kv_array_count(kv_array_t *init);
#endif