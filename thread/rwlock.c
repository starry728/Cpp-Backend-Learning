#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>

#define MAX 50
// 全局变量
int number;

// 创建一把互斥锁
// 全局变量, 多个线程共享
pthread_rwlock_t rwlock;

// 线程处理函数
void* read_num(void* arg)
{
    for(int i=0; i<MAX; ++i)
    {
        // 如果线程A加锁成功, 不阻塞
        // 如果B加锁成功, 线程A阻塞
        pthread_rwlock_rdlock(&rwlock);
        printf("Thread read, id = %lu, number = %d\n", pthread_self(), number);
        pthread_rwlock_unlock(&rwlock);
        usleep(rand()%5);
    }

    return NULL;
}

void* write_num(void* arg)
{
    for(int i=0; i<MAX; ++i)
    {
        // a加锁成功, b线程访问这把锁的时候是锁定的
        // 线程B先阻塞, a线程解锁之后阻塞解除
        // 线程B加锁成功了
        pthread_rwlock_wrlock(&rwlock);
        int cur = number;
        cur++;
        number = cur;
        printf("Thread write, id = %lu, number = %d\n", pthread_self(), number);
        pthread_rwlock_unlock(&rwlock);   
        usleep(5);
    }

    return NULL;
}

int main(int argc, const char* argv[])
{
    pthread_t p1[5], p2[3];//5个读，3个写

    // 初始化互斥锁
    pthread_rwlock_init(&rwlock, NULL);

    // 创建两个子线程
    for(int i=0;i<5;i++)
    {
        pthread_create(&p1[i], NULL, read_num, NULL);
    }
    for(int i=0;i<3;i++)
    {
        pthread_create(&p2[i], NULL, write_num, NULL);
    }
    // 阻塞，资源回收
    for(int i=0;i<5;i++)
    {
        pthread_join(p1[i], NULL);
    }
    for(int i=0;i<3;i++)
    {
        pthread_join(p2[i], NULL);

    }
    
    // 销毁互斥锁
    // 线程销毁之后, 再去释放互斥锁
    pthread_rwlock_destroy(&rwlock);

    return 0;
}
