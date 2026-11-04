#include"kv_hash.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include"tool.h"
#include"log.h"
static int _hash(char *key,int size){
   if(!key) return -1;
   int sum=0;
   int i=0;
   while(key[i]!=0){
        sum+=key[i];
        i++;
   }
   return sum%size;
}
static KVNode * create_node(char *key,uint8_t *value,uint32_t value_len){
    if(key==NULL||value==NULL||value_len==0){
        LOG_ERROR("create_node param is illegal ");
        return NULL;
    } 
    KVNode *node=(KVNode *)kv_malloc(sizeof(KVNode));
    if(!node) {
        LOG_ERROR("kv_malloc failed");
        return NULL;
    }
    char *kcopy=(char *)kv_malloc(strlen(key)+1);
    if(!kcopy){
        LOG_ERROR("kv_malloc failed");
        kv_free(node);
        return NULL;
    }
    memset(kcopy,0,strlen(key)+1);
    strncpy(kcopy,key,strlen(key));
    node->key=kcopy;
    
    uint8_t *kvalue=(uint8_t *)kv_malloc(value_len);
    if(!kvalue){
        LOG_ERROR("kv_malloc failed");
        kv_free(node);
        kv_free(kcopy);
        return NULL;
    }

    memset(kvalue,0,value_len);
    memcpy(kvalue,value,value_len);
    node->value=kvalue;
    node->value_len=value_len;
    node->next=NULL;
    return node;

}

int KV_Map_create(KV_Map *hash){
    if(hash==NULL){
        LOG_ERROR("KV_Map_create parma is NULL");
        return -1;
    } 
    hash->cap=MAX_TABLE_SIZE;
    hash->table=(KVNode **)kv_malloc(sizeof(KVNode *)*MAX_TABLE_SIZE);
    if(!hash->table){
        LOG_ERROR("kv_malloc failed");
        return -1;
    }
    memset(hash->table,0,sizeof(KVNode *)*MAX_TABLE_SIZE);

    for(int i=0;i<hash->cap;i++){//可能会泄漏
        hash->table[i]=(KVNode *)kv_malloc(sizeof(KVNode));
        memset(hash->table[i],0,sizeof(KVNode));
        hash->table[i]->next=NULL;
    }

    LOG_DEBUG("hash->cap:%d\n",hash->cap);

    hash->count=0;
    return 0;
}
void KV_Map_destory(KV_Map* hash){
    if(!hash){
        LOG_ERROR("KV_Map_destory hash=NULL");
        return;
    }
    for(int i=0;i<hash->cap;i++){
        KVNode *head=hash->table[i];
        KVNode *node=head->next;
        while(node!=NULL){
            KVNode *tmp=node;
            node=node->next;
            kv_free(tmp->key);//key/value是深拷贝的，先释放再释放节点
            kv_free(tmp->value);
            kv_free(tmp);
        }
        kv_free(head);
    }
    kv_free(hash->table);
}
int KV_Map_set(KV_Map*hash,char *key,uint8_t  *value,uint32_t value_len){
    if(hash==NULL||key==NULL||value==NULL) return -1;
    int idx=_hash(key,hash->cap);
    KVNode *head=hash->table[idx];
    KVNode *node=head->next;
    if(node==NULL){
        KVNode * cor=create_node(key,value,value_len);
        cor->next=head->next;
        head->next=cor;
        hash->count++;
        return 0;
    }else {
        while(node!=NULL){
            if(strcmp(node->key,key)==0){
                return 1;
            }
            node=node->next;
        }
        KVNode *cor=create_node(key,value,value_len);
        cor->next=head->next;//头插
        head->next=cor;
        hash->count++;//链上追加也要计数（原实现漏了，只统计了空桶首插）
    }
    return 0;
}
int KV_Map_get(KV_Map* hash,char *key,uint8_t **value,uint32_t *value_len){
    if(hash==NULL||key==NULL) return -1;
    int idx=_hash(key,hash->cap);
    KVNode *head=hash->table[idx];
    KVNode *node=head->next;
    if(node==NULL){
        return 1;
    }else{
        while(node!=NULL){
            if(strcmp(node->key,key)==0){
                *value=node->value;
                (*value_len)=node->value_len;
                return 0;
            }
            node=node->next;//没找到就沿链前进，否则node不变死循环
        }
    }
    return 1;
}
int KV_Map_del(KV_Map*hash,char *key){
    if(hash==NULL||key==NULL) return -1;
    int idx=_hash(key,hash->cap);
    KVNode *head=hash->table[idx];
    KVNode *node=head->next;
    KVNode* pre=head;
    if(node==NULL){
        return 1;
    }else{
        while(node!=NULL){
            if(strcmp(node->key,key)==0){
                pre->next=node->next;
                kv_free(node->key);//key/value是深拷贝的，先释放再释放节点
                kv_free(node->value);
                kv_free(node);
                hash->count--;
                return 0;
            }else{
                pre=node;
                node=node->next;
            }
        }
        return 1;
    }
}
int KV_Map_mod(KV_Map*hash,char *key,uint8_t*value,uint32_t value_len){
    if(hash==NULL||key==NULL||value==NULL) return -1;
    int idx=_hash(key,hash->cap);
    KVNode *head=hash->table[idx];
    KVNode *node=head->next;
    if(node==NULL){
        return 1;
    }else{
        while(node!=NULL){
            if(strcmp(node->key,key)==0){
                //新值可能比旧值长，必须free旧的重新malloc，直接strncpy进旧缓冲区会堆溢出
                kv_free(node->value);
                node->value=(uint8_t *)kv_malloc(value_len);
                if(node->value==NULL){
                    LOG_ERROR("kv_malloc failed");
                    return -2;
                }
                memset(node->value,0,value_len);
                
                memcpy(node->value,value,value_len);
                node->value_len=value_len;
                return 0;
            }
            node=node->next;
        }
    }
    return 1;
}
int KV_Map_exist(KV_Map*hash,char *key){
    if(hash==NULL||key==NULL) return -1;
    int idx=_hash(key,hash->cap);
    KVNode*head=hash->table[idx];
    KVNode *node=head->next;
    if(node==NULL){
        return 0;
    }else{
        while(node!=NULL){
            if(strcmp(node->key,key)==0){
                return 1;
            }
            node=node->next;
        }
    }
    return 0;
}
void KV_Map_tra(KV_Map*hash,char **key,uint8_t **value,uint32_t *value_len){
    if(hash==NULL) return;
    for(int i=0;i<hash->cap;i++){
        KVNode* head=hash->table[i];
        KVNode *node=head->next;
        if(node==NULL){
            continue;
        }else{
            while(node!=NULL){
                LOG_DEBUG("key:%s,value:%s\n",node->key,node->value);
                key[i]=node->key;
                value[i]=node->value;
                value_len[i]=node->value_len;
                node=node->next;
            }
        }
    }
    return;
}
int KV_Map_count(KV_Map *hash){
    return hash->count;
}


