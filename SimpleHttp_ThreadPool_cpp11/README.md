# C++语言 epoll+线程池，实现一个单反应堆(单reactor的变体)服务器

# 高并发 HTTP 服务器 (C++11 版)

基于单 Reactor + 线程池模型的高并发 Web 服务器，使用现代 C++11 特性重构，支持静态文件服务和目录浏览。

## 架构特点

- **事件驱动**：epoll ET 模式 + EPOLLONESHOT，高效处理海量连接
- **线程池**：动态工作线程数（默认硬件并发数），任务队列容量可配置
- **零拷贝**：sendfile 传输文件，减少用户态/内核态数据拷贝
- **RAII 资源管理**：智能指针 + 标准容器，杜绝内存泄漏

## 核心改进（C++11 现代化）

| 模块 | 关键技术 | 优势 |
|------|---------|------|
| 线程池 | `std::thread`, `std::condition_variable`, `std::function` | 类型安全，自动生命周期管理 |
| 资源管理 | `std::shared_ptr`, RAII 守卫 | 异常安全，无内存泄漏 |
| 字符串处理 | `std::string`, `std::ostringstream` | 动态扩容，边界安全 |
| 并发控制 | `std::atomic&lt;bool&gt;`, lambda 谓词 | 防止虚假唤醒，可见性保证 |

## 快速开始

### 编译

```bash
g++ -std=c++11 -pthread -O2 -Wall \
    main.cpp Server.cpp ThreadPool.cpp \
    -o webserver