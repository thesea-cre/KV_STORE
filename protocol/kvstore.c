#include"kvstore.h"
#include"sync.h"
kv_array_t global_array;
KV_Map global_hash;
KV_Rbtree global_rbtree;
Mempool_Manager *mempool;
void *kv_malloc(size_t size){//用的很多的系统函数最好包装一层，为了修改方便
    #if 1
    return malloc(size);
    #else 
    return Managerpool_Alloc(mempool,size);
    #endif
}
void kv_free(void* ptr){
    #if 1
    return free(ptr);
    #else
    return Managerpool_free(mempool,ptr);
    #endif
}
int kv_set(char *key,char *value){//做一个适配层，对外接口适用，隐藏核心层的选择，这样测试模块方便一点
    int ret;
    #if ENGINE==ENGINE_ARRAY
    ret=kv_array_set(&global_array,key,value);
    #elif ENGINE==ENGINE_HASH
    ret=KV_Map_set(&global_hash,key,value);
    #elif ENGINE==ENGINE_RBTREE
    ret=KV_Rbtree_set(&global_rbtree,key,value);
    #endif
    return ret;
}
int kv_cmd_set(kv_request_t req,result*c){
    c->tlv_array=(TLV*)kv_malloc(c->tlv_num*sizeof(TLV));
    int ret;
    for(int i=0;i<req.key_num;i++){
        char *key=req.key[i]->data;
        char *value=req.value[i]->data;
        uint8_t value_type=req.value[i]->type;
        if(value_type==TYPE_STRING){
            //不同类型执行不同的函数，最后将真的value值存入值中
        }
        ret=kv_set(key,value);
        c->tlv_array[i].tag=TAG_RES;
        c->tlv_array[i].type=TYPE_STRING;
        char buf[BUFFER_SIZE]={0};
        if(ret==0){
            c->tlv_array[i].length=sprintf(buf,"OK\r\n");
            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
            memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
        }else if(ret>0){
            c->tlv_array[i].length=sprintf(buf,"EXIST\r\n");
            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
            memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
        }else{
            c->tlv_array[i].length=sprintf(buf,"ERROR\r\n");
            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
            memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
        }
    }
}
char *kv_get(char *key){
    char *value;
    #if ENGINE==ENGINE_ARRAY
    value=kv_array_get(&global_array,key);
    #elif ENGINE==ENGINE_HASH
    value=KV_Map_get(&global_hash,key);
    #elif ENGINE==ENGINE_RBTREE
    value=KV_Rbtree_get(&global_rbtree,key);
    #endif
    return value;
}
int kv_cmd_get(kv_request_t req,result*c){
    c->tlv_array=(TLV*)kv_malloc(c->tlv_num*sizeof(TLV));
    for(int i=0;i<req.key_num;i++){
        char *key=req.key[i]->data;//存入的时候没有存入值的类型，直接存的是字节流，get出来怎么解析呢
        char *value=kv_get(key);
        c->tlv_array[i].tag=TAG_VALUE;
        c->tlv_array[i].type=TYPE_STRING;
        char buf[BUFFER_SIZE]={0};
        if(value){
            c->tlv_array[i].length=strlen(value)+1;
            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
            memcpy(c->tlv_array[i].value,value,c->tlv_array[i].length);
        }else if(value==NULL){
            c->tlv_array[i].length=sprintf(buf,"NOT EXIST\r\n");
            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
            memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
        }
    }
}
int kv_mod(char *key,char *value){
    int ret;
    #if ENGINE==ENGINE_ARRAY
    ret=kv_array_mod(&global_array,key,value);
    #elif ENGINE==ENGINE_HASH
    ret=KV_Map_mod(&global_hash,key,value);
    #elif ENGINE==ENGINE_RBTREE
    ret=KV_Rbtree_mod(&global_rbtree,key,value);
    #endif
    return ret;
}
int kv_cmd_mod(kv_request_t req,result*c){
    int ret;
    c->tlv_array=(TLV*)kv_malloc(c->tlv_num*sizeof(TLV));
    for(int i=0;i<req.key_num;i++){
        char *key=req.key[i]->data;
        char *value=req.value[i]->data;
        uint8_t value_type=req.value[i]->type;
        if(value_type==TYPE_STRING){
            //不同类型执行不同的函数，最后将真的value值存入值中
        }

        ret=kv_mod(key,value);
        c->tlv_array[i].tag=TAG_RES;
        c->tlv_array[i].type=TYPE_STRING;
        char buf[BUFFER_SIZE]={0};
        if(ret==0){
            c->tlv_array[i].length=sprintf(buf,"OK\r\n");
            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
            memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
        }else if(ret>0){
            c->tlv_array[i].length=sprintf(buf,"NOT EXIST\r\n");
            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
            memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
        }else{
            c->tlv_array[i].length=sprintf(buf,"ERROR\r\n");
            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
            memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
        }
    }
}
int kv_del(char *key){
    int ret;
    #if ENGINE==ENGINE_ARRAY
    ret=kv_array_del(&global_array,key);
    #elif ENGINE==ENGINE_HASH
    ret=KV_Map_del(&global_hash,key);
    #elif ENGINE==ENGINE_RBTREE
    ret=KV_Rbtree_del(&global_rbtree,key);
    #endif
    return ret;
}
int kv_cmd_del(kv_request_t req,result*c){
    int ret;
    c->tlv_array=(TLV*)kv_malloc(c->tlv_num*sizeof(TLV));
    for(int i=0;i<req.key_num;i++){
        char *key=req.key[i]->data;
        ret=kv_del(key);
        c->tlv_array[i].tag=TAG_RES;
        c->tlv_array[i].type=TYPE_STRING;
        char buf[BUFFER_SIZE]={0};
        if(ret==0){
            c->tlv_array[i].length=sprintf(buf,"OK\r\n");
            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
            memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
        }else if(ret>0){
            c->tlv_array[i].length=sprintf(buf,"NOT EXIST\r\n");
            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
            memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
        }else{
            c->tlv_array[i].length=sprintf(buf,"ERROR\r\n");
            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
            memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
        }
    }
}
int kv_exist(char *key){
    int ret;
    #if ENGINE==ENGINE_ARRAY
    ret=kv_array_exist(&global_array,key);
    #elif ENGINE==ENGINE_HASH
    ret=KV_Map_exist(&global_hash,key);
    #elif ENGINE==ENGINE_RBTREE
    ret=KV_Rbtree_exist(&global_rbtree,key);
    #endif
    return ret;
}
int kv_cmd_exist(kv_request_t req,result*c){
    int ret;
    c->tlv_array=(TLV*)kv_malloc(c->tlv_num*sizeof(TLV));
    for(int i=0;i<req.key_num;i++){
        char *key=req.key[i]->data;
        ret=kv_exist(key);
        c->tlv_array[i].tag=TAG_RES;
        c->tlv_array[i].type=TYPE_STRING;
        char buf[BUFFER_SIZE]={0};
        if(ret==0){
            c->tlv_array[i].length=sprintf(buf,"NOT EXIST\r\n");
            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
            memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
        }else if(ret>0){
            c->tlv_array[i].length=sprintf(buf,"EXIST\r\n");
            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
            memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
        }else{
            c->tlv_array[i].length=sprintf(buf,"ERROR\r\n");
            c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
            memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
        }
    }
}
//
int kv_response_expectation(kv_request_t req,result*c,int cmd){
    c->tlv_array=(TLV*)kv_malloc(c->tlv_num*sizeof(TLV));
    for(int i=0;i<req.key_num;i++){
        char *key=req.key[i]->data;
        //ret=kv_exist(key);
        c->tlv_array[i].tag=TAG_RES;
        c->tlv_array[i].type=TYPE_STRING;
        char buf[BUFFER_SIZE]={0};
        switch (cmd){
            case KV_CMD_GET:
            case KV_CMD_EXIST:
                c->tlv_array[i].length=sprintf(buf,"this server do not support read\r\n");
                break;
            case KV_CMD_SET:
            case KV_CMD_MOD:
            case KV_CMD_DEL:
                c->tlv_array[i].length=sprintf(buf,"this server do not support write\r\n");
                break;
        }
        c->tlv_array[i].value=(char *)kv_malloc(c->tlv_array[i].length);
        memcpy(c->tlv_array[i].value,buf,c->tlv_array[i].length);
    }
    return 0;
}
// 只执行命令，不做任何持久化
int kv_cmd_execute(kv_request_t *req, result *c, int cmd) {
    switch (cmd) {
        case KV_CMD_SET:
            kv_cmd_set(*req, c);
            break; 
        case KV_CMD_GET:
            kv_cmd_get(*req, c);
            break;
        case KV_CMD_MOD:
            kv_cmd_mod(*req, c);
            break;
        case KV_CMD_DEL:
            kv_cmd_del(*req, c);
            break;
        case KV_CMD_EXIST:
            kv_cmd_exist(*req, c);
            break;
        default:
            break;
    }
    return 0;
}
//>0表示处理了多少字节，-2 非法包，=-1 包不完整
//rlength:msg的长度,msg是rbuffer,里面全是包，函数处理一个包就返回
int kv_protocol(char *msg,int rlength,result *c){//这个函数只能一个包一个包的处理
    if(rlength<10){
        return -1;
    }
    int offset=0;
    int ret=0;
    kv_header_t header;
    kv_request_t req;
    memset(&header,0,sizeof(kv_header_t));
    ret=kv_header_decode(msg,rlength,&header);
    offset+=ret;
    uint8_t version=header.version;
    uint8_t msg_type=header.msg_type;
    uint16_t opcode=header.opcode;
    uint32_t len=header.length;//一条命令的数据长度,也是这个payload消息的消息边界
    INFO("rlength:%d\n",rlength);
    INFO("header.length=%d\n",len);
    if(header.magic!=KV_MAGIC){//数据是脏数据
        return -2;
    }
    if(rlength-10<len){//解析出头部，等包完整后才继续处理
        return -1;
    }
    kv_request_t_init(&req);
    ret=kv_payload_decode(msg+offset,len,&req);//解析协议，结果处理到req中，里面都是key,value
    offset+=ret;//offset标记了这个包的边界
    INFO("kv_payload_decode return %d\n",ret);
    printf("for\n");
    // for(int i=0;i<req.key_num;i++){
    //     INFO("key:%s,length:%d\n",req.key[i]->data,req.key[i]->length);
    //     if(i < req.value_num && req.value[i] && req.value[i]->data){
    //     INFO("value:%s, length:%d\n", req.value[i]->data, req.value[i]->length);
    // }
    // }
    if(version==KV_VERSION){//版本
        if(msg_type==MSG_TYPE_REQ){//协议类型
            int flag=0;
            int cmd;
            for(cmd=KV_CMD_START;cmd<KV_CMD_COUNT;cmd++){
                if(strcmp(command[opcode],command[cmd])==0){
                    flag=1;
                    INFO("command:%s\n",command[cmd]);
                    break;
                    }
            }
            if(flag==0) {
                INFO("命令输入错误\n");
                char buf[BUFFER_SIZE]="命令输入错误";
                c->tlv_num=1;
                c->tlv_array=(TLV*)kv_malloc(c->tlv_num*sizeof(TLV));
                c->tlv_array[0].tag=TAG_RES;
                c->tlv_array[0].type=TYPE_STRING;
                c->tlv_array[0].value=(char *)kv_malloc(strlen(buf)+1);
                c->tlv_array[0].length=strlen(buf)+1;
                memcpy(c->tlv_array[0].value,buf,c->tlv_array[0].length);
                exit(1);
            }
            c->tlv_num=req.key_num;
            switch (cmd) {
    // 写操作：先写AOF，再执行
            case KV_CMD_SET:
            case KV_CMD_MOD:
            case KV_CMD_DEL:
                if(s_or_c==0){
                    aof_append(msg, offset);                       // 单包长，防粘包把剩余缓冲区写进AOF
                    kv_cmd_execute(&req, c, cmd);
                    master_append_cmd(&mgr,(uint8_t*)msg,offset);
                    master_notify_slaves(&mgr);
                }else if(s_or_c==1){
                    kv_response_expectation(req,c,cmd);
                }else{
                    aof_append(msg, rlength);
                    kv_cmd_execute(&req, c, cmd);
                }
                break;
                
            // 读操作：直接执行，不写AOF
            case KV_CMD_GET:
            case KV_CMD_EXIST:
                if(s_or_c==0){
                    kv_response_expectation(req,c,cmd);
                }else if(s_or_c==1){
                    kv_cmd_execute(&req,c,cmd);
                }else{
                    kv_cmd_execute(&req, c, cmd);
                }
                
                break;
        }

            kv_request_t_destory(&req);
            return offset;//处理完了一个包，一个包一个包的处理
        }
    }
}

