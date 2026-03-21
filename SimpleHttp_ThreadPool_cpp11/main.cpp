#include "Server.h"
#include "ThreadPool.h"
#include <iostream>
#include <cstdlib>
#include <unistd.h>

int main(int argc, char* argv[]) {
    // 参数检查
    if (argc < 3) {
        std::cerr << "用法: " << argv[0] << " <端口> <资源目录>" << std::endl;
        std::cerr << "示例: " << argv[0] << " 8080 /var/www/html" << std::endl;
        return -1;
    }
    
    // 解析端口
    unsigned short port = static_cast<unsigned short>(std::atoi(argv[1]));
    if (port == 0) {
        std::cerr << "错误：无效的端口号" << std::endl;
        return -1;
    }
    
    // 切换工作目录
    if (chdir(argv[2]) == -1) {
        perror("chdir");
        return -1;
    }
    
    // 初始化线程池（任务队列容量1000）
    ThreadPool pool(1000);
    
    // 初始化监听套接字
    int lfd = initListenFd(port);
    if (lfd == -1) {
        return -1;
    }
    
    std::cout << "服务器启动成功，监听端口: " << port << std::endl;
    std::cout << "工作目录: " << argv[2] << std::endl;
    
    // 启动epoll事件循环（传入线程池引用）
    epollRun(lfd, pool);
    
    return 0;
}