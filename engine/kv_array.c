#include"kv_array.h"
#include"tool.h"

int kv_array_create(kv_array_t *init){
    if(init==NULL){
        return -1;
    }
    init->table=(kv_array_item_t* )kv_malloc(KV_MAX_ARRAY*(sizeof(kv_array_item_t)));
    if(init->table==NULL){
        return -2;
    }
    memset(init->table,0,KV_MAX_ARRAY*(sizeof(kv_array_item_t)));
    init->total=0;
    init->idx=0;
    return 0;
}
int kv_array_destory(kv_array_t *init){
    if(init==NULL){
        return -1;
    }
    for(int i=0;i<init->total;i++){//key/value是set时深拷贝出来的，先逐个释放，否则全泄漏
        if(init->table[i].key!=NULL){
            kv_free(init->table[i].key);
        }
        if(init->table[i].value!=NULL){
            kv_free(init->table[i].value);
        }
    }
    kv_free(init->table);
    return 0;
}
int kv_array_set(kv_array_t *init,char *key,uint8_t *value,uint32_t value_size){
    if(init==NULL||key==NULL||value==NULL||value_size==0){
        return -1;
    }
   int i=0;
   int j=init->total;//第一个空位下标；无空位则为total
   for(i=0;i<init->total && i<KV_MAX_ARRAY;i++){//i也要防越界：total虚增到128时扫描会跑出数组
        if(init->table[i].key==NULL){//先判NULL再strcmp：空位进strcmp会崩
            if(j>i){
                j=i;
            }
            continue;
        }
        if(strcmp(init->table[i].key,key)==0){
            return 1;//已存在，拒绝
        }
   }
   if(j>=KV_MAX_ARRAY){//满了（含total==128扫到数组尽头的情况），在malloc前拦截，免得kcopy白分配
        return -1;
   }
   int kcopy_len=strlen(key)+1;
   char *kcopy=(char *)kv_malloc(kcopy_len);
   if(kcopy==NULL){
        return -2;
   }
   memset(kcopy,0,kcopy_len);
   strncpy(kcopy,key,strlen(key));

   uint8_t *kvalue=(uint8_t *)kv_malloc(value_size);
   if(kvalue==NULL){
        return -2;
   }
   memset(kvalue,0,value_size);
   memcpy(kvalue,value,value_size);
   init->table[j].key=kcopy;
   init->table[j].value=kvalue;
   init->table[j].value_len=value_size;
   init->idx++;
   if(j==init->total){//只有插在末尾才推进total；插空位复用不推进（原实现无条件total++，total会虚增漂出界）
        init->total++;
   }
    return 0;
}

int kv_array_get(kv_array_t *init,char *key,uint8_t **value,uint32_t *value_size){
    if(init==NULL||key==NULL){
        return -1;
    }
    for(int i=0;i<init->total;i++){
        if(init->table[i].key==NULL){
            continue;
        }
        if(strcmp(init->table[i].key,key)==0){
            (*value)=init->table[i].value;
            (*value_size)=init->table[i].value_len;
            return 0;
        }
    } 
    return 1;
}

int kv_array_del(kv_array_t *init,char *key){
    if(init==NULL||key==NULL){
        return -1;
    }
    int i;
    for(i=0;i<init->total;i++){
        if(init->table[i].key==NULL){
            continue;
        }
        if(strcmp(init->table[i].key,key)==0){
            kv_free(init->table[i].key);
            init->table[i].key=NULL;
            kv_free(init->table[i].value);
            init->table[i].value=NULL;
            init->idx--;
            if(init->total-1==i){
                init->total--;
            }
            return 0;
        }
        
    }
    return 1;
   
}
int kv_array_mod(kv_array_t *init,char *key,uint8_t *value,uint32_t value_size){
 if(init==NULL||key==NULL||value==NULL||value_size==0){
        return -1;
    }
   int i;
   int found=-1;//mod只关心key在不在，空位与mod无关；不能用"第一个空位下标"当未找到标记，有空洞时会误判
   for(i=0;i<init->total && i<KV_MAX_ARRAY;i++){
        if(init->table[i].key==NULL){
            continue;
        }
        if(strcmp(init->table[i].key,key)==0){
            found=i;
            break;
        }
   }
   if(found<0){
        return 1;
   }
   char *kvalue=(char *)kv_malloc(value_size);
   if(kvalue==NULL){
        return -2;
   }
    memset(kvalue,0,value_size);
    memcpy(kvalue,value,value_size);
    kv_free(init->table[found].value);//替换前释放旧值，否则泄漏
    init->table[found].value=kvalue;
    init->table[found].value_len=value_size;
    return 0;
}
int kv_array_exist(kv_array_t *init,char *key){
    if(init==NULL||key==NULL){
        return -1;
    }
    uint8_t *value=NULL;
    uint32_t value_size=0;
    int ret = kv_array_get(init, key,&value,&value_size);
	if (ret==0) {
		return 1; // 
	}
	return 0;
}
void kv_array_tra(kv_array_t *init,char **key,uint8_t **value,uint32_t *value_size){
    int i=0;
    int num=0;
    while(i<init->total){
        if(init->table[i].key!=NULL){
            key[num]=init->table[i].key;
            value[num]=init->table[i].value;
            value_size[num]=init->table[i].value_len;
            num++;
        }
        i++;
    }
    return;
}
int kv_array_count(kv_array_t *init){
    return init->idx;
}
