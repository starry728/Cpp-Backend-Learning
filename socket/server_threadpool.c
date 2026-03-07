#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "threadpool.h"

//信息结构体
struct SockInfo
{
    struct sockaddr_in addr;
    int fd;
};
typedef struct PoolInfo
{
    ThreadPool* p;
    int fd;
}PoolInfo;

void working(void* arg);
void acceptConn(void* arg);

int main()
{
    //1.创建监听的套接字
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd==-1)
    {
        perror("socket");
        return -1;
    }
    //2.绑定本地的IP port
    struct sockaddr_in saddr;
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(9999);//找一个本地的未被占用的端口，一般5000以上占用较少
    saddr.sin_addr.s_addr = INADDR_ANY;//这个宏的实际值为0 = 0.0.0.0 ; 对于0来说，大端和小端没有区别因此不需要转换。指定为0后会自动去读本地主机的IP地址
    int ret = bind(fd, (struct sockaddr*)&saddr, sizeof(saddr));
    if(ret==-1)
    {
        perror("bind");
        return -1;
    }    
    //3.设置监听
    ret = listen(fd, 128);
    if(ret==-1)
    {
        perror("listen");
        return -1;
    }

    //创建线程池
    ThreadPool *pool = threadPoolCreate(3,8,100);//最小3个最多8个线程，最多存100个任务
    PoolInfo* info = (PoolInfo*)malloc(sizeof(PoolInfo));
    info->p = pool;
    info->fd = fd;//监听绑定的fd
    threadPoolAdd(pool,acceptConn,info);//添加任务：去检测有无新的客户端连接

    pthread_exit(NULL);//退出当前线程即主线程，并不会影响其他线程

    return 0;
}
void acceptConn(void* arg)
{
    PoolInfo* poolinfo = (PoolInfo*)arg;
    //4.阻塞并等待客户端的连接
    int addrlen = sizeof(struct sockaddr_in);
    while(1)
    {
        struct SockInfo* pinfo;
        pinfo = (struct SockInfo*)malloc(sizeof(struct SockInfo));
        pinfo->fd = accept(poolinfo->fd, (struct sockaddr*)&pinfo->addr, &addrlen);
        if(pinfo->fd==-1)
        {
            perror("accept");
            break;
        }
        //添加通信的任务
        threadPoolAdd(poolinfo->p,working,pinfo);
    }
    close(poolinfo->fd);//监听的文件描述符需要被关闭，通信的是在子线程里面关闭
}

void working(void* arg)
{
        struct SockInfo* pinfo=(struct SockInfo*)arg;
        //连接建立成功，打印客户端的IP和端口信息
        char ip[32];
        printf("客户端的IP：%s，端口：%d\n",
                inet_ntop(AF_INET, &pinfo->addr.sin_addr.s_addr, ip, sizeof(ip)),
                ntohs(pinfo->addr.sin_port));
        //5.通信
        while(1)
        {
            //接收数据
            char buff[1024];
            int len = recv(pinfo->fd, buff, sizeof(buff), 0);
            if(len > 0)
            {
                printf("client say: %s\n", buff);
                send(pinfo->fd, buff, len, 0);//原样回复
            }
            else if(len==0)
            {
                printf("客户端已经断开了连接\n");
                break;
            }
            else
            {
                perror("recv"); 
                break;
            }
        }
        //通信结束，关闭文件描述符
        close(pinfo->fd);
}

