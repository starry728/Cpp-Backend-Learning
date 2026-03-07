#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "socket.h"

//创建监听/通信的套接字
int createSocket()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd == -1)
    {
        perror("socket");
        return -1;
    }
    printf("套接字创建成功， fd=%d\n",fd);
    return fd;
}
//绑定本地的IP和端口 + 设置监听
int setListen(int lfd, unsigned short port)
{
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;//0 = 0.0.0.0
    int ret = bind(lfd, (struct sockaddr*)&addr, sizeof(addr));
    if(ret==-1)
    {
        perror("bind");
        return -1;
    }
    printf("套接字绑定成功！\n");

    ret = listen(lfd, 128);
    if(ret==-1)
    {
        perror("listen");
        return -1;
    }
    printf("设置监听成功！\n");
    return ret;
}
//阻塞并等待客户端的连接
int acceptConn(int lfd, struct sockaddr_in *addr)
{
    int cfd = -1;
    if(addr == NULL)
    {
        cfd = accept(lfd, NULL, NULL);
    }
    else
    {
        socklen_t addrlen = sizeof(struct sockaddr_in); // 类型修正
        cfd = accept(lfd, (struct sockaddr *)addr, &addrlen);//后者是传出参数（客户端的IP PORT）
    }
    if(cfd==-1)
    {
        perror("accept");
        return -1;       
    }
    printf("成功和客户端建立连接...\n");
    return cfd;
}

//接收指定字节个数的字符串
int readn(int fd, char* buf, int size)//第三个参数--接收的字节个数
{
    char* pt = buf;//辅助指针，指向存储数据的这块内存
    int count = size;//剩余要接收的字节数
    while(count > 0)
    {
        int len = recv(fd,pt,count,0);//返回本次接收数据得到的字节数
        if(len==-1)
        {
            return -1;
        }
        else if(len==0)//发送端断开了连接，不能继续接受数据了
        {
            return size-count;//返回已经接收到的字节数
        }
        pt += len;
        count -= len;
    }
    return size;
}   
//接收数据
int recvMsg(int fd, char** msg)
{
    int len =0;
    readn(fd,(char*)&len,4);//读出包头，读到&len所指向的地址（len即为数据块长度了）
    len = ntohl(len);//转为主机字节序
    printf("要接收的数据块长度：%d\n",len);
    
    char* data = (char*)malloc(len+1);//"1"代表'\0'
    int length = readn(fd,data,len);
    if(length!=len)
    {
        printf("接收数据失败了...\n");
        close(fd);
        free(data);
        return -1;
    }
    data[len] = '\0';
    *msg = data;//二级指针指向一级指针,!!因为还需要访问，故在函数外面free掉内存!!
    return length;
}
//发送指定长度的字符串
int writen(int fd,const char* msg,int size)
{
    const char* buf = msg;//指向待发送的数据的地址
    int count = size;//还没发送的字节数
    while(count > 0)
    {
        int len = send(fd,buf,count,0);
        if(len==-1)
        {
            return -1;
        }
        else if(len==0)
        {
            continue;
        }
        buf += len;
        count -= len;
    }
    return size;
}
//发送数据
int sendMsg(int fd, const char* msg, int len)
{
    if(fd < 0 || msg==NULL || len <= 0)
    {
        return -1;
    }
    char* data = (char*)malloc(len+4);//包头（4字节） + 数据块
    int biglen = htonl(len);//把包头数据(=数据块长度)转换为大端（主机字节序->网络字节序）
    memcpy(data, &biglen, 4);//拷贝到上面那块动态内存中
    memcpy(data+4, msg, len);//数据块（是网络中的）已经是大端了

    int ret = write(fd,data,len+4);
    if(ret==-1)
    {
        close(fd);       
    }
    return ret;
}
//关闭套接字
int closeSocket(int fd)
{
    int ret = close(fd);
    if(ret == -1)
    {
        perror("close");
    }
    return ret; 
}
//////////////////////////////////////客户端///////////////////////////////////
//连接服务器
int connectToHost(int fd, const char* ip, unsigned short port)
{
    struct sockaddr_in saddr;
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port); 
    inet_pton(AF_INET, ip, &saddr.sin_addr.s_addr);//
    int ret = connect(fd, (struct sockaddr*)&saddr, sizeof(saddr));
    if(ret == -1)
    {
        perror("connect");
        return -1;
    }
    printf("成功和服务器建立连接...\n");
    return ret;
}