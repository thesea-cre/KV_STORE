#include<stdlib.h>
#include<stdio.h>
#include<string.h>
#include"aof.h"
#include<pthread.h>
#include<fcntl.h>
#include"kvstore.h"
#include <unistd.h>     // write、read、close、lseek、fsync
#include <fcntl.h>      // open、O_* 宏
#include <sys/stat.h>   // ftruncate、文件权限宏
#include <sys/types.h>  // size_t、ssize_t、off_t 等基础类型
int aof_init(const char *file_path){
    if(aof_fd>0){
        return 0;
    }
    if(file_path==NULL){
        return -1;
    }
    aof_fd=open(file_path,O_WRONLY|O_CREAT|O_APPEND,0664);
    if(aof_fd<0){
        perror("aof_init open failed");
        return -1;
    }
    pthread_mutex_init(&aof_mutex,NULL);

}
int aof_append(const char *pkt,int pkt_len){
    if(pkt==NULL||pkt_len<0||aof_fd<0){
        return -1;
    }
    pthread_mutex_lock(&aof_mutex);
    size_t total_write=0;
    while(total_write<pkt_len){
        size_t w=write(aof_fd,pkt+total_write,pkt_len-total_write);
        if(w<0){
            perror("aof_append write failed");
            pthread_mutex_unlock(&aof_mutex);
            return -1;
        }
        total_write+=w;
    }
    pthread_mutex_unlock(&aof_mutex);
    return total_write;

}
int aof_recover(void){
    int fd=open("aof_db",O_RDONLY);
    if(fd<0){
        return -1;
    }
    char buf[AOF_READ_BUFFER_SIZE]={0};
    int buf_len=0;
    while(1){
        int valid_offset=0;//最后一个有效包的偏移量
        ssize_t r=read(fd,buf+buf_len,AOF_READ_BUFFER_SIZE-buf_len);//一次性读固定个数据出来
        if (r < 0) {
            perror("aof_recover read failed");
            close(fd);
            return -1;
        }
        if (r == 0) {
            break; // 文件正常读完
        }
        buf_len+=r;
        if(buf_len<10){//包不是完整的
            break;
        }
        while(1){
            if(buf_len-valid_offset<10){//剩下的数据足不足以有头部
                break;
            }
            kv_header_t *header;
            kv_header_t h;
            memcpy(&h, buf + valid_offset, sizeof(kv_header_t));
            header=&h;
            if(header->magic!=KV_MAGIC){
                printf("aof_recover:bad magic at offset %d\n",valid_offset);
                close(fd);
                return -1;
            }
            int payload_len=header->length;
            if(payload_len<10){
                printf("aof_recover:invalid length %d\n",payload_len);
                close(fd);
                return -1;
            }
            if(buf_len-valid_offset<payload_len+10){//完整的包处理完了，剩下不完整的包
                break;
            }
            result res;
            int ret=0;
            memset(&res,0,sizeof(struct result));//buf里面不知道有几个包
            ret=kv_protocol(buf+valid_offset,10+payload_len,&res);//要一个包一个包地弄
            for (int i = 0; i < res.tlv_num; i++) {
                kv_free(res.tlv_array[i].value);
            }
            kv_free(res.tlv_array);
            valid_offset+=ret;
            if(valid_offset>=buf_len){//判断处理完一个包后，还有没有数据
                break;
            }
        }//已经处理了多个包，或者没有处理包，包不完整
    //处理了多个包
    if(valid_offset>0){
        memmove(buf,buf+valid_offset,buf_len-valid_offset);
        buf_len-=valid_offset;
        continue;
    }else{//最后的数据不足以一个完整包，可能是写日志时断电了
        //这里应该是要修改日志才行
    }
    }
    if (buf_len > 0) {
        off_t valid_len = lseek(fd, 0, SEEK_CUR) - buf_len;
        ftruncate(fd, valid_len);
    }
    close(fd);
}
int aof_fsync(void){
    if (aof_fd < 0) {
        return -1;
    }
    return fsync(aof_fd);
}
void aof_close(void){
    if (aof_fd >= 0) {
        fsync(aof_fd); // 关闭前强制刷盘
        close(aof_fd);
        aof_fd = -1;
        pthread_mutex_destroy(&aof_mutex);
    }
}
