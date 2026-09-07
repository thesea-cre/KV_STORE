#include"server.h"

int main(int argc,char *argv[]){
    if(argc!=2) return -1;
    #if 1
    mempool=Managerpool_GetInstance();
    #endif
    aof_init("aof_db");
    unsigned int port=atoi(argv[1]);
    init_kvengine();
    aof_recover();
    #if NETWORK_SELECT == ENABLE_REACTOR
    reactor_entry(port);
    #elif NETWORK_SELECT == ENABLE_PROACTOR
    uring_start(port);
    #elif NETWORK_SELECT == ENABLE_NTYCO
    nytco(port);
    #endif
    aof_close();
    dest_kvengine();
 

}