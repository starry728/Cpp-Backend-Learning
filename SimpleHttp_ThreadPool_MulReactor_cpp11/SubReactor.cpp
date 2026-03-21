#include "SubReactor.h"
#include "ThreadPool.h"
#include "Server.h"
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <errno.h>

/**
 * @brief 构造函数：创建epoll实例，初始化成员
 */
SubReactor::SubReactor(int id, ThreadPool& pool) 
    : id_(id), pool_(pool), epfd_(-1) {
    
    // 创建独立的epoll实例（size参数在现代内核已废弃，但需>0）
    epfd_ = epoll_create(1);
    if (epfd_ == -1) {
        perror("epoll_create in SubReactor");
        throw std::runtime_error("Failed to create epoll in SubReactor");
    }
    
    std::cout << "[SubReactor " << id_ << "] 创建成功，epfd=" << epfd_ << std::endl;
}

/**
 * @brief 析构函数：RAII资源清理
 */
SubReactor::~SubReactor() {
    // 停止事件循环（如果还在运行）
    stop();
    
    // 关闭epoll实例
    if (epfd_ != -1) {
        close(epfd_);
        epfd_ = -1;
    }
    
    std::cout << "[SubReactor " << id_ << "] 已销毁" << std::endl;
}

/**
 * @brief 启动事件循环线程
 */
void SubReactor::start() {
    // 原子操作设置运行标志
    running_.store(true);
    
    // 创建线程运行eventLoop，使用成员函数指针+this
    thread_ = std::thread(&SubReactor::eventLoop, this);
    
    std::cout << "[SubReactor " << id_ << "] 事件循环线程已启动" << std::endl;
}

/**
 * @brief 停止事件循环（优雅关闭）
 */
void SubReactor::stop() {
    // 设置停止标志（原子操作，eventLoop会检测到）
    running_.store(false);
    
    // 等待线程结束（如果可join）
    if (thread_.joinable()) {
        thread_.join();
        std::cout << "[SubReactor " << id_ << "] 事件循环线程已停止" << std::endl;
    }
}

/**
 * @brief 添加新连接到本SubReactor（线程安全，供MainReactor调用）
 */
bool SubReactor::addConnection(int cfd) {
    // 【关键】设置非阻塞模式（ET模式要求）
    // 注意：MainReactor应该已经设置过，这里再设置一次确保万无一失
    int flags = fcntl(cfd, F_GETFL, 0);
    if (!(flags & O_NONBLOCK)) {
        fcntl(cfd, F_SETFL, flags | O_NONBLOCK);
    }
    
    // 构造epoll事件：ET（边缘触发）模式
    // 不使用EPOLLONESHOT，因为我们在单线程中处理，不会竞态
    struct epoll_event ev;
    ev.data.fd = cfd;
    ev.events = EPOLLIN | EPOLLET;  // 读事件 + 边缘触发
    
    // 加入epoll
    if (epoll_ctl(epfd_, EPOLL_CTL_ADD, cfd, &ev) == -1) {
        perror("epoll_ctl ADD in SubReactor");
        close(cfd);
        return false;
    }
    
    // 记录连接（线程安全）
    {
        std::lock_guard<std::mutex> lock(connsMutex_);
        connections_.insert(cfd);
    }
    
    std::cout << "[SubReactor " << id_ << "] 新连接 " << cfd 
              << "，当前连接数: " << connectionCount() << std::endl;
    return true;
}

/**
 * @brief 事件循环主函数（在独立线程中运行）
 * 
 * 【多Reactor核心】One Loop Per Thread
 */
void SubReactor::eventLoop() {
    std::cout << "[SubReactor " << id_ << "] 事件循环开始运行 (tid=" 
              << std::this_thread::get_id() << ")" << std::endl;
    
    // 预分配事件数组（避免每次循环分配）
    std::vector<struct epoll_event> events(1024);
    
    while (running_.load()) {
        // epoll_wait：等待IO事件（超时100ms，便于定期检查running_标志）
        // -1表示无限等待，但这样无法及时响应stop()，所以用100ms超时
        int num = epoll_wait(epfd_, events.data(), static_cast<int>(events.size()), 100);
        
        if (num == -1) {
            if (errno == EINTR) {
                // 被信号中断，继续循环
                continue;
            }
            perror("epoll_wait in SubReactor");
            break; // 严重错误，退出循环
        }
        
        // 处理所有就绪事件
        for (int i = 0; i < num; ++i) {
            int fd = events[i].data.fd;
            uint32_t evFlags = events[i].events;
            
            // 【错误处理】连接异常或挂断
            if (evFlags & (EPOLLERR | EPOLLHUP)) {
                std::cout << "[SubReactor " << id_ << "] 连接 " << fd 
                          << " 错误/挂断 (flags=" << evFlags << ")" << std::endl;
                handleError(fd);
                continue;
            }
            
            // 【可读事件】数据到达
            if (evFlags & EPOLLIN) {
                handleRead(fd);
            }
        }
    }
    
    std::cout << "[SubReactor " << id_ << "] 事件循环正常退出" << std::endl;
}

