#include"adapt.h"
#include"kv_array.h"
#include"kv_hash.h"
#include"kv_rbtree.h"
#include"kvstore.h"
#include<pthread.h>
kv_array_t global_array;
KV_Map global_hash;
KV_Rbtree global_rbtree;
pthread_mutex_t mutex_=PTHREAD_MUTEX_INITIALIZER;
int kv_set(char *key,uint8_t *value,uint32_t value_size){//做一个适配层，对外接口适用，隐藏核心层的选择，这样测试模块方便一点
    int ret;
    pthread_mutex_lock(&mutex_);
    #if ENGINE==ENGINE_ARRAY
    ret=kv_array_set(&global_array,key,value,value_size);
    #elif ENGINE==ENGINE_HASH
    ret=KV_Map_set(&global_hash,key,value);
    #elif ENGINE==ENGINE_RBTREE
    ret=KV_Rbtree_set(&global_rbtree,key,value);
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
    value=KV_Map_get(&global_hash,key);
    #elif ENGINE==ENGINE_RBTREE
    value=KV_Rbtree_get(&global_rbtree,key);
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
    ret=KV_Map_mod(&global_hash,key,value);
    #elif ENGINE==ENGINE_RBTREE
    ret=KV_Rbtree_mod(&global_rbtree,key,value);
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
    #endif
    pthread_mutex_unlock(&mutex_);
    return ret;
}
void kv_tra(char **key,uint8_t** value,uint32_t *value_len){
    pthread_mutex_lock(&mutex_);
    #if ENGINE==ENGINE_ARRAY
    kv_array_tra(&global_array,key,value,value_len);
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
    #endif
}
void dest_kvengine(void){
    #if ENGINE==ENGINE_ARRAY
    kv_array_destory(&global_array);
    #elif ENGINE==ENGINE_HASH
    KV_Map_destory(&global_hash);
    #elif ENGINE==ENGINE_RBTREE
    KV_Rbtree_destory(&global_rbtree);
    #endif
}
int kv_count(void){
    int ret;
    pthread_mutex_lock(&mutex_);
    #if ENGINE==ENGINE_ARRAY
    ret= kv_array_count(&global_array);
    #elif ENGINE==ENGINE_HASH
    
    #elif ENGINE==ENGINE_RBTREE
    
    #endif
    pthread_mutex_unlock(&mutex_);
    return ret;
}