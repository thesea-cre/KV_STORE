#ifndef _AOF_H_
#define _AOF_H_
#define AOF_READ_BUFFER_SIZE 4096
#include<stddef.h>
int aof_init(const char *file_path);
int aof_append(const char *pkt,int pkt_len);
int aof_recover(void);
int aof_fsync(void);
void aof_close(void);

 
#endif