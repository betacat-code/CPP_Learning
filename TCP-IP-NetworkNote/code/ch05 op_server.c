#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#define BUF_SIZE 1024
#define OPSZ 4
void error_handling(char *message);
int calculate(int opnum, char opnds[], char op);
int main(int argc, char *argv[]){
    int serv_sock, clnt_sock;
    char op_msg[BUF_SIZE];
    int res,op_cnt;
    struct sockaddr_in serv_adr, clnt_adr;
    socklen_t clnt_adr_sz;
    if (argc != 2){
        printf("Usage : %s <port>\n", argv[0]);
        exit(1);
    }
    serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (serv_sock == -1)
        error_handling("socket() error");
    memset(&serv_adr, 0, sizeof(serv_adr));
    serv_adr.sin_family = AF_INET;
    serv_adr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_adr.sin_port = htons(atoi(argv[1]));
    if (bind(serv_sock, (struct sockaddr *)&serv_adr, sizeof(serv_adr)) == -1)
        error_handling("bind() error");
    if (listen(serv_sock, 5) == -1)
        error_handling("listen() error");
    clnt_adr_sz = sizeof(clnt_adr);
    for (int i = 0; i < 5; i++){//接受5个客户
        clnt_sock = accept(serv_sock, (struct sockaddr *)&clnt_adr, &clnt_adr_sz);
        read(clnt_sock,op_msg,BUF_SIZE);
        op_cnt=op_msg[0];
        res=calculate(op_cnt,op_msg,op_msg[op_cnt+1]);
        write(clnt_sock, (char *)&res, sizeof(res));
        close(clnt_sock);
    }
    close(serv_sock);
}
int calculate(int opnum, char opnds[], char op){
    int result = 0, i;
    switch (op)
    {
    case '+':
        for (i = 1; i <=opnum; i++)
            result += opnds[i];
        break;
    case '-':
        for (i = 1; i <=opnum; i++)
            result -= opnds[i];
        break;
    case '*':
        for (i = 1; i <=opnum; i++)
            result *= opnds[i];
        break;
    }
    return result;
}
void error_handling(char *message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}
