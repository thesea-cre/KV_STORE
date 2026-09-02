#include"kv_array.h"
#include"kvstore.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
int kv_array_create(kv_array_t *init){
    if(init==NULL){
        //printf("kv_array_create\n");
        return -1;
    }
    init->table=(kv_array_item_t* )kv_malloc(KV_MAX_ARRAY*(sizeof(kv_array_item_t)));
    if(init->table==NULL){
       // printf("kv_malloc failed\n");
        return -2;
    }
    init->total=0;
    init->idx=0;
    return 0;
}
int kv_array_destory(kv_array_t *init){
    if(init==NULL){
       // printf("kv_array_destory\n");
        return -1;
    }
    kv_free(init->table);
    return 0;
}
int kv_array_set(kv_array_t *init,char *key,char *value){
    if(init==NULL||key==NULL||value==NULL){
     //   printf("kv_array_set\n");
        return -1;
    }
   int i=0;
   int j=init->total;
   for(i=0;i<init->total;i++){
        if(strcmp(init->table[i].key,key)==0){
            break;
        }
        if(init->table[i].key==NULL){
            if(j>i){
                j=i;
            }
            continue;
        }
   }
   char *str=init->table[i].key;
   if(str!=NULL){
       // printf("key exist,permission deny\n");
        return 1;
   }
   int kcopy_len=strlen(key)+1;
   char *kcopy=(char *)kv_malloc(kcopy_len);
   if(kcopy==NULL){
       // printf("kv_malloc failed\n");
        return -2;
   }
   memset(kcopy,0,kcopy_len);
   strncpy(kcopy,key,strlen(key));

   char *kvalue=(char *)kv_malloc(strlen(value)+1);
   if(kvalue==NULL){
      //  printf("kvalue kv_malloc failed\n");
        return -2;
   }
   memset(kvalue,0,strlen(value)+1);
   strncpy(kvalue,value,strlen(value));
   if(j<KV_MAX_ARRAY){
        init->table[j].key=kcopy;
        init->table[j].value=kvalue;
        init->total++;
    }
    else{
       // printf("out of KV_MAX_ARRAR\n");
        return -1;
    }
    return 0;
}
char* kv_array_get(kv_array_t *init,char *key){
    if(init==NULL||key==NULL){
       // printf("kv_array_get\n");
        return NULL;
    }
    for(int i=0;i<init->total;i++){
        if(init->table[i].key==NULL){
            continue;
        }
        if(strcmp(init->table[i].key,key)==0){
            return init->table[i].value;
        }
    } 
    return NULL;
}
int kv_array_del(kv_array_t *init,char *key){
     if(init==NULL||key==NULL){
        //printf("kv_array_get\n");
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
            if(init->total-1==i){
                init->total--;
            }
            return 0;
        }
        
    }
    //printf("kv_array_del key not exist\n");
    return 1;
   
}
int kv_array_mod(kv_array_t *init,char *key,char *value){
 if(init==NULL||key==NULL||value==NULL){
       // printf("kv_array_set\n");
        return -1;
    }
   int i;
   int j=init->total;
   for(i=0;i<init->total;i++){
    if(init->table[i].key==NULL){
            if(j>i){
                j=i;
            }
            continue;
        }
        if(strcmp(init->table[i].key,key)==0){
            j=i;
            break;
        }
   }
   if(j==init->total){
       // printf("kv_array_mod key not exist\n");
        return 1;
   }
   char *kvalue=(char *)kv_malloc(strlen(value)+1);
   if(kvalue==NULL){
      //  printf("kvalue kv_malloc failed\n");
        return -2;
   }
   memset(kvalue,0,strlen(value)+1);
   strncpy(kvalue,value,strlen(value));
    init->table[j].value=kvalue;
    return 0;
}
int kv_array_exist(kv_array_t *init,char *key){
    if(init==NULL||key==NULL){
       // printf("kv_array_exist\n");
        return -1;
    }
   char *str = kv_array_get(init, key);
   //printf("exist key :%s\n",str);
	if (str!=NULL) {
		return 1; // 
	}
	return 0;
}
void kv_array_tra(kv_array_t *init){
    for(int i=0;i<init->total;i++){
        if(init->table[i].key!=NULL){
            printf("key:%s,value:%s\n",init->table[i].key,init->table[i].value);
        }
    }
    return;
}
// int main(){
//     kv_array_t *init=(kv_array_t *)malloc(sizeof(kv_array_t));
//     int ret=kv_array_create(init);
//     if(ret!=0){
//         printf("kv_array_create failed\n");
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
//                 ret=kv_array_set(init,key,value);
//                 if(ret<0)   printf("set failed\n");
//                 break;
//             case 1:
//                 scanf("%s",key);
//                 char * vl=kv_array_get(init,key);
//                 if(vl==NULL)   printf("get failed\n");
//                 else{
//                     printf("value:%s\n",vl);
//                 }
//                 break;
//             case 2:
//                 scanf("%s",key);
//                 ret=kv_array_del(init,key);
//                 if(ret<0)   printf("del failed\n");
//                 break;
//             case 3:
//                 scanf("%s %s",key,value);
//                 ret=kv_array_mod(init,key,value);
//                 if(ret<0)   printf("mod failed\n");
//                 break;
//             case 4:
//                 scanf("%s",key);
//                 ret=kv_array_exist(init,key);
//                 if(ret>=0){
//                     printf("key:%s,value:%s\n",init->table[ret].key,init->table[ret].value);
//                 }
//                 break;
//             case 5:
//                 kv_array_tra(init);
//                 break;
//             default:
//                 goto exit;
//         }

//     }
// exit:
//     kv_array_destory(init);
//     return 0;


    
// }