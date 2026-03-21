#ifndef SUBREACTOR_H
#define SUBREACTOR_H

#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <memory>

// 前向声明
class ThreadPool;

/**
 * @brief 子Reactor类（IO线程）
 * 
 * 【多Reactor架构角色】
 * 每个SubReactor是一个独立的IO事件处理单元，运行在独立线程中。
 * 
 * 核心职责：
 * 1. 管理独立的epoll实例（epfd_），处理多个连接的IO事件
 * 2. 使用ET（边缘触发）模式高效处理高并发连接
 * 3. 读取HTTP请求数据（IO操作必须在IO线程完成，保证ET语义）
 * 4. 将业务逻辑（HTTP解析、文件发送）提交给ThreadPool异步处理
 * 5. 管理连接生命周期统计（connections_集合）
 * 
 * 线程安全设计：
 * - addConnection()：线程安全，可被MainReactor线程调用
 * - connections_：受connsMutex_保护
 * - running_：原子变量，控制事件循环启停
 * 
 * 【为什么IO读取不能交给ThreadPool？】
 * ET模式要求：事件触发后必须一次性读完所有数据，否则不会再次触发。
 * 如果交给ThreadPool，SubReactor线程继续epoll_wait，可能：
 * 1. 新事件到来，但ThreadPool还没读完，导致数据混乱
 * 2. 多个线程操作同一fd，产生竞态条件
 * 因此：IO读取必须在SubReactor线程完成，业务逻辑才能异步化。
 */
class SubReactor {
public:
    /**
     * @brief 构造函数
     * @param id Reactor标识（用于日志区分）
     * @param pool 全局业务逻辑线程池引用
     * 
     * 创建独立的epoll实例，初始化连接集合
     */
    explicit SubReactor(int id, ThreadPool& pool);
    
    /**
     * @brief 析构函数
     * 
     * 自动停止事件循环，关闭epoll实例，清理资源（RAII）
     */
    ~SubReactor();

    // 禁止拷贝（线程资源不可复制）
    SubReactor(const SubReactor&) = delete;
    SubReactor& operator=(const SubReactor&) = delete;

    /**
     * @brief 启动事件循环线程
     * 
     * 创建新线程运行eventLoop()，开始监听epoll事件
     */
    void start();

    /**
     * @brief 停止事件循环（优雅关闭）
     * 
     * 设置running_标志，等待线程结束（join）
     * 注意：不会强制关闭连接，由操作系统清理
     */
    void stop();

    /**
     * @brief 添加新连接到本SubReactor（线程安全）
     * @param cfd 客户端socket（调用者已设置为非阻塞）
     * @return 成功返回true，失败返回false（自动关闭cfd）
     * 
     * 【调用者】MainReactor线程（通过Round-Robin选择本SubReactor）
     * 【操作】将cfd加入epoll（ET模式），记录到connections_集合
     * 【线程安全】使用epoll_ctl是线程安全的，connections_受mutex保护
     */
    bool addConnection(int cfd);

    /**
     * @brief 获取当前管理的连接数（线程安全）
     * @return 连接集合大小
     */
    size_t connectionCount() const;

    /**
     * @brief 获取Reactor ID
     */
    int getId() const { return id_; }

private:
    /**
     * @brief 事件循环主函数（在独立线程中运行）
     * 
     * 核心循环：
     * while (running_) {
     *   1. epoll_wait等待IO事件（超时100ms，便于检查running_标志）
     *   2. 处理所有就绪事件：
     *      - EPOLLIN：调用handleRead读取数据
     *      - EPOLLERR/EPOLLHUP：调用handleError清理资源
     *   3. 继续循环
     * }
     */
    void eventLoop();

    /**
     * @brief 处理可读事件（EPOLLIN）
     * @param cfd 客户端socket
     * 
     * 【关键设计】IO读取 + 业务逻辑分离
     * 
     * 执行流程：
     * 1. 循环读取数据直到EAGAIN（ET模式要求一次性读完）
     * 2. 将读取到的数据封装为std::string（或保留在buffer）
     * 3. 创建FdInfo（只包含fd，数据通过lambda捕获传递）
     * 4. 提交lambda任务给ThreadPool：
     *    - lambda捕获：fd、读取到的数据buffer、this指针
     *    - ThreadPool线程执行：HTTP解析、文件发送、关闭连接
     *    - 执行完毕后，从connections_移除记录
     * 
     * 为什么数据要一起提交？
     * 避免ThreadPool线程再执行recv（可能阻塞或竞态），
     * 确保业务逻辑纯内存操作，不依赖IO。
     */
    void handleRead(int cfd);

    /**
     * @brief 处理错误/断开事件（EPOLLERR/EPOLLHUP）
     * @param cfd 客户端socket
     * 
     * 清理资源：
     * 1. 从epoll移除fd（epoll_ctl DEL）
     * 2. 关闭socket（close）
     * 3. 从connections_集合移除
     * 
     * 注意：这种情况通常是客户端异常断开，没有完整HTTP请求需要处理
     */
    void handleError(int cfd);

    /**
     * @brief 从连接集合中移除记录（内部使用）
     * @param cfd 客户端socket
     * 
     * 由handleRead提交的lambda任务在业务逻辑完成后调用，
     * 或使用handleError在连接异常时调用。
     */
    void removeConnection(int cfd);

    int id_;                              // Reactor标识（日志、调试）
    int epfd_;                            // 独立的epoll实例（本线程专用）
    ThreadPool& pool_;                    // 全局业务逻辑线程池引用
    std::thread thread_;                  // 事件循环线程
    
    mutable std::mutex connsMutex_;       // 保护connections_的互斥锁
    std::unordered_set<int> connections_; // 管理的连接集合（用于统计和清理）
    
    std::atomic<bool> running_{false};    // 运行标志（原子操作，无锁）
};

#endif // SUBREACTOR_H