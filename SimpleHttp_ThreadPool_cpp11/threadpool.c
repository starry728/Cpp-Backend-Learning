#include "threadpool.h"
#include <stdlib.h>
#include <stdio.h>

// 工作线程的运行函数
void* worker(void* arg)
{
    ThreadPool* pool = (ThreadPool*)arg;
    while (1)
    {
        pthread_mutex_lock(&pool->mutexPool); // 加锁
        
        // 如果队列为空，且没有要求关闭，就阻塞在这里睡觉，等待新任务唤醒
        while (pool->queueSize == 0 && !pool->shutdown)
        {
            pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);
        }

        // 判断是否需要销毁线程
        if (pool->shutdown)
        {
            pthread_mutex_unlock(&pool->mutexPool);
            pthread_exit(NULL);
        }

        // 从队列中拿出一个任务
        Task task;
        task.function = pool->taskQ[pool->queueFront].function;
        task.arg = pool->taskQ[pool->queueFront].arg;

        // 移动队头，任务数减1
        pool->queueFront = (pool->queueFront + 1) % pool->queueCapacity;
        pool->queueSize--;

        // 唤醒可能因为队列满了而阻塞的添加任务线程（生产者）
        pthread_cond_signal(&pool->notFull);
        pthread_mutex_unlock(&pool->mutexPool); // 解锁

        // === 执行任务（去调用你的 recvHttpRequest） ===
        // 注意：执行任务时绝不能加锁，否则就变成单线程排队执行了！
        task.function(task.arg);
    }
    return NULL;
}

// 创建线程池
ThreadPool* threadPoolCreate(int maxQueueSize)
{
    ThreadPool* pool = (ThreadPool*)malloc(sizeof(ThreadPool));
    pool->queueCapacity = maxQueueSize;
    pool->queueSize = 0;
    pool->queueFront = 0;
    pool->queueRear = 0;
    pool->shutdown = 0;

    // 分配任务队列内存
    pool->taskQ = (Task*)malloc(sizeof(Task) * maxQueueSize);
    
    // 初始化锁和条件变量
    pthread_mutex_init(&pool->mutexPool, NULL);
    pthread_cond_init(&pool->notEmpty, NULL);
    pthread_cond_init(&pool->notFull, NULL);

    // 预先创建好 8 个工作线程（你可以根据电脑 CPU 核数改）
    for (int i = 0; i < 8; ++i)
    {
        pthread_create(&pool->threadID[i], NULL, worker, pool);
        pthread_detach(pool->threadID[i]); // 线程分离，防止内存泄露
    }

    return pool;
}

// 往线程池里添加任务（主线程调用）
void threadPoolAdd(ThreadPool* pool, void*(*func)(void*), void* arg)
{
    pthread_mutex_lock(&pool->mutexPool);

    // 如果队列满了，就阻塞等待
    while (pool->queueSize == pool->queueCapacity && !pool->shutdown)
    {
        pthread_cond_wait(&pool->notFull, &pool->mutexPool);
    }
    if (pool->shutdown)
    {
        pthread_mutex_unlock(&pool->mutexPool);
        return;
    }

    // 将任务添加到队尾
    pool->taskQ[pool->queueRear].function = func;
    pool->taskQ[pool->queueRear].arg = arg;
    pool->queueRear = (pool->queueRear + 1) % pool->queueCapacity;
    pool->queueSize++;

    // 唤醒一个正在睡觉的工作线程起来干活！
    pthread_cond_signal(&pool->notEmpty);
    pthread_mutex_unlock(&pool->mutexPool);
}