/*rbuffer:接受缓冲区，wbuffer:发送缓冲区，rlength:接受缓冲区的字节数，wlength:发送缓冲区的字节数
    思路：在发送缓冲区中拿一个包出来解包kv_protocol()，解一TLV，结果放在res，解一个TLV生成d对应的响应信息，
    响应信息放放在res中，然后kv_response()将res中的响应信息组成TLV放到发送缓冲区，都是以追加包的方式放到缓冲区。
    kv_requeset返回处理了多少字节数据，格式要求严格.
 */
 //-2:错误。-1:不完整TLV。0 处理完了rbuffer里的所有TLV。>0:处理了的所有数据长度，wbuffer满了
 //考虑了wbuffer满的情况。
int kv_request(char *rbuffer,char *wbuffer,int rlength,int *wlength){//解包，协议编码，响应
    if((rlength)<10){
        return -1;
    }
    int total_handled=0;
    
     while(1){//循环解包
        if(rlength==total_handled){
            return 0;
        }
        int ret;
        struct result res;
        memset(&res,0,sizeof(struct result));
        // if(s_or_c==0){
        //     ret=kv_master_protocol(rbuffer+total_handled,(rlength)-total_handled,&res);
        // }else if(s_or_c==1){
        //     ret=kv_slave_protocol(rbuffer+total_handled,(rlength)-total_handled,&res);
        // }else{
        //     ret=kv_protocol(rbuffer+total_handled,(rlength)-total_handled,&res);
        // }
        ret=kv_protocol(rbuffer+total_handled,(rlength)-total_handled,&res);
        printf("kv_protocol return %d\n",ret);
        if(ret==-1){//包不完整
            for(int i=0;i<res.tlv_num;i++){
                kv_free(res.tlv_array[i].value);
            }
            kv_free(res.tlv_array);
            return -1;
        }
        if(ret==-2){//非法包
            for(int i=0;i<res.tlv_num;i++){
                kv_free(res.tlv_array[i].value);
            }
            kv_free(res.tlv_array);
            return -2;
        }
        char tmp_buf[BUFFER_SIZE]={0};
        int res_ret=kv_response(tmp_buf,BUFFER_SIZE,&res);//响应先写到暂时的缓冲区中
        printf("kv_response return %d\n",res_ret);
        if((*wlength)+res_ret>BUFFER_SIZE){//发送缓冲区写不下了就不把新的响应加进来
            break;
        }
        memcpy(wbuffer+(*wlength),tmp_buf,res_ret);//然后追加到发送缓冲区中
        (*wlength)+=res_ret;
        for(int i=0;i<res.tlv_num;i++){
            kv_free(res.tlv_array[i].value);
        }
        kv_free(res.tlv_array);
        total_handled+=ret;
    }
    return total_handled;
}
// int kv_incremental(char *rbuffer,int rlength){//解包，协议编码，响应
//     if((rlength)<10){
//         return -1;
//     }
//     int ret=0;
//     int offset=0;
//      while(rlength-offset>=10){//循环解包，剩余不足一个包头就停，尾部留给下一段数据
//         struct result res;
//         memset(&res,0,sizeof(struct result));
//         INFO("没有处理的数据长度:%d(bytes)\n",rlength-offset);
//         // 
     

