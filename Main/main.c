#include"server.h"
static Mempool_Manager *msg;
int main(int argc,char *argv[]){
    if(argc!=2) return -1;
    #if 1
    msg=Managerpool_GetInstance();
    #endif
    unsigned int port=atoi(argv[1]);
    init_kvengine();
    #if NETWORK_SELECT == ENABLE_REACTOR
    reactor_entry(port);
    #elif NETWORK_SELECT == ENABLE_PROACTOR
    uring_start(port);
    #elif NETWORK_SELECT == ENABLE_NTYCO
    nytco(port);
    #endif
    dest_kvengine();
}