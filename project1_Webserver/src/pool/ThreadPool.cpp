#include "ThreadPool.h"
#include <iostream>

ThreadPool::ThreadPool(size_t maxQueueSize, size_t threadCount)
    : maxQueueSize_(maxQueueSize) 
{
    // 创建工作线程，使用emplace_back直接构造，避免拷贝
    workers_.reserve(threadCount); // 预分配空间，避免重新分配
    for (size_t i = 0; i < threadCount; ++i) {
        // 每个线程运行workerLoop，传入this指针
        workers_.emplace_back(&ThreadPool::workerLoop, this);
    }
}

ThreadPool::~ThreadPool() {
    // 1. 设置停止标志（原子操作，线程安全）
    shutdown_.store(true);
    
    // 2. 唤醒所有等待的线程，让它们检查shutdown标志
    notEmpty_.notify_all();
    notFull_.notify_all();
    
    // 3. 等待所有工作线程结束（RAII核心：资源获取即初始化，释放即销毁）
    for (auto& worker : workers_) {
        if (worker.joinable()) { // 检查线程是否可join（防止重复join）
            worker.join();
        }
    }
}

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(queueMutex_);
        
        // 等待队列有空间（使用lambda作为谓词，防止虚假唤醒）
        // 注意：即使shutdown_为true，如果队列满了，我们仍然需要等待或者处理
        // 这里选择：如果shutdown，直接返回，不添加新任务
        notFull_.wait(lock, [this] { 
            return tasks_.size() < maxQueueSize_ || shutdown_.load(); 
        });
        
        if (shutdown_.load()) {
            return; // 线程池已停止，拒绝新任务
        }
        
        // 将任务加入队列（使用move语义，避免拷贝）
        tasks_.emplace(std::move(task));
    } // 锁在这里自动释放（RAII）
    
    // 唤醒一个等待的工作线程
    notEmpty_.notify_one();
}

void ThreadPool::workerLoop() {
    while (true) {
        std::function<void()> task;
        
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            
            // 等待任务到来或停止信号
            // 条件：队列非空 或 需要停止
            notEmpty_.wait(lock, [this] { 
                return !tasks_.empty() || shutdown_.load(); 
            });
            
            // 检查是否需要退出
            if (shutdown_.load() && tasks_.empty()) {
                return; // 停止且队列为空，线程退出
            }
            
            // 取出任务（使用move避免拷贝）
            task = std::move(tasks_.front());
            tasks_.pop();
        } // 锁释放
        
        // 唤醒可能正在等待队列空间的生产者线程
        notFull_.notify_one();
        
        // 执行任务（在锁外执行，避免阻塞其他线程）
        // 这是关键：如果在这里加锁，就变成串行执行了！
        task();
    }
}

size_t ThreadPool::queueSize() const {
    std::lock_guard<std::mutex> lock(queueMutex_); // 简洁的RAII锁
    return tasks_.size();
}