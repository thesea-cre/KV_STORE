#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<arpa/inet.h>
#include<sys/time.h>
#include<sys/socket.h>
#define MAX_MSG_LENGTH 128
#define TIME_SUB_MS(tv1, tv2)  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)
int recv_msg(int fd,char *msg,int length){
    int ret=recv(fd,msg,length,0);
    if(ret<0){
        perror("recv");
        return -1;
    }
    return ret;
}
int send_msg(int fd,char *msg,int length){
    int ret=send(fd,msg,length,0);
    if(ret<0){
        perror("recv");
        return -1;
    }
    return ret;
}
int connect_server(const char *ip,unsigned short port){
    int connfd=socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in addr;
    addr.sin_family=AF_INET;
    addr.sin_port=htons(port);
    addr.sin_addr.s_addr=inet_addr(ip);
    if(0!=connect(connfd,(struct sockaddr*)&addr,sizeof(struct sockaddr_in))){
        perror("connect");
        exit(1);
    }
    return connfd;

}
void testcase(int connfd,char *msg,char *pattern,char *casename){
    if(connfd<0||msg==NULL||pattern==NULL||casename==NULL){
        return;
    }
    send_msg(connfd,msg,strlen(msg));
    char result[MAX_MSG_LENGTH]={0};
    recv_msg(connfd,result,MAX_MSG_LENGTH);
    if(strcmp(result,pattern)==0){
       // printf("==>pass->%s\n",casename);
    }else{
        printf("==>failed->%s, '%s'!='%s' \n",casename,result,pattern);
        exit(1);
    }
}
void array_testcase(int connfd) {

	testcase(connfd, "SET Teacher King", "OK\r\n", "SET-Teacher");
	testcase(connfd, "GET Teacher", "King\r\n", "GET-Teacher");
	testcase(connfd, "MOD Teacher Darren", "OK\r\n", "MOD-Teacher");
	testcase(connfd, "GET Teacher", "Darren\r\n", "GET-Teacher");
	testcase(connfd, "EXIST Teacher", "EXIST\r\n", "EXIST-Teacher");
	testcase(connfd, "DEL Teacher", "OK\r\n", "DEL-Teacher");
	testcase(connfd, "GET Teacher", "NO EXIST\r\n", "GET-Teacher");
	testcase(connfd, "MOD Teacher KING", "NO EXIST\r\n", "MOD-Teacher");
	testcase(connfd, "EXIST Teacher", "NO EXIST\r\n", "EXIST-Teacher");

}
void array_testcase_1w(int connfd) {

	int count = 10000;
	int i = 0;
	struct timeval tv_begin;
	gettimeofday(&tv_begin, NULL);

	// for (i = 0;i < count;i ++) {

	// 	testcase(connfd, "SET Teacher King", "OK\r\n", "SET-Teacher");
	// 	testcase(connfd, "GET Teacher", "King\r\n", "GET-Teacher");
	// 	testcase(connfd, "MOD Teacher Darren", "OK\r\n", "MOD-Teacher");
	// 	testcase(connfd, "GET Teacher", "Darren\r\n", "GET-Teacher");
	// 	testcase(connfd, "EXIST Teacher", "EXIST\r\n", "GET-Teacher");
	// 	testcase(connfd, "DEL Teacher", "OK\r\n", "DEL-Teacher");
	// 	testcase(connfd, "GET Teacher", "NO EXIST\r\n", "GET-Teacher");
	// 	testcase(connfd, "MOD Teacher KING", "NO EXIST\r\n", "MOD-Teacher");
	// 	testcase(connfd, "EXIST Teacher", "NO EXIST\r\n", "GET-Teacher");

	// }
    while(i<count){
        array_testcase(connfd);
        i++;
    }
	struct timeval tv_end;
	gettimeofday(&tv_end, NULL);
	int time_used = TIME_SUB_MS(tv_end, tv_begin); // ms
	printf("array testcase --> time_used: %d, qps: %d\n", time_used, 90000 * 1000 / time_used);
}


int main(int argc,char *argv[]){
    if(argc!=4){
        return -1;
    }
    char *ip=argv[1];
    unsigned short port=atoi(argv[2]);
    int mod=atoi(argv[3]);
    int connfd=connect_server(ip,port);
    if(mod==0){
        array_testcase_1w(connfd);
    }
    return 0;
    
}