#include "socket.h"
#include <stdio.h> //NULL需要
#include <errno.h> //errno需要
#include <string.h> //memcpy需要
#include <stdlib.h> //free需要
#include <unistd.h>

int initSocket()
{
    int lfd = socket(AF_INET,SOCK_STREAM,0);//IPV4,流式协议中的TCP
    if(lfd == -1)
    {
        perror("socket");
        return -1;
    }
    return lfd;
}

void initSockaddr(struct sockaddr* addr,unsigned short port,const char* ip)
{
    struct sockaddr_in* addrin = (struct sockaddr_in*)addr;//成员更多，直接用不太方便；占用内存大小是一样的
    addrin->sin_family = AF_INET;
    addrin->sin_port = htons(port);//传进来的是小端（主机字节序），要转换为网络字节序
    addrin->sin_addr.s_addr = inet_addr(ip);//转换为整型数 
}

int setListen(int lfd, unsigned short port)
{
    struct sockaddr addr;
    initSockaddr(&addr, port, "0.0.0.0");//0地址，对应一个宏INADDR_ANY
    //设置端口复用
    int opt = 1;//表示复用属性需要被打开
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    int ret = bind(lfd, &addr, sizeof(addr));//绑定
    if(ret == -1)
    {
        perror("bind");
        return -1;
    }
    ret = listen(lfd,128);//一次性同时监听的连接数量（每一次的，可以排队后面继续连）
    if(ret == -1)
    {
        perror("listen");
        return -1;
    }
    return 0;
}

int acceptConnect(int lfd, struct sockaddr *addr)
{
    int connfd;
    if(addr == NULL)
    {
        connfd = accept(lfd, NULL, NULL);
    }
    else
    {
        socklen_t len = sizeof(struct sockaddr);
        connfd = accept(lfd, addr, &len);
    }
    if(connfd == -1)
    {
        perror("accept");
        return -1;
    }
    return connfd;
}

int connectToHost(int fd, unsigned short port, const char* ip)
{
    struct sockaddr addr;
    initSockaddr(&addr,port,ip);
    int ret = connect(fd, &addr, sizeof(addr));
    if(ret == -1)
    {
        perror("connect");
        return -1;
    }
    return 0;
}

int readn(int fd, char* buffer, int size)//往buffer内存里写数据
{
    int left = size;//size是能够往内存里写入的最大字节数
    int readBytes = 0;//每次读数据读出的字节数
    char* ptr = buffer;//读出数据写入buffer这块内存

    while(left)
    {
        readBytes = read(fd, ptr, left);
        if(readBytes == -1)
        {
            if(errno == EINTR)//“当被信号中断、调用失败了”
            {
                readBytes = 0;
            }
            else
            {
                perror("read");
                return -1;
            }           
        }
        else if(readBytes == 0)
        {
            printf("对方主动断开了连接...\n");//即对方调用了close()函数
            return -1;
        }
        left -= readBytes;
        ptr += readBytes; 
    }
    return size-left;
}

int writen(int fd, const char* buffer, int length)//发送buffer这块内存中的数据，所以加const
{
    int left = length;
    int writeBytes = 0;
    const char* ptr = buffer;

    while(left)
    {
        writeBytes = write(fd, ptr, left);//返回值是当前已经发送出去的字节数。send比write多一个参数
        if(writeBytes <= 0)
        {
            if(errno == EINTR)//errno是一个全局变量。“当write被信号中断、调用失败了”
            {
                writeBytes = 0;
            }
            else
            {
                perror("write");
                return -1;
            }
        }
        ptr += writeBytes;
        left -=writeBytes;
    }
    return length;//返回已发送字节数
}

bool sendMessage(int fd, const char* buffer, int length, enum Type t)
{
    int dataLen = length + 1 + sizeof(int);
    //申请堆内存
    char* data = (char*)malloc(dataLen);
    if(data == NULL)
    {
        return false;
    }
    int netlen = htonl(length + 1);
    memcpy(data, &netlen, sizeof(int));
    char* ch = t==Heart?"H":"M";
    memcpy(data + sizeof(int), ch, sizeof(char));
    memcpy(data + sizeof(int) + 1, buffer, length);
    int ret = writen(fd, data, dataLen);
    free(data);
    return ret==dataLen;
}

int recvMessage(int fd, char** buffer, enum Type* t)
{
    int dataLen = 0;
    int ret = readn(fd, (char*)&dataLen, sizeof(int));
    if(ret == -1)
    {
        *buffer = NULL;
        return -1;
    }
    dataLen = ntohl(dataLen);
    char ch;
    readn(fd, &ch, 1);
    *t = ch=='H' ? Heart : Message;
    char* tmpbuf = (char*)calloc(dataLen, sizeof(char));//dataLen多申请了一个字节的内存：结尾的\0
    if(tmpbuf == NULL)
    {
        *buffer = NULL;
        return -1;
    }
    ret = readn(fd, tmpbuf, dataLen-1);
    if(ret != dataLen-1)
    {
        free(tmpbuf);
        *buffer = NULL;
        return -1;
    }
    *buffer = tmpbuf;
    return ret;//数据长度
}