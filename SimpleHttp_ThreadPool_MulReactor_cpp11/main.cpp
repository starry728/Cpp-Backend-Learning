#include "Server.h"
#include "ThreadPool.h"
#include "SubReactor.h"
#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <vector>
#include <memory>

/**
 * @brief 多Reactor高并发HTTP服务器入口
 * 
 * 【架构总览】
 * 
 * 进程启动流程：
 * 1. 解析命令行参数（端口、资源目录、SubReactor数量）
 * 2. 切换工作目录到资源目录（HTTP服务的根目录）
 * 3. 创建全局ThreadPool（业务逻辑线程池）
 * 4. 创建SubReactor数组（IO线程池，每个带独立epoll）
 * 5. 创建监听socket（MainReactor使用）
 * 6. 启动MainReactor事件循环（epollRun）
 * 
 * 运行时线程模型：
 * - 1个MainReactor线程（main函数所在线程）：accept新连接，轮询分发
 * - N个SubReactor线程（N=CPU核心数或命令行指定）：IO事件处理，读取HTTP数据
 * - M个ThreadPool线程（M=硬件并发数）：HTTP解析、文件发送、业务逻辑
 * 
 * 【为什么这样设计？】
 * 
 * 单Reactor瓶颈：
 * - 一个线程同时处理accept、epoll_wait、recv、业务逻辑
 * - 慢连接（如网速慢的客户端）会阻塞整个事件循环
 * - 无法利用多核CPU（单线程）
 * 
 * 多Reactor优势：
 * - MainReactor只accept，不被慢连接阻塞（C10K问题的关键）
 * - 多个SubReactor并行处理IO，充分利用多核（水平扩展）
 * - ThreadPool隔离耗时操作（文件IO、HTTP解析），不阻塞事件循环
 * - 整体吞吐量 = N * 单Reactor吞吐量（理想情况下）
 * 
 * 【编译和运行】
 * 
 * 编译：
 *   g++ -std=c++11 -O2 -pthread -o server \
 *       main.cpp Server.cpp SubReactor.cpp ThreadPool.cpp
 * 
 * 运行：
 *   ./server 8080 ./www 4
 *   参数1：监听端口（8080）
 *   参数2：资源目录（./www，包含index.html等）
 *   参数3：SubReactor数量（4，默认为CPU核心数）
 * 
 * 测试：
 *   浏览器访问：http://localhost:8080/
 *   压测工具：wrk -t12 -c400 -d30s http://localhost:8080/
 */
int main(int argc, char* argv[]) {
    // 【1】参数解析
    if (argc < 3) {
        std::cerr << "用法: " << argv[0] << " <端口> <资源目录> [subReactor数量]" << std::endl;
        std::cerr << "示例: " << argv[0] << " 8080 ./www 4" << std::endl;
        std::cerr << "说明: subReactor数量默认为CPU核心数，建议设置为CPU核心数或2倍核心数" << std::endl;
        return -1;
    }
    
    // 解析端口号
    unsigned short port = static_cast<unsigned short>(std::atoi(argv[1]));
    if (port == 0) {
        std::cerr << "错误：无效的端口号" << std::endl;
        return -1;
    }
    
    // 【2】切换工作目录到资源目录
    // HTTP服务器的根目录，所有文件请求都相对于此目录
    if (chdir(argv[2]) == -1) {
        perror("chdir");
        std::cerr << "错误：无法进入资源目录: " << argv[2] << std::endl;
        return -1;
    }
    
    // 【3】确定SubReactor数量
    // 默认使用硬件并发数（CPU核心数），可通过命令行参数覆盖
    size_t subReactorCount = std::thread::hardware_concurrency();
    if (subReactorCount == 0) {
        subReactorCount = 4; // 获取失败时的默认值
    }
    
    if (argc >= 4) {
        int customCount = std::atoi(argv[3]);
        if (customCount > 0) {
            subReactorCount = static_cast<size_t>(customCount);
        } else {
            std::cerr << "警告：无效的subReactor数量，使用默认值: " << subReactorCount << std::endl;
        }
    }
    
    // 【4】创建全局业务逻辑线程池（ThreadPool）
    // 任务队列大小1000，工作线程数为硬件并发数
    // 用于异步处理HTTP解析、文件读取等耗时操作
    ThreadPool pool(1000);
    std::cout << "[Main] 业务线程池创建完成，工作线程: " 
              << std::thread::hardware_concurrency() << std::endl;
    
    // 【5】创建SubReactor数组（IO线程池）
    // 使用C++11兼容方式：new + unique_ptr（make_unique是C++14特性）
    std::vector<std::unique_ptr<SubReactor>> subReactors;
    subReactors.reserve(subReactorCount); // 预分配，避免重新分配
    
    for (size_t i = 0; i < subReactorCount; ++i) {
        // C++11写法：new SubReactor + 放入unique_ptr
        // C++14可改为：std::make_unique<SubReactor>(i, pool)
        subReactors.emplace_back(new SubReactor(static_cast<int>(i), pool));
    }
    std::cout << "[Main] SubReactor创建完成，数量: " << subReactorCount << std::endl;
    
    // 【6】初始化监听套接字（MainReactor使用）
    int lfd = initListenFd(port);
    if (lfd == -1) {
        std::cerr << "错误：初始化监听套接字失败" << std::endl;
        return -1;
    }
    std::cout << "[Main] 监听套接字创建完成，端口: " << port << std::endl;
    
    // 【7】启动多Reactor服务器（进入MainReactor事件循环）
    // 此函数阻塞，直到服务器停止（收到信号或致命错误）
    std::cout << "[Main] 启动多Reactor服务器..." << std::endl;
    int ret = epollRun(lfd, pool, subReactors);
    
    // 【8】清理（实际上epollRun通常不会返回，除非出错）
    std::cout << "[Main] 服务器已停止，返回码: " << ret << std::endl;
    
    // SubReactor和ThreadPool的析构函数会自动清理资源（RAII）
    // unique_ptr会自动delete SubReactor对象
    
    return ret;
}