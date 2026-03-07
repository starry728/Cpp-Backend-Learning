#include "socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>

//信息结构体
struct SockInfo
{
    struct sockaddr_in addr;
    int fd;
};
struct SockInfo infos[250];//表示最多支持和250个用户端同时通信

void* working(void* arg)
{
    struct SockInfo* pinfo = (struct SockInfo*)arg;
    //连接建立成功，打印客户端的IP和端口信息
    char ip[32];
    printf("客户端的IP：%s, 端口：%d\n",
            inet_ntop(AF_INET, &pinfo->addr.sin_addr.s_addr, ip, sizeof(ip)),
            ntohs(pinfo->addr.sin_port));
    //5.通信
    while(1)
    {
        char* buf;
        int len = recvMsg(pinfo->fd, &buf);
        printf("接收数据,%d：.....\n",len);
        if(len>0)
        {
            printf("%s\n\n\n\n",buf);
            free(buf);
        }    
        else
        {
            close(pinfo->fd);
            break;
        }
        sleep(1);
    }
    pinfo->fd=-1;
    return NULL;
}

int main()
{
    // 1. 创建用于监听的套接字
    int fd = createSocket();
    if(fd == -1)
    {
        return -1;
    }
    // 2. 绑定本地的IP和端口 + 设置监听
    int ret = setListen(fd,10000);
    if(ret==-1)
    {
        return -1;
    }
    int max = sizeof(infos)/sizeof(infos[0]);
    for(int i=0;i<max;++i)
    {
        bzero(&infos[i],sizeof(infos[0]));
        infos[i].fd=-1;
    }
    // 4. 阻塞并等待客户端的连接
    while(1)
    {
        struct SockInfo* pinfo;
        printf("max: %d\n",max);
        for(int i=0;i<max;++i)
        {
            if(infos[i].fd==-1)
            {
                pinfo = &infos[i];
                break;
            }
        }
        pinfo->fd=acceptConn(fd, &pinfo->addr);
        //创建对应子线程 
        pthread_t tid;
        pthread_create(&tid,NULL,working,pinfo);
        pthread_detach(tid);
    }
    closeSocket(fd);
}