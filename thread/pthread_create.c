#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

struct Test
{
    int num;
    int age;
};

void* callback(void* arg)
{
    for(int i=0;i<5;i++)
    {
        printf("子线程：i=%d\n",i);
    }
    printf("子线程：%ld\n",pthread_self());
    struct Test* t = (struct Test*)arg;
    t->num=100;
    t->age=6;
    pthread_exit(&t);
    return NULL;
}
int main(void)
{
    struct Test t;
    pthread_t tid;
    pthread_create(&tid,NULL,callback,&t);
    printf("主线程：%ld\n",pthread_self());
    pthread_detach(tid);
    pthread_exit(NULL);
    return 0;
}