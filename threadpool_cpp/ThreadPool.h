#pragma once
#include "TaskQueue.h"
#include "TaskQueue.cpp"//模板类需要.h和.cpp文件都包含

template <typename T>
class ThreadPool
{
public:
    //创建线程池并初始化
    ThreadPool(int min, int max);

    //销毁线程池
    ~ThreadPool();

    //给线程池添加任务
    void addTask(Task<T> task);

    //获取线程池中工作的线程的个数
    int getBusyNum();

    //获取线程池中活着的线程的个数
    int getAliveNum();

private:
    ///////////////////////////
    static void* worker(void* arg);
    static void* manager(void* arg);//静态，不可直接访问非静态成员变量，如private下面的
    void threadExit();//非静态，可以访问...

private:
    //任务队列
    TaskQueue<T>* taskQ;

    pthread_t managerID;    //管理者线程ID
    pthread_t *threadIDs;   //工作的线程ID
    int minNum;    //最小线程数量
    int maxNum;    //最大线程数量
    int busyNum;   //忙的线程数量
    int liveNum;   //存活的线程数量
    int exitNum;   //要销毁的线程数量
    pthread_mutex_t mutexPool;  //锁整个线程池
    pthread_cond_t notEmpty;    //任务队列是不是空了(消费者)
    static const int NUMBER = 2;

    bool shutdown;       //是不是要销毁线程池，销毁为1，不销毁为0
};