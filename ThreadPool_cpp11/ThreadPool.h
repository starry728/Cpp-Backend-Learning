#pragma once
#include <thread>
#include <vector>//若干工作线程的数组
#include <atomic>//原子变量
#include <queue>//任务队列
#include <functional>//包装器和绑定器
#include <mutex>
#include <condition_variable>
#include <map>
#include <future>
#include <memory>

using namespace std;
/*
* 构成：
* 1.管理者线程 -> 子线程：1个
*       - 控制工作线程的数量：增加或减少
* 2.若干工作线程 -> 子线程：n个
*       - 从任务队列中取线程
*       - 若任务队列为空，被阻塞（被条件变量阻塞）
*       - 线程同步（互斥锁）
*       - 当前数量，空闲的线程数量
*       - 最小，最大线程数量
* 3.任务队列 -> stl -> queue
*       - 互斥锁
*       - 条件变量
* 4.线程池开关 -> bool
*/
class ThreadPool
{
public:
    ThreadPool(int min = 2, int max = thread::hardware_concurrency());//自动读取电脑 核 数
    ~ThreadPool();

    //添加任务 -> 任务队列；参数是可调用对象
    void addTask(function<void(void)>task);

    template<typename F,typename... Args>//F可能是左值引用，可能是右值引用
    auto addTask(F&& f, Args&&... args)->future<typename result_of<F(Args...)>::type>//返回值是future类型的对象
    {//要写在同一个文件中
        //1. packaged_task
        using returnType = typename result_of<F(Args...)>::type;
        auto mytask = make_shared<packaged_task<returnType()>> (
            bind(forward<F>(f), forward<Args>(args)...)
            );
        //2. 得到future
        future<returnType> res = mytask->get_future();
        //3. 任务函数添加到任务队列
        m_queueMutex.lock();
        m_tasks.emplace([mytask]() {
            (*mytask)();
            });//匿名函数
        m_queueMutex.unlock();
        m_condition.notify_one();
        
        return res;
    }

private:
    void manager(void);
    void worker(void);

private:
    thread* m_manager;
    map<thread::id, thread>m_workers;
    vector<thread::id> m_ids;//存储已经退出了的任务函数的线程的ID
    atomic<int> m_minThread;//原子变量 最小线程数量
    atomic<int> m_maxThread;//原子变量 最大线程数量
    atomic<int> m_curThread;//原子变量 当前线程数量
    atomic<int> m_idleThread;//原子变量 空闲线程数量
    atomic<int> m_exitThread;//原子变量 退出线程数量
    atomic<bool> m_stop;//原子变量 线程池开关
    queue<function<void(void)>>m_tasks;//任务——处理函数（函数名就是函数地址），所以队列中存的是函数地址——函数指针；可调用对象包装器（类型统一）;绑定bind
    mutex m_queueMutex;
    mutex m_idsMutex;
    condition_variable m_condition;//条件变量——用来阻塞消费者线程的（当任务队列为空时）
};

