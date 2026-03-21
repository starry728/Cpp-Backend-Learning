#include "Server.h"
#include "ThreadPool.h"
#include "SubReactor.h"
#include "ConnectionPool.h"
#include "SessionManager.h"
#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <vector>
#include <memory>
#include <signal.h>
#include <atomic>
#include <climits>
#include <cstring>
#include <libgen.h>  // 包含 dirname 函数

std::atomic<bool> g_running{true};

void signalHandler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        std::cout << "\n[Main] 收到终止信号，准备关闭服务器..." << std::endl;
        g_running = false;
    }
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    if (argc < 3) {
        std::cerr << "用法: " << argv[0] << " <端口> <资源目录> [subReactor数量]" << std::endl;
        std::cerr << "示例: " << argv[0] << " 8080 ./www 4" << std::endl;
        return -1;
    }
    
    unsigned short port = static_cast<unsigned short>(std::atoi(argv[1]));
    if (port == 0) {
        std::cerr << "错误：无效的端口号" << std::endl;
        return -1;
    }
    
    // 【关键修复】正确处理前端目录和资源目录
    // 获取可执行文件所在目录
    char exePath[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len == -1) {
        // 如果 readlink 失败，使用 argv[0]
        strncpy(exePath, argv[0], sizeof(exePath) - 1);
    }
    exePath[sizeof(exePath) - 1] = '\0';
    
    char* exeDir = dirname(exePath);
    std::string exeDirStr = exeDir;
    
    // 前端目录：可执行文件所在目录的 www 子目录
    std::string webRoot = exeDirStr + "/www";
    
    // 资源目录：命令行参数指定的目录
    std::string resourceRoot = argv[2];
    char resolvedPath[PATH_MAX];
    
    if (realpath(resourceRoot.c_str(), resolvedPath) == nullptr) {
        perror("realpath");
        std::cerr << "错误：资源目录不存在: " << resourceRoot << std::endl;
        return -1;
    }
    resourceRoot = resolvedPath;
    
    // 设置环境变量
    setenv("WEB_ROOT", webRoot.c_str(), 1);
    setenv("PIC_ROOT", resourceRoot.c_str(), 1);
    
    std::cout << "[Main] 可执行文件目录: " << exeDirStr << std::endl;
    std::cout << "[Main] 前端目录: " << webRoot << std::endl;
    std::cout << "[Main] 资源目录: " << resourceRoot << std::endl;
    
    // 检查前端目录是否存在
    if (access(webRoot.c_str(), F_OK) == -1) {
        std::cerr << "错误：前端目录不存在: " << webRoot << std::endl;
        std::cerr << "请确保在可执行文件所在目录下有 www 文件夹" << std::endl;
        return -1;
    }
    
    // 切换到前端目录
    if (chdir(webRoot.c_str()) == -1) {
        perror("chdir");
        return -1;
    }
    
    size_t subReactorCount = std::thread::hardware_concurrency();
    if (subReactorCount == 0) subReactorCount = 4;
    if (argc >= 4) {
        int customCount = std::atoi(argv[3]);
        if (customCount > 0) subReactorCount = static_cast<size_t>(customCount);
    }
    
    ThreadPool pool(1000);
    std::cout << "[Main] 业务线程池创建完成" << std::endl;
    
    std::vector<std::unique_ptr<SubReactor>> subReactors;
    subReactors.reserve(subReactorCount);
    for (size_t i = 0; i < subReactorCount; ++i) {
        subReactors.emplace_back(new SubReactor(static_cast<int>(i), pool));
    }
    std::cout << "[Main] SubReactor创建完成，数量: " << subReactorCount << std::endl;
    
    std::cout << "[Main] 正在初始化数据库连接池..." << std::endl;
    ConnectionPool* dbPool = ConnectionPool::getConnectPool();
    if (!dbPool) {
        std::cerr << "[Main] 错误：数据库连接池初始化失败" << std::endl;
    } else {
        auto testConn = dbPool->getConnection();
        if (testConn) {
            std::cout << "[Main] ✅ 数据库连接池初始化成功" << std::endl;
        } else {
            std::cerr << "[Main] ⚠️ 数据库连接池测试失败" << std::endl;
        }
    }
    
    int lfd = initListenFd(port);
    if (lfd == -1) {
        std::cerr << "错误：初始化监听套接字失败" << std::endl;
        return -1;
    }
    
    std::cout << "[Main] 服务器监听端口: " << port << std::endl;
    std::cout << "[Main] 前端目录: " << webRoot << std::endl;
    std::cout << "[Main] 资源目录: " << resourceRoot << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  多Reactor Web服务器已启动" << std::endl;
    std::cout << "  功能：静态文件 + 用户认证 + 文件管理" << std::endl;
    std::cout << "========================================" << std::endl;
    
    int ret = epollRun(lfd, pool, subReactors, g_running);
    
    std::cout << "[Main] 服务器已停止，返回码: " << ret << std::endl;
    
    return ret;
}