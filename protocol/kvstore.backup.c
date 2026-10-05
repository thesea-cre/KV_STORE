// int kv_master_protocol(char*msg,int rlength,result*c){
//     if(rlength<10){
//         return -1;
//     }
//     int offset=0;
//     int ret=0;
//     kv_header_t header;
//     kv_request_t req;
//     memset(&header,0,sizeof(kv_header_t));
//     ret=kv_header_decode(msg,rlength,&header);
//     offset+=ret;
//     uint8_t version=header.version;
//     uint8_t msg_type=header.msg_type;
//     uint16_t opcode=header.opcode;
//     uint32_t len=header.length;//一条命令的数据长度,也是这个payload消息的消息边界
//     INFO("rlength:%d\n",rlength);
//     INFO("header.length=%d\n",len);
//     if(header.magic!=KV_MAGIC){//数据是脏数据
//         return -2;
//     }
//     if(rlength-10<len){//解析出头部，等包完整后才继续处理
//         return -1;
//     }
//     kv_request_t_init(&req);
//     ret=kv_payload_decode(msg+offset,len,&req);//解析协议，结果处理到req中，里面都是key,value
    
//     offset+=ret;//offset标记了这个包的边界
//     INFO("kv_payload_decode return %d\n",ret);
//     // for(int i=0;i<req.key_num;i++){
//     //     printf("key:%s,length:%d\n",req.key[i]->data,req.key[i]->length);
//     //     if(i < req.value_num && req.value[i] && req.value[i]->data){
//     //     printf("value:%s, length:%d\n", req.value[i]->data, req.value[i]->length);
//     // }
//     // }
//     if(version==KV_VERSION){//版本
//         if(msg_type==MSG_TYPE_REQ){//协议类型
//             int flag=0;
//             int cmd;
//             for(cmd=KV_CMD_START;cmd<KV_CMD_COUNT;cmd++){
//                 if(strcmp(command[opcode],command[cmd])==0){
//                     flag=1;
//                     INFO("command:%s\n",command[cmd]);
//                     break;
//                     }
//             }
//             if(flag==0) {
//                 printf("命令输入错误\n");
//                 char buf[BUFFER_SIZE]="命令输入错误";
//                 c->tlv_num=1;
//                 c->tlv_array=(TLV*)kv_malloc(c->tlv_num*sizeof(TLV));
//                 c->tlv_array[0].tag=TAG_RES;
//                 c->tlv_array[0].type=TYPE_STRING;
//                 c->tlv_array[0].value=(char *)kv_malloc(strlen(buf)+1);
//                 c->tlv_array[0].length=strlen(buf)+1;
//                 memcpy(c->tlv_array[0].value,buf,c->tlv_array[0].length);
//                 exit(1);
//             }
//             c->tlv_num=req.key_num;
//             switch (cmd) {
//     // 写操作：先写AOF，再执行
//             case KV_CMD_SET:
//             case KV_CMD_MOD:
//             case KV_CMD_DEL:
//                 aof_append(msg, offset);                       // 单包长，防粘包把剩余缓冲区写进AOF
//                 kv_cmd_execute(&req, c, cmd);
//                 master_append_cmd(&mgr,(uint8_t*)msg,offset);
//                 master_notify_slaves(&mgr);
//                 break;
                
