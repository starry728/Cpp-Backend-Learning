#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <sys/select.h>
#include <pthread.h>
//多线程+select
//线程同步：访问的共享资源为rdset和maxfd ->加了互斥锁

pthread_mutex_t mutex;

typedef struct fdinfo
{
    int fd;
    int *maxfd;
    fd_set* rdset;
}FDInfo;

void* acceptConn(void* arg)
{
    printf("子线程ID：%ld\n",pthread_self());
    FDInfo* info = (FDInfo*)arg;
    // 接受客户端的连接请求, 这个调用不阻塞
    struct sockaddr_in cliaddr;
    int cliLen = sizeof(cliaddr);
    int cfd = accept(info->fd, (struct sockaddr*)&cliaddr, &cliLen);//只是做了拷贝，不需要加锁

    pthread_mutex_lock(&mutex);
    FD_SET(cfd, info->rdset);
    *info->maxfd = cfd > *info->maxfd ? cfd : *info->maxfd;// 重置最大的文件描述符
    pthread_mutex_unlock(&mutex);
    
    free(info);

    return NULL;
}

void* communication(void* arg)
{
    FDInfo* info = (FDInfo*)arg;
    char buf[1024] = {0};
    int len = read(info->fd, buf, sizeof(buf));
    if(len == 0)
    {
        printf("客户端关闭了连接...\n");
        // 将检测的文件描述符从读集合中删除
        pthread_mutex_lock(&mutex);
        FD_CLR(info->fd, info->rdset);
        pthread_mutex_unlock(&mutex);
        
        close(info->fd);
        free(info);
        return NULL;
    }
    else if(len == -1)
    {
        // 异常
        perror("read");
        free(info);
        return NULL;
    }
    printf("read buf = %s\n",buf);
                
    //小写转大写发给客户端
    for(int j=0;j<len;++j)
    {
        buf[j]=toupper(buf[j]);
    }
    printf("after buf = %s\n",buf);
    len = send(info->fd,buf,strlen(buf)+1,0);
    if(len==-1)
    {
        perror("send error");
        exit(1);
    }
    free(info);

    return NULL;
}

int main()
{
    pthread_mutex_init(&mutex,NULL);
    // 1. 创建监听的fd
    int lfd = socket(AF_INET, SOCK_STREAM, 0);

    // 2. 绑定
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9999);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(lfd, (struct sockaddr*)&addr, sizeof(addr));

    // 3. 设置监听
    listen(lfd, 128);


    int maxfd = lfd;
    // 初始化检测的"读"集合
    fd_set rdset;//传出时可能减少。是被内核检测的文件描述符
    fd_set rdtemp;//所以创建临时集合，储存传出的集合
    // 清零
    FD_ZERO(&rdset);
    // 将监听的lfd设置到检测的读集合中
    FD_SET(lfd, &rdset);
    // 通过select委托内核检测读集合中的文件描述符状态, 检测read缓冲区有没有数据
    // 如果有数据, select解除阻塞返回
    // 应该让内核持续检测
    while(1)
    {
        pthread_mutex_lock(&mutex);
        rdtemp = rdset;//初始化temp集合
        pthread_mutex_unlock(&mutex);
        int num = select(maxfd+1, &rdtemp, NULL, NULL, NULL);//检测完毕后，重置temp集合里面的数据

        if(FD_ISSET(lfd, &rdtemp))//判断监听的lfd是不是在temp这个读集合里面，在 则请求连接accept()
        {
            //建立连接，创建子线程
            pthread_t tid;
            FDInfo* info = (FDInfo*)malloc(sizeof(FDInfo));
            info->fd=lfd;
            info->maxfd=&maxfd;
            info->rdset=&rdset;
            pthread_create(&tid,NULL,acceptConn,info);
            pthread_detach(tid);//子线程和主线程进行线程分离
        }

        for(int i=0; i<maxfd+1; ++i)
        {
			// 判断从监听的文件描述符之后到maxfd这个范围内的文件描述符是否读缓冲区有数据
            if(i != lfd && FD_ISSET(i, &rdtemp))
            {
                // 通信接收数据，创建子线程
                pthread_t tid;
                FDInfo* info = (FDInfo*)malloc(sizeof(FDInfo));
                info->fd=i;
                info->rdset=&rdset;
                pthread_create(&tid,NULL,communication,info);
                pthread_detach(tid);//子线程和主线程进行线程分离

            }
        }
    }
    close(lfd);
    pthread_mutex_destroy(&mutex);
    return 0;
}
