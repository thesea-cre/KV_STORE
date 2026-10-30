#include<stdio.h>
#include<pthread.h>
#include<stdlib.h>
#include<time.h>
#include<string.h>
#include<stdarg.h>
#include"log.h"

static pthread_mutex_t log_mutex;
static log_level_t level=LOG_INFO;
static FILE* log_fp=NULL;
static const char * log_level_str[]={
    "DEBUG","INFO","WARN","ERROR","FATAL"
};
void log_init(const char *logfile,log_level_t log_level){
    level=log_level;
    pthread_mutex_init(&log_mutex,NULL);
    if(logfile&&strlen(logfile)>0){
        log_fp=fopen(logfile,"a");
    }
    if(!log_fp){
        log_fp=stdout;
    }
}
void log_close(){
    if(log_fp&&log_fp!=stdout){
        fclose(log_fp);
    }
    pthread_mutex_destroy(&log_mutex);
}
void log_write(log_level_t lev,const char *file,int line,const char *fmt,...){
    if(lev<level){
        return;
    }
    pthread_mutex_lock(&log_mutex);
    time_t now=time(NULL);
    struct tm* time_now=localtime(&now);
    char time_buf[32];
    strftime(time_buf,32,"%Y-%m-%d %H:%M:%S",time_now);
    fprintf(log_fp,"[%s][%s][tid:%lu][%s:%d]",time_buf,log_level_str[lev],(unsigned long)pthread_self(),file,line);
    va_list ap;
    va_start(ap,fmt);
    vfprintf(log_fp,fmt,ap);
    va_end(ap);
    fprintf(log_fp,"\n");
    fflush(log_fp);
    pthread_mutex_unlock(&log_mutex);
}