//             // 读操作：直接执行，不写AOF
//             case KV_CMD_GET:
//             case KV_CMD_EXIST:
//                 //回复客户端不支持写入
//                 kv_response_expectation(req,c,cmd);
//                 break;
//         }
//             kv_request_t_destory(&req);
//             return offset;//处理完了一个包，一个包一个包的处理
//         }
//     }
// }
// int kv_slave_protocol(char*msg,int rlength,result*c){
//     if(rlength<10){
//         return -1;
//     }
//     int offset=0;
//     int ret=0;
//     kv_header_t header;
//     kv_request_t req;
//     memset(&header,0,sizeof(kv_header_t));
//     ret=kv_header_decode(msg,rlength,&header);
//     offset+=ret;
//     uint8_t version=header.version;
//     uint8_t msg_type=header.msg_type;
//     uint16_t opcode=header.opcode;
//     uint32_t len=header.length;//一条命令的数据长度,也是这个payload消息的消息边界
//     INFO("rlength:%d\n",rlength);
//     INFO("header.length=%d\n",len);
//     if(header.magic!=KV_MAGIC){//数据是脏数据
//         return -2;
//     }
//     if(rlength-10<len){//解析出头部，等包完整后才继续处理
//         return -1;
//     }
//     kv_request_t_init(&req);
//     ret=kv_payload_decode(msg+offset,len,&req);//解析协议，结果处理到req中，里面都是key,value
    
//     offset+=ret;//offset标记了这个包的边界
//     INFO("kv_payload_decode return %d\n",ret);
//     // for(int i=0;i<req.key_num;i++){
//     //     printf("key:%s,length:%d\n",req.key[i]->data,req.key[i]->length);
//     //     if(i < req.value_num && req.value[i] && req.value[i]->data){
//     //     printf("value:%s, length:%d\n", req.value[i]->data, req.value[i]->length);
//     // }
//     // }
//     if(version==KV_VERSION){//版本
//         if(msg_type==MSG_TYPE_REQ){//协议类型
//             int flag=0;
//             int cmd;
//             for(cmd=KV_CMD_START;cmd<KV_CMD_COUNT;cmd++){
//                 if(strcmp(command[opcode],command[cmd])==0){
//                     flag=1;
//                     printf("command:%s\n",command[cmd]);
//                     break;
//                     }
//             }
//             if(flag==0) {
//                 printf("命令输入错误\n");
//                 char buf[BUFFER_SIZE]="命令输入错误";
//                 c->tlv_num=1;
//                 c->tlv_array=(TLV*)kv_malloc(c->tlv_num*sizeof(TLV));
//                 c->tlv_array[0].tag=TAG_RES;
//                 c->tlv_array[0].type=TYPE_STRING;
//                 c->tlv_array[0].value=(char *)kv_malloc(strlen(buf)+1);
//                 c->tlv_array[0].length=strlen(buf)+1;
//                 memcpy(c->tlv_array[0].value,buf,c->tlv_array[0].length);
//                 exit(1);
//             }
//             c->tlv_num=req.key_num;
//             switch (cmd) {
//     // 写操作：先写AOF，再执行
//             case KV_CMD_SET:
//             case KV_CMD_MOD:
//             case KV_CMD_DEL:
//                 kv_response_expectation(req,c,cmd);
//                 break;
                
