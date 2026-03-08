#include <stdio.h>
#include "socket.h"
#include <pthread.h>
#include <stdlib.h>//free()需要
#include <unistd.h>//close()需要
#include <string.h>

pthread_mutex_t mutex;
struct FdInfo
{
    int fd;
    int count;//记录有多少次没有收到服务器回复的心跳包数据了
};
void* parseRecvMessage(void* arg)//子线程的回调函数
{
    struct FdInfo *info = (struct FdInfo*)arg;
    while(1)
    {
        char* buffer;
        enum Type t;
        recvMessage(info->fd, &buffer, &t);
        if(buffer==NULL)
        {
            continue;
        }
        else
        {
            if(t == Heart)
            {
                printf("心跳包...%s\n",buffer);
                pthread_mutex_lock(&mutex);
                info->count = 0;//重置
                pthread_mutex_unlock(&mutex);
            }
            else
            {
                printf("数据包：%s\n",buffer);
            }
            free(buffer);
        }
    }
    return NULL;
}

//1.给服务器发送心跳包数据
//2.检测心跳包，看看能否收到服务器回复的数据
void* heartBeat(void* arg)
{
    struct FdInfo *info = (struct FdInfo*)arg;
    while(1)
    {
        pthread_mutex_lock(&mutex);
        info->count++;//默认没有收到服务器回复的心跳包数据
        printf("fd = %d, count = %d\n", info->fd, info->count);
        if(info->count > 5)
        {
            //客户端和服务器断开了连接
            printf("客户端和服务器断开了连接...\n");
            close(info->fd);
            //释放套接字资源，退出客户端程序
            exit(0);//进程退出（子线程、主线程资源全都被释放掉了）
        }
        pthread_mutex_unlock(&mutex);
        sendMessage(info->fd, "hello", 5, Heart);
        sleep(3);
    }
    return NULL;
}
int main()
{
    struct FdInfo info;
    unsigned short port = 7777;
    const char* ip = "127.0.0.1";//本机
    info.fd = initSocket();
    info.count = 0;
    connectToHost(info.fd, port, ip);

    pthread_mutex_init(&mutex, NULL);

    //创建接收数据的子线程
    pthread_t pid;
    pthread_create(&pid, NULL, parseRecvMessage, &info);

    //添加心跳包子线程
    pthread_t pid1;
    pthread_create(&pid1, NULL, heartBeat, &info);    
    while(1)
    {
        const char* data = "你好，cxy....";
        //发送数据
        sendMessage(info.fd, data, strlen(data), Message);
        sleep(2);
    }

    pthread_join(pid,NULL);
    pthread_join(pid1,NULL);
    pthread_mutex_destroy(&mutex);
    return 0;
}