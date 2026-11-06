#include"adapt.h"
#include"kv_array.h"
#include"kv_hash.h"
#include"kv_rbtree.h"
#include"kv_skiplist.h"
#include"kvstore.h"
#include<pthread.h>
kv_array_t global_array;
KV_Map global_hash;
KV_Rbtree global_rbtree;
kv_skiplist_t global_skiplist;
pthread_mutex_t mutex_=PTHREAD_MUTEX_INITIALIZER;
int kv_set(char *key,uint8_t *value,uint32_t value_size){//做一个适配层，对外接口适用，隐藏核心层的选择，这样测试模块方便一点
    int ret;
    pthread_mutex_lock(&mutex_);
    #if ENGINE==ENGINE_ARRAY
    ret=kv_array_set(&global_array,key,value,value_size);
    #elif ENGINE==ENGINE_HASH
    ret=KV_Map_set(&global_hash,key,value,value_size);
    #elif ENGINE==ENGINE_RBTREE
    ret=KV_Rbtree_set(&global_rbtree,key,value_value_size);
    #elif ENGINE==ENGINE_SKIPLIST
    ret=kv_skiplist_set(&global_skiplist,key,value,value_size);
    #endif
    pthread_mutex_unlock(&mutex_);
    return ret;
}
int kv_get(char *key,uint8_t **value,uint32_t *value_size){
    int ret=0;
    pthread_mutex_lock(&mutex_);
    #if ENGINE==ENGINE_ARRAY
    ret=kv_array_get(&global_array,key,value,value_size);
    #elif ENGINE==ENGINE_HASH
    ret=KV_Map_get(&global_hash,key,value,value_size);
    #elif ENGINE==ENGINE_RBTREE
    ret=KV_Rbtree_get(&global_rbtree,key,value,value_size);
    #elif ENGINE==ENGINE_SKIPLIST
    ret=kv_skiplist_get(&global_skiplist,key,value,value_size);
    #endif
    pthread_mutex_unlock(&mutex_);
    return ret;
}
int kv_mod(char *key,uint8_t* value,uint32_t value_size){
    int ret;
    pthread_mutex_lock(&mutex_);
    #if ENGINE==ENGINE_ARRAY
    ret=kv_array_mod(&global_array,key,value,value_size);
    #elif ENGINE==ENGINE_HASH
    ret=KV_Map_mod(&global_hash,key,value,value_size);
    #elif ENGINE==ENGINE_RBTREE
    ret=KV_Rbtree_mod(&global_rbtree,key,value,value_size);
    #elif ENGINE==ENGINE_SKIPLIST
    ret=kv_skiplist_mod(&global_skiplist,key,value,value_size);
    #endif
    pthread_mutex_unlock(&mutex_);
    return ret;
}
int kv_del(char *key){
    int ret;
    pthread_mutex_lock(&mutex_);
    #if ENGINE==ENGINE_ARRAY
    ret=kv_array_del(&global_array,key);
    #elif ENGINE==ENGINE_HASH
    ret=KV_Map_del(&global_hash,key);
    #elif ENGINE==ENGINE_RBTREE
    ret=KV_Rbtree_del(&global_rbtree,key);
    #elif ENGINE==ENGINE_SKIPLIST
    ret=kv_skiplist_del(&global_skiplist,key);
    #endif
    pthread_mutex_unlock(&mutex_);
    return ret;
}
int kv_exist(char *key){
    int ret;
    pthread_mutex_lock(&mutex_);
    #if ENGINE==ENGINE_ARRAY
    ret=kv_array_exist(&global_array,key);
    #elif ENGINE==ENGINE_HASH
    ret=KV_Map_exist(&global_hash,key);
    #elif ENGINE==ENGINE_RBTREE
    ret=KV_Rbtree_exist(&global_rbtree,key);
    #elif ENGINE==ENGINE_SKIPLIST
    ret=kv_skiplist_exist(&global_skiplist,key);
    #endif
    pthread_mutex_unlock(&mutex_);
    return ret;
}
void kv_tra(char **key,uint8_t** value,uint32_t *value_len){
    pthread_mutex_lock(&mutex_);
    #if ENGINE==ENGINE_ARRAY
    kv_array_tra(&global_array,key,value,value_len);
    #elif ENGINE==ENGINE_HASH
    KV_Map_tra(&global_hash,key,value,value_len);
    #elif ENGINE==ENGINE_RBTREE
    KV_Rbtree_tra(&global_rbtree,key,value,value_size);
    #elif ENGINE==ENGINE_SKIPLIST
    kv_skiplist_tra(&global_skiplist,key,value,value_size);
    #endif
    pthread_mutex_unlock(&mutex_);
    return;
}
void init_kvengine(void){
    #if ENGINE==ENGINE_ARRAY
    memset(&global_array,0,sizeof(kv_array_t));
    kv_array_create(&global_array);
    #elif ENGINE==ENGINE_HASH
    memset(&global_hash,0,sizeof(KV_Map));
    KV_Map_create(&global_hash);
    #elif ENGINE==ENGINE_RBTREE
    memset(&global_rbtree,0,sizeof(KV_Rbtree));
    KV_Rbtree_create(&global_rbtree);
    #elif ENGINE==ENGINE_SKIPLIST
    memset(&global_skiplist,0,sizeof(kv_skiplist_t));
    kv_skiplist_create(&global_skiplist);
    #endif
}
void dest_kvengine(void){
    #if ENGINE==ENGINE_ARRAY
    kv_array_destory(&global_array);
    #elif ENGINE==ENGINE_HASH
    KV_Map_destory(&global_hash);
    #elif ENGINE==ENGINE_RBTREE
    KV_Rbtree_destory(&global_rbtree);
    #elif ENGINE==ENGINE_SKIPLIST
    kv_skiplist_destory(&global_skiplist);
    #endif
}
int kv_count(void){
    int ret;
    pthread_mutex_lock(&mutex_);
    #if ENGINE==ENGINE_ARRAY
    ret= kv_array_count(&global_array);
    #elif ENGINE==ENGINE_HASH
    ret= KV_Map_count(&global_hash);
    #elif ENGINE==ENGINE_RBTREE
    ret= KV_Rbtree_count(&global_rbtree);
    #elif ENGINE==ENGINE_SKIPLIST
    ret=kv_skiplist_count(&global_skiplist);
    #endif
    pthread_mutex_unlock(&mutex_);
    return ret;
}