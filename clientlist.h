#pragma once
#include <stdbool.h>
#include <pthread.h>

//定义链表的节点
struct ClientInfo
{
    int fd;//通信
    int count;
    pthread_t pid;
    struct ClientInfo* next;
};

//创建一个链表，带头节点，返回头节点
struct ClientInfo* createList();
//添加一个节点（头插法），返回这个节点的地址
struct ClientInfo* prependNode(struct ClientInfo* head, int fd);
//删除指定的节点
bool removeNode(struct ClientInfo* head, int fd);
//销毁链表
void freeCliList(struct ClientInfo* head);
