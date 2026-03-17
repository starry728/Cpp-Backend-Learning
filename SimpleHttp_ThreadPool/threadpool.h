#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>

// 任务结构体
typedef struct Task
{
    void* (*function)(void* arg); // 任务运行的函数指针（正好匹配你的 recvHttpRequest）
    void* arg;                    // 函数需要的参数（你的 info）
} Task;

// 线程池结构体
typedef struct ThreadPool
{
    Task* taskQ;              // 任务队列（数组模拟环形队列）
    int queueCapacity;        // 队列容量
    int queueSize;            // 当前任务个数
    int queueFront;           // 队头
    int queueRear;            // 队尾

    pthread_t threadID[8];    // 工作线程数组（假设固定8个线程）
    
    pthread_mutex_t mutexPool; // 互斥锁：保护整个线程池
    pthread_cond_t  notEmpty;  // 条件变量：队列是不是空了？
    pthread_cond_t  notFull;   // 条件变量：队列是不是满了？

    int shutdown;             // 线程池销毁标志（1代表销毁，0代表正常）
} ThreadPool;

// 初始化线程池
ThreadPool* threadPoolCreate(int queueSize);
// 给线程池添加任务
void threadPoolAdd(ThreadPool* pool, void*(*func)(void*), void* arg);

#endif