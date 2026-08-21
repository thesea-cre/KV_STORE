
#include"server.h"
#include<stdlib.h>
extern int reactor_entry(unsigned int port);
int main(int argc,char *argv[]){
    if(argc!=2) return -1;
    unsigned int port=atoi(argv[1]);
    reactor_entry(port);
}