//         //
//         if(ret<=0){//-1不完整/-2非法：已消费的部分照常返回，不整包丢弃
//             break;
//         }
//          offset+=ret;
//          INFO("处理了%d(bytes)的数据\n",offset);
//         for(int i=0;i<res.tlv_num;i++){
//             kv_free(res.tlv_array[i].value);
//         }
//         kv_free(res.tlv_array);
//     }
//     return offset; 
// }
int kv_incremental(char *rbuffer, int rlength) {
    if (rlength < 10) {
        return -1; // 连包头都不够，等下一段数据
    }
    
    int offset = 0;
    // 循环解包，直到剩余数据不足一个完整的包
    while (rlength - offset >= 10) {
        kv_header_t header;
        memset(&header, 0, sizeof(kv_header_t));
        
        // 1. 解析头部
        int head_ret = kv_header_decode((const uint8_t*)(rbuffer + offset), rlength - offset, &header);
        if (header.magic != KV_MAGIC) {
            INFO("kv_incremental: bad magic\n");
            break; // 脏数据，停止处理，避免死循环
        }
        
        int packet_len = header.length + 10; // 10字节头部 + 数据长度
        
        // 2. 判断包是否完整
        if (rlength - offset < packet_len) {
            break; // 半包，留给下一段数据拼接
        }

        // 3. 解析 Payload
        kv_request_t req;
        kv_request_t_init(&req);
        int payload_ret = kv_payload_decode(rbuffer + offset + 10, header.length, &req);

        // 4. 识别命令类型
        int cmd;
        int flag = 0;
        for (cmd = KV_CMD_START; cmd < KV_CMD_COUNT; cmd++) {
            if (strcmp(command[header.opcode], command[cmd]) == 0) {
                flag = 1;
                break;
            }
        }

        if (flag) {
            // 5. 增量补发只处理写命令（SET/MOD/DEL），需要执行并落盘
            if (cmd == KV_CMD_SET || cmd == KV_CMD_MOD || cmd == KV_CMD_DEL) {
                // 【关键修复1】写入本地 AOF 文件（防止重启后数据丢失）
                aof_append(rbuffer + offset, packet_len);
                
                // 【关键修复2】直接调用执行引擎，更新内存
                result res;
                memset(&res, 0, sizeof(result));
                res.tlv_num = req.key_num; // 
                
                kv_cmd_execute(&req, &res, cmd);
                
                // 【关键修复3】释放执行过程中分配的内存
                for (uint32_t i = 0; i < res.tlv_num; i++) {
                    kv_free(res.tlv_array[i].value);
                }
                kv_free(res.tlv_array);
            }
        }
        
        kv_request_t_destory(&req);
        offset += packet_len; // 严格按包长移动偏移量
    }
    
    return offset; // 返回实际处理的字节数，未处理的残包留给下次recv
}
int kv_response(char *wbuffer,int len,result *c){
    int offset=0;
    int ret=0;
    kv_header_t h;
    h.magic=KV_MAGIC;
    h.version=KV_VERSION;
    h.msg_type=MSG_TYPE_RSP;
    uint32_t data_len=0;
    for(int i=0;i<c->tlv_num;i++){
        data_len+=c->tlv_array[i].length;
    }
    h.length=data_len+10+c->tlv_num*7;//头部长度+data_len+7*tlv_num;
    h.opcode=OP_RES;
    ret=kv_header_encode(&h,wbuffer,len);
    offset+=ret;
    printf("tlv_num:%d\n",c->tlv_num);
    for(int i=0;i<c->tlv_num;i++){
        ret=kv_tlv_append(wbuffer+offset,len-offset,c->tlv_array[i]);
        offset+=ret;
    }
    return offset;
}
void init_kvengine(void){
    #if ENGINE==ENGINE_ARRAY
    memset(&global_array,0,sizeof(kv_array_t));
    kv_array_create(&global_array);
    #elif ENGINE==ENGINE_HASH
    memset(&global_hash,0,sizeof(KV_Map));
    KV_Map_create(&global_hash);
    #elif ENGINE==ENGINE_RBTREE
    memset(&global_rbtree,0,sizeof(KV_Rbtree));
    KV_Rbtree_create(&global_rbtree);
    #endif
}
void dest_kvengine(void){
    #if ENGINE==ENGINE_ARRAY
    kv_array_destory(&global_array);
    #elif ENGINE==ENGINE_HASH
    KV_Map_destory(&global_hash);
    #elif ENGINE==ENGINE_RBTREE
    KV_Rbtree_destory(&global_rbtree);
    #endif
}
