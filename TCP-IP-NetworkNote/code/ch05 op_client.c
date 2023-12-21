#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#define BUF_SIZE 1024
#define RLT_SIZE 4 //字节大小数
#define OPSZ 4

void error_handling(char *message);

int main(int argc, char *argv[]){
    char op_msg[BUF_SIZE];
    int res,op_cnt;
    int sock;
    struct sockaddr_in serv_adr;
    if (argc != 3){
        printf("Usage : %s <IP> <port>\n", argv[0]);
        exit(1);
    }
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
        error_handling("socket() error");
    memset(&serv_adr, 0, sizeof(serv_adr));
    serv_adr.sin_family=AF_INET;
    serv_adr.sin_addr.s_addr=inet_addr(argv[1]);
    serv_adr.sin_port=htons(atoi(argv[2]));//主机字节序
    if (connect(sock, (struct sockaddr *)&serv_adr, sizeof(serv_adr)) == -1)
        error_handling("connect() error!");
    else
        puts("Connected...........");

    
    fputs("Operand count: ", stdout);
    scanf("%d", &op_cnt);
    op_msg[0]=op_cnt;//传输计算的数字数量
    for(int i=0;i<op_cnt;i++){
        //只输入0-9之间的数字
        printf("Operand %d: ", i + 1);
        scanf("%d",(int *)&op_msg[i+1]);
    }
    fgetc(stdin);
    fputs("Operator: ", stdout);
    //最后一个位置标识+-*/
    scanf("%c",&op_msg[op_cnt+1]);
    write(sock, op_msg, op_cnt+2);
    read(sock, &res, RLT_SIZE);
    printf("Operation result: %d \n", res);
    close(sock);
    return 0;
}
void error_handling(char *message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}