/**
 * @brief 处理可读事件（核心改造：IO线程读取，业务线程处理）
 * 
 * 【关键设计变更】修复ET模式Bug
 * 
 * 原Bug：把recv交给ThreadPool，破坏ET语义，导致数据丢失或竞态。
 * 
 * 修复方案：
 * 1. SubReactor线程（IO线程）：循环recv直到EAGAIN，确保读完所有数据
 * 2. 将读取到的数据buffer封装到lambda
 * 3. 提交lambda给ThreadPool：buffer已就绪，无需再recv
 * 4. ThreadPool线程：纯业务逻辑（HTTP解析、文件发送）
 * 
 * 这样保证：
 * - ET模式语义正确（IO线程一次性读完）
 * - 业务逻辑不阻塞IO（异步化到ThreadPool）
 * - 无竞态（一个fd只在一个SubReactor线程处理）
 */
void SubReactor::handleRead(int cfd) {
    // 【步骤1】IO线程：读取所有数据（ET模式要求）
    std::string buffer;
    buffer.reserve(4096);
    
    char tmp[1024];
    ssize_t len = 0;
    bool error = false;
    
    // 循环读取直到EAGAIN（数据读完）或错误
    while (true) {
        len = recv(cfd, tmp, sizeof(tmp), 0);
        
        if (len > 0) {
            // 读取到数据，追加到buffer
            buffer.append(tmp, static_cast<size_t>(len));
            
            // 安全检查：防止恶意大请求
            if (buffer.size() > 8 * 1024 * 1024) { // 8MB限制
                std::cerr << "[SubReactor " << id_ << "] 连接 " << cfd 
                          << " 请求过大，断开" << std::endl;
                error = true;
                break;
            }
        } else if (len == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 数据已读完（非阻塞socket的EAGAIN表示无更多数据）
                break;
            } else {
                // 其他错误
                perror("recv in SubReactor");
                error = true;
                break;
            }
        } else { // len == 0
            // 客户端关闭连接（发送了FIN）
            std::cout << "[SubReactor " << id_ << "] 连接 " << cfd 
                      << " 客户端关闭" << std::endl;
            error = true;
            break;
        }
    }
    
    // 如果读取过程中出错，直接清理资源
    if (error) {
        handleError(cfd);
        return;
    }
    
    // 检查是否读取到有效数据
    if (buffer.empty()) {
        // 虽然ET触发但无数据（理论上不应发生），直接返回等待下次事件
        return;
    }
    
    std::cout << "[SubReactor " << id_ << "] 连接 " << cfd 
              << " 读取完成，数据大小: " << buffer.size() << " 字节，提交给ThreadPool" << std::endl;
    
    // 【步骤2】提交业务逻辑给ThreadPool（数据已读取完毕）
    // 使用lambda捕获：cfd、buffer内容、this指针
    // 注意：buffer会复制到lambda中（std::string拷贝），确保ThreadPool线程安全访问
    
    pool_.enqueue([this, cfd, buffer]() mutable {
        // 【ThreadPool线程执行】
        // 此时IO读取已完成，buffer包含完整HTTP请求（或部分，取决于TCP分包）
        
        // 创建FdInfo（只包含fd，buffer通过lambda捕获传递）
        auto info = std::make_shared<FdInfo>(cfd);
        
        // 解析HTTP请求（使用Server.h中的函数）
        // 注意：recvHttpRequest期望从fd读取，但我们已经读取了
        // 因此我们需要修改策略：直接在这里调用parseRequestLine等
        
        // 查找HTTP请求行（第一行）
        auto pos = buffer.find("\r\n");
        if (pos != std::string::npos) {
            std::string requestLine = buffer.substr(0, pos);
            
            std::cout << "[ThreadPool] 处理连接 " << cfd 
                      << "，请求: " << requestLine << std::endl;
            
            // 调用原有的HTTP处理逻辑（parseRequestLine会发送响应）
            parseRequestLine(requestLine, cfd);
        } else {
            std::cerr << "[ThreadPool] 连接 " << cfd << " 请求格式错误" << std::endl;
        }
        
        // 发送完毕后关闭连接（HTTP短连接）
        close(cfd);
        
        // 从本SubReactor的连接集合中移除记录
        // 注意：这里需要小心，因为我们在ThreadPool线程，而SubReactor有自己的线程
        // 使用removeConnection（线程安全）
        this->removeConnection(cfd);
        
        std::cout << "[ThreadPool] 连接 " << cfd << " 处理完成" << std::endl;
    });
    
    // 【重要】从epoll移除该fd的监听（因为ThreadPool将处理并关闭它）
    // 如果不移除，ThreadPool关闭fd后，epoll可能报告错误事件
    epoll_ctl(epfd_, EPOLL_CTL_DEL, cfd, nullptr);
    
    // 注意：不从connections_移除，等ThreadPool处理完再移除（保证统计准确）
}

/**
 * @brief 处理错误事件，清理资源
 */
void SubReactor::handleError(int cfd) {
    // 从epoll移除
    epoll_ctl(epfd_, EPOLL_CTL_DEL, cfd, nullptr);
    
    // 关闭socket
    close(cfd);
    
    // 从连接集合移除
    removeConnection(cfd);
    
    std::cout << "[SubReactor " << id_ << "] 连接 " << cfd << " 已清理" << std::endl;
}

/**
 * @brief 从连接集合移除记录（线程安全）
 */
void SubReactor::removeConnection(int cfd) {
    std::lock_guard<std::mutex> lock(connsMutex_);
    connections_.erase(cfd);
}

/**
 * @brief 获取当前连接数（线程安全）
 */
size_t SubReactor::connectionCount() const {
    std::lock_guard<std::mutex> lock(connsMutex_);
    return connections_.size();
}