#ifndef _ADAPT_H_
#define _ADAPT_H_
#include<stdint.h>
#include<stddef.h>
int kv_set(char *key,uint8_t *value,uint32_t value_size);
int kv_get(char *key,uint8_t **value,uint32_t *value_size);
int kv_mod(char *key,uint8_t* value,uint32_t value_size);
int kv_del(char *key);
int kv_exist(char *key);
void kv_tra(char **key,uint8_t** value,uint32_t *value_len);
void init_kvengine(void);
void dest_kvengine(void);
int kv_count(void);
#endif