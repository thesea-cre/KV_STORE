#include"kv_hash.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<kvstore.h>
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
static KVNode * create_node(char *key,char *value){
    if(key==NULL||value==NULL) return NULL;
    KVNode *node=(KVNode *)kv_malloc(sizeof(KVNode));
    if(!node) return NULL;

    char *kcopy=(char *)kv_malloc(strlen(key)+1);
    memset(kcopy,0,strlen(key)+1);
    strncpy(kcopy,key,strlen(key));
    node->key=kcopy;
    
    char *kvalue=(char *)kv_malloc(strlen(value)+1);
    memset(kvalue,0,strlen(value)+1);
    strncpy(kvalue,value,strlen(value));
    node->value=kvalue;

    node->next=NULL;
    return node;
}

int KV_Map_create(KV_Map *hash){
    if(hash==NULL) return -1;
    hash->cap=MAX_TABLE_SIZE;
    hash->table=(KVNode **)kv_malloc(sizeof(KVNode *)*MAX_TABLE_SIZE);
    if(hash->table==NULL) return -1;
    for(int i=0;i<hash->cap;i++){
        hash->table[i]=(KVNode *)kv_malloc(sizeof(KVNode));
        hash->table[i]->next=NULL;
    }
    printf("hash->cap:%d\n",hash->cap);
    hash->count=0;
    return 0;
}
void KV_Map_destory(KV_Map* hash){
    if(!hash) return;
    for(int i=0;i<hash->cap;i++){
        KVNode *head=hash->table[i];
        KVNode *node=head->next;
        while(node!=NULL){
            KVNode *tmp=node;
            node=node->next;
            hash->table[i]=node;
            kv_free(tmp);
        }
        kv_free(head);
    }
    kv_free(hash->table);
}
int KV_Map_set(KV_Map*hash,char *key,char *value){
    if(hash==NULL||key==NULL||value==NULL) return -1;
    int idx=_hash(key,hash->cap);
    KVNode *head=hash->table[idx];
    KVNode *node=head->next;
    if(node==NULL){
        KVNode * cor=create_node(key,value);
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
        KVNode *cor=create_node(key,value);
        cor->next=head->next;
        head->next=cor;
    }
    return 0;
}
char * KV_Map_get(KV_Map* hash,char *key){
    if(hash==NULL||key==NULL) return NULL;
    int idx=_hash(key,hash->cap);
    KVNode *head=hash->table[idx];
    KVNode *node=head->next;
    if(node==NULL){
        return NULL;
    }else{
        while(node!=NULL){
            if(strcmp(node->key,key)==0){
                return node->value;
            }
        }
    }
    return NULL;
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
int KV_Map_mod(KV_Map*hash,char *key,char*value){
    if(hash==NULL||key==NULL||value==NULL) return -1;
    int idx=_hash(key,hash->cap);
    KVNode *head=hash->table[idx];
    KVNode *node=head->next;
    if(node==NULL){
        return 1;
    }else{
        while(node!=NULL){
            if(strcmp(node->key,key)==0){
                strncpy(node->value,value,strlen(value));
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
void KV_Map_traverse(KV_Map*hash){
    if(hash==NULL) return;
    printf("hash count:%d\n",hash->count);
    for(int i=0;i<hash->cap;i++){
        KVNode* head=hash->table[i];
        KVNode *node=head->next;
        if(node==NULL){
            continue;
        }else{
            while(node!=NULL){
                printf("key:%s,value:%s\n",node->key,node->value);
                node=node->next;
            }
        }
    }
    return;
}
// int main(){
//     KV_Map hash;
//     int ret=KV_Map_create(&hash);
//     if(ret!=0){
//         printf("KV_Map_create failed\n");
//         return -1;
//     }
//     char key[128]={0};
//     char value[128]={0};
//     while(1){
//         int opt;
//         printf("输入命令：\n");
//         printf("0:set key value\n");
//         printf("1:get key\n");
//         printf("2:del key \n");
//         printf("3:mod key value\n");
//         printf("4:exist key \n");
//         printf("5:tra\n");
//         scanf("%d",&opt);
//         printf("输入key,value\n");
//         switch(opt){
//             case 0:
//                 scanf("%s %s",key,value);
//                 ret=KV_Map_set(&hash,key,value);
//                 if(ret<0)   printf("set failed\n");
//                 break;
//             case 1:
//                 scanf("%s",key);
//                 char * vl=KV_Map_get(&hash,key);
//                 if(vl==NULL)   printf("get failed\n");
//                 else{
//                     printf("value:%s\n",vl);
//                 }
//                 break;
//             case 2:
//                 scanf("%s",key);
//                 ret=KV_Map_del(&hash,key);
//                 if(ret<0)   printf("del failed\n");
//                 break;
//             case 3:
//                 scanf("%s %s",key,value);
//                 ret=KV_Map_mod(&hash,key,value);
//                 if(ret<0)   printf("mod failed\n");
//                 break;
//             case 4:
//                 scanf("%s",key);
//                 ret=KV_Map_exist(&hash,key);
//                 if(ret==0){
//                     printf("key:%s is exist\n",key);
//                 }else{
//                     printf("key %s is not exist\n",key);
//                 }
//                 break;
//             case 5:
//                 KV_Map_traverse(&hash);
//                 break;
//             default:
//                 goto exit;
//         }

//     }
// exit:
//     KV_Map_destory(&hash);
//     return 0;


    
// }