//             // 读操作：直接执行，不写AOF
//             case KV_CMD_GET:
//             case KV_CMD_EXIST:
//                 kv_cmd_execute(&req,c,cmd);
//                 break;
//         }
//             kv_request_t_destory(&req);
//             return offset;//处理完了一个包，一个包一个包的处理
//         }
//     }
// }
// int kv_split_token(char *msg,char* tokens[]){
//     if(msg==NULL||tokens==NULL) return -1;
//     int i=0;
//     char *token=strtok(msg," ");
//     while(token!=NULL){
//         tokens[i++]=token;
//         token=strtok(NULL," ");
//     }
//     return i;
// }
// int kv_filter_protocol(char **tokens,int count,char *response){
//     if(tokens==NULL||count<=0||response==NULL){
//       //  printf("kv_filter_protocol\n");
//         return -1;
//     }
//     int flag=0;
//     int cmd;
//     for(cmd=KV_CMD_START;cmd<KV_CMD_COUNT;cmd++){
//         if(strcmp(tokens[0],command[cmd])==0){
//             flag=1;
//             break;
//         }
//     }
//     if(flag==0){
//        // printf("not kv_protocol\n");
//         exit(1);
//     }
//     char*key=tokens[1];
//     //printf("key:%s\n",key);
//     char *value=tokens[2];
//     //printf("value:%s\n",value);
//     int ret;
//     int length=0;
//     switch(cmd){
//         case KV_CMD_SET:
//             #if ENGINE==ENGINE_ARRAY
//             ret=kv_array_set(&global_array,key,value);
//             #elif ENGINE==ENGINE_HASH
//             ret=KV_Map_set(&global_hash,key,value);
//             #elif ENGINE==ENGINE_RBTREE
//             ret=KV_Rbtree_set(&global_rbtree,key,value);
//             #endif
//             if(ret==0){
//                 length=sprintf(response,"OK\r\n");
//             }else if(ret>0){
//                 length=sprintf(response,"EXIST\r\n");
//             }else{
//                 length=sprintf(response,"ERROR\r\n");
//             }
//             break;
//         case KV_CMD_GET:
//             #if ENGINE==ENGINE_ARRAY
//             char *v=kv_array_get(&global_array,key);
//             #elif ENGINE==ENGINE_HASH
//             char *v=KV_Map_get(&global_hash,key);
//             #elif ENGINE==ENGINE_RBTREE
//             char *v =KV_Rbtree_get(&global_rbtree,key);
//             #endif
//             if(v==NULL){
//                 length=sprintf(response,"NO EXIST\r\n");
//             }else{
//                 length=sprintf(response,"%s\r\n",v);
//             }
//             break;
//         case KV_CMD_DEL:
//             #if ENGINE==ENGINE_ARRAY
//             ret=kv_array_del(&global_array,key);
//             #elif ENGINE==ENGINE_HASH
//             ret=KV_Map_del(&global_hash,key);
//             #elif ENGINE==ENGINE_RBTREE
//             ret=KV_Rbtree_del(&global_rbtree,key);
//             #endif
//             if(ret==0){
//                 length=sprintf(response,"OK\r\n");
//             }else if(ret>0){
//                 length=sprintf(response,"NO EXIST\r\n");
//             }else{
//                 length=sprintf(response,"ERROR\r\n");
//             }
//             break;
//         case KV_CMD_MOD:
//             #if ENGINE==ENGINE_ARRAY
//             ret=kv_array_mod(&global_array,key,value);
//             #elif ENGINE==ENGINE_HASH
//             ret=KV_Map_mod(&global_hash,key,value);
//             #elif ENGINE==ENGINE_RBTREE
//             ret=KV_Rbtree_mod(&global_rbtree,key,value);
//             #endif
//             if(ret==0){
//                 length=sprintf(response,"OK\r\n");
//             }else if(ret>0){
//                 length=sprintf(response,"NO EXIST\r\n");
//             }else{
//                 length=sprintf(response,"ERROR\r\n");
//             }
//             break;
//         case KV_CMD_EXIST:
//             #if ENGINE==ENGINE_ARRAY
//             ret=kv_array_exist(&global_array,key);
//             #elif ENGINE==ENGINE_HASH
//             ret=KV_Map_exist(&global_hash,key);
//             #elif ENGINE==ENGINE_RBTREE
//             ret=KV_Rbtree_exist(&global_rbtree,key);
//             #endif
//             if(ret>0){
//                 length=sprintf(response,"EXIST\r\n");
//             }else if(ret==0){
//                 length=sprintf(response,"NO EXIST\r\n");
//             }else if(ret<0){
//                 length=sprintf(response,"ERROR\r\n");
//             }
//             break;
//         default:
//             assert(0);
//             break;
//     }
//     return length;
// }
// int kv_protocol(char *msg,int rlength,char *request){
//     if(msg==NULL||rlength<=0||request==NULL) {
//         return -1;
//     }
//     char *tokens
//     int count=kv_split_token(msg,tokens);
//     return kv_filter_protocol(tokens,count,request);
// }