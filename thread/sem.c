#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>


pthread_mutex_t mutex;
//生产者的信号量
sem_t semp;
//消费者的信号量
sem_t semc;
//链表的节点类型(任务队列)
struct Node
{
    int number;
    struct Node* next;
};
//链表头节点
struct Node* head=NULL;
//生产者
void* producer(void* arg)
{
    while (1)
    {
        sem_wait(&semp);//检验一下生产者是否有资源
        pthread_mutex_lock(&mutex);
        //创建新节点
        struct Node* newNode = (struct Node*)malloc(sizeof(struct Node));
        //初始化节点
        newNode->number=rand()%1000;
        newNode->next=head;
        head=newNode;
        printf("生产者，id：%ld，number：%d\n",pthread_self(),newNode->number);
        pthread_mutex_unlock(&mutex);
        sem_post(&semc);

        sleep(rand()%3);//单位是s
    }
    
    return NULL;
}
//消费者
void* consumer(void* arg)
{
    while (1)//消费：从头部取节点
    {
        sem_wait(&semc);
        pthread_mutex_lock(&mutex);
        struct Node* node = head;
        printf("消费者，id：%ld，number：%d\n",pthread_self(),node->number);
        head=head->next;
        free(node);
        pthread_mutex_unlock(&mutex);
        sem_post(&semp);

        sleep(rand()%3);//单位是s
    }
    
    return NULL;
}
int main()
{
    //生产者
    sem_init(&semp, 0, 5);
    //消费者 - > 资源初始化为0，消费者线程启动就阻塞了
    sem_init(&semc, 0, 0);
    pthread_mutex_init(&mutex,NULL);
    

    pthread_t t1[5],t2[5];
    for(int i=0;i<5;i++)
    {
        pthread_create(&t1[i],NULL,producer,NULL);
    }
    for(int i=0;i<5;i++)
    {
        pthread_create(&t2[i],NULL,consumer,NULL);
    }
    for(int i=0;i<5;i++)
    {
        pthread_join(t1[i],NULL);
    }
    for(int i=0;i<5;i++)
    {
        pthread_join(t2[i],NULL);
    }
    pthread_mutex_destroy(&mutex);
    sem_destroy(&semp);
    sem_destroy(&semc);
    return 0;
}