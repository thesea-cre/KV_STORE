#include<stdio.h>
#include<pthread.h>
#include<stdlib.h>
#include<time.h>
#include<string.h>
#include<stdarg.h>
typedef enum{
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
}log_level_t;
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
void log_write(log_level_t lev,const char* file,int line,const char* fmt,...);
#define LOG_DEBUG(fmt,...) log_write(LOG_DEBUG,__FILE__,__LINE__,fmt,##__VA_ARGS__)
#define LOG_INFO(fmt,...) log_write(LOG_INFO,__FILE__,__LINE__,fmt,##__VA_ARGS__)
#define LOG_WARN(fmt,...) log_write(LOG_WARN,__FILE__,__LINE__,fmt,##__VA_ARGS__)
#define LOG_ERROR(fmt,...) log_write(LOG_ERROR,__FILE__,__LINE__,fmt,##__VA_ARGS__)
#define LOG_FATAL(fmt,...) log_write(LOG_FATAL,__FILE__,__LINE__,fmt,##__VA_ARGS__)
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
int main(){
    log_init("logfile",LOG_INFO);
    LOG_INFO("alloc size=%zu",128);
    log_close();
    return 0;
}

