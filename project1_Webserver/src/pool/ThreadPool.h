#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <vector>
#include <atomic>
#include <memory>

/**
 * @brief 现代C++11线程池类
 * 
 * 相比C版本的主要改进：
 * 1. 使用std::function替代函数指针，支持lambda、bind等任意可调用对象
 * 2. 使用std::queue替代数组环形队列，更安全易用
 * 3. 使用RAII管理线程生命周期，自动join/detach
 * 4. 使用std::atomic替代int标志位，保证原子性
 * 5. 使用std::unique_ptr管理任务内存，防止泄漏
 */
class ThreadPool {
public:
    /**
     * @brief 构造函数，创建指定数量的工作线程
     * @param threadCount 工作线程数量，默认为硬件并发数
     * @param maxQueueSize 任务队列最大容量
     */
    explicit ThreadPool(size_t maxQueueSize = 1000, 
                       size_t threadCount = std::thread::hardware_concurrency());
    
    /**
     * @brief 析构函数，自动停止所有线程并清理资源
     * 使用RAII机制，无需手动调用destroy
     */
    ~ThreadPool();

    // 禁止拷贝和赋值（线程池通常不应该被复制）
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /**
     * @brief 添加任务到线程池
     * @param task 可调用对象（函数、lambda、bind表达式等）
     * 
     * 使用完美转发和可变参数模板，支持任意签名
     * 这里简化为void()类型，配合std::bind使用
     */
    void enqueue(std::function<void()> task);

    /**
     * @brief 获取当前队列中的任务数量
     */
    size_t queueSize() const;

    /**
     * @brief 检查线程池是否已停止
     */
    bool isShutdown() const { return shutdown_.load(); }

private:
    /**
     * @brief 工作线程的主循环函数
     */
    void workerLoop();

    std::vector<std::thread> workers_;           // 工作线程数组，使用vector动态管理
    std::queue<std::function<void()>> tasks_;    // 任务队列，使用STL队列
    
    mutable std::mutex queueMutex_;              // 保护任务队列的互斥锁
    std::condition_variable notEmpty_;           // 队列非空条件变量
    std::condition_variable notFull_;            // 队列非满条件变量
    
    size_t maxQueueSize_;                        // 队列最大容量
    std::atomic<bool> shutdown_{false};          // 原子标志位，防止指令重排序
};

#endif // THREADPOOL_H