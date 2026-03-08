#include <stdio.h>
#include "socket.h"
#include <pthread.h>
#include "clientlist.h"
#include <unistd.h>//close()需要
#include <stdlib.h>
#include <string.h>


pthread_mutex_t mutex;
struct FdInfo
{
    int fd;
    int count;//记录有多少次没有收到服务器回复的心跳包数据了
};
void* parseRecvMessage(void* arg)//子线程的回调函数
{
    struct ClientInfo* info = (struct ClientInfo*)arg;
    while(1)
    {
        char* buffer;
        enum Type t;
        int len = recvMessage(info->fd, &buffer, &t);
        if(buffer==NULL)
        {
            printf("fd = %d, 通信的子线程退出了...\n", info->fd);
            pthread_exit(NULL);
        }
        else
        {
            if(t == Heart)
            {
                printf("心跳包...%s\n",buffer);
                pthread_mutex_lock(&mutex);
                info->count = 0;//重置
                pthread_mutex_unlock(&mutex);
                sendMessage(info->fd, buffer, len, Heart);
            }
            else
            {
                const char* pt = "愿世界和平...";
                printf("数据包：%s\n",buffer);
                sendMessage(info->fd, pt, strlen(pt), Message);
            }
            free(buffer);
        }
    }
    return NULL;
}

// 建议在 server.c 定义一个专门的链表锁，或者复用 mutex（但在你的代码里 mutex 只是为了保护 count，粒度不够）
// 这里为了简单，假设你扩大了 mutex 的范围，或者只修逻辑错误：

void* heartBeat(void* arg)
{
    struct ClientInfo* head = (struct ClientInfo*)arg;
    struct ClientInfo* p = NULL;
    struct ClientInfo* tmp = NULL; // 用于保存下一个节点

    while(1)
    {
        pthread_mutex_lock(&mutex); // 【加锁】保护整个链表遍历过程
        p = head->next;
        while(p)
        {
            tmp = p->next; // 【关键】先保存下一个节点
            
            p->count++; 
            printf("fd = %d, count = %d\n", p->fd, p->count);
            
            if(p->count > 5)
            {
                printf("客户端fd = %d 和服务器断开了连接...\n" ,p->fd);
                close(p->fd);
                pthread_cancel(p->pid);
                // 删除节点，这里已经持锁，removeNode 内部不需要再加锁(如果内部没锁的话)
                // 注意：removeNode 函数是根据 fd 从头查找删除的，效率较低但逻辑尚可
                removeNode(head, p->fd); 
            }
            
            p = tmp; // 【关键】使用保存的节点继续遍历
        }
        pthread_mutex_unlock(&mutex); // 【解锁】
        
        sleep(3);
    }
    return NULL;
}
int main()
{
    unsigned short port = 7777;
    int lfd = initSocket();
    setListen(lfd, port);
    ///创建链表
    struct ClientInfo* head = createList();
    pthread_mutex_init(&mutex, NULL);

    //添加心跳包子线程
    pthread_t pid1;
    pthread_create(&pid1, NULL, heartBeat, head);    

    while(1)
    {
        int sockfd = acceptConnect(lfd, NULL);
        if(sockfd == -1)
        {
            continue;
        }
        struct ClientInfo* node = prependNode(head, sockfd);

        const char* data = "你好，cxy....";
        //创建接收数据的子线程

        pthread_create(&node->pid, NULL, parseRecvMessage, node);
        pthread_detach(node->pid);//线程分离：子线程结束后会自动释放
    }

    pthread_join(pid1,NULL);
    pthread_mutex_destroy(&mutex);
    close(lfd);
    return 0;
}