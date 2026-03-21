#include "Server.h"
#include "ThreadPool.h"
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <dirent.h>
#include <unistd.h>
#include <cctype>
#include <iostream>
#include <sstream>
#include <memory>
#include <map>

// 使用RAII包装epoll_event，避免C数组
using EpollEventArray = std::vector<struct epoll_event>;

int initListenFd(unsigned short port) {
    // 1. 创建监听套接字
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd == -1) {
        perror("socket");
        return -1;
    }
    
    // 2. 设置端口复用（使用SO_REUSEADDR）
    int opt = 1;
    if (setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        close(lfd);
        return -1;
    }
    
    // 3. 绑定地址
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY; // 绑定所有可用网络接口
    
    if (bind(lfd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == -1) {
        perror("bind");
        close(lfd);
        return -1;
    }
    
    // 4. 开始监听，backlog设为128
    if (listen(lfd, 128) == -1) {
        perror("listen");
        close(lfd);
        return -1;
    }
    
    return lfd;
}

int epollRun(int lfd, ThreadPool& pool) {
    // 1. 创建epoll实例
    int epfd = epoll_create(1);
    if (epfd == -1) {
        perror("epoll_create");
        return -1;
    }
    
    // 使用RAII确保epfd最终被关闭
    auto epfdGuard = std::shared_ptr<int>(new int(epfd), [](int* p) {
        close(*p);
        delete p;
    });
    
    // 2. 将监听套接字加入epoll（使用EPOLLET边缘触发）
    struct epoll_event ev;
    ev.data.fd = lfd;
    ev.events = EPOLLIN;
    
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev) == -1) {
        perror("epoll_ctl");
        return -1;
    }
    
    std::cout << "线程池初始化完成，" << std::thread::hardware_concurrency() 
              << "个工作线程已就绪！" << std::endl;
    
    // 3. 事件循环
    EpollEventArray evs(1024); // 使用vector替代C数组
    
    while (true) {
        int num = epoll_wait(epfd, evs.data(), static_cast<int>(evs.size()), -1);
        
        for (int i = 0; i < num; ++i) {
            int fd = evs[i].data.fd;
            
            if (fd == lfd) {
                // 监听套接字可读 = 有新连接
                acceptClient(lfd, epfd);
            } else {
                // 客户端套接字可读 = 有数据到来
                // 使用shared_ptr自动管理FdInfo生命周期
                auto info = std::make_shared<FdInfo>(fd, epfd);
                
                // 将任务打包进线程池
                // 使用lambda捕获shared_ptr，确保对象生命周期延长到任务完成
                pool.enqueue([info]() {
                    recvHttpRequest(info);
                });
            }
        }
    }
    
    return 0; // 实际上不会执行到这里
}

int acceptClient(int lfd, int epfd) {
    int cfd = accept(lfd, nullptr, nullptr);
    if (cfd == -1) {
        perror("accept");
        return -1;
    }
    
    // 设置非阻塞模式（使用fcntl）
    int flags = fcntl(cfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        close(cfd);
        return -1;
    }
    
    if (fcntl(cfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL");
        close(cfd);
        return -1;
    }
    
    // 加入epoll，使用ET模式（边缘触发）+ ONESHOT（防止多线程重复触发）
    struct epoll_event ev;
    ev.data.fd = cfd;
    ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
    
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev) == -1) {
        perror("epoll_ctl");
        close(cfd);
        return -1;
    }
    
    return 0;
}
void recvHttpRequest(std::shared_ptr<FdInfo> info) {
    // 使用C++11获取当前线程ID
    std::cout << "开始接收数据了, threadId: " 
              << std::this_thread::get_id() << "..." << std::endl;
    
    std::string buffer;  // 使用string动态扩容，避免固定大小数组
    buffer.reserve(4096);
    
    char tmp[1024];
    ssize_t len = 0;
    ssize_t total = 0;
    
    // ET模式需要循环读到EAGAIN
    while ((len = recv(info->fd, tmp, sizeof(tmp), 0)) > 0) {
        buffer.append(tmp, static_cast<size_t>(len));
        total += len;
        
        // 防止恶意请求导致内存无限增长（限制最大8MB）
        if (buffer.size() > 8 * 1024 * 1024) {
            std::cerr << "请求过大，拒绝服务" << std::endl;
            break;
        }
    }
    
    // 处理读取结果
    if (len == -1 && errno == EAGAIN) {
        // 数据读取完毕（EAGAIN表示当前无数据可读）
        // 查找请求行结束符\r\n
        auto pos = buffer.find("\r\n");
        
        if (pos != std::string::npos) {
            std::string requestLine = buffer.substr(0, pos);
            parseRequestLine(requestLine.c_str(), info->fd);
        } else {
            std::cout << "警告：请求格式不完整" << std::endl;
        }
    } else if (len == 0) {
        std::cout << "客户端主动断开了连接..." << std::endl;
    } else {
        perror("recv error");
    }
    
    // 【极其关键】统一清理资源
    // 从epoll移除并关闭socket
    epoll_ctl(info->epfd, EPOLL_CTL_DEL, info->fd, nullptr);
    close(info->fd);
    // info是shared_ptr，自动释放，无需手动free
}

int parseRequestLine(const std::string& line, int cfd) {
    // 解析请求行：GET /path/to/file HTTP/1.1
    std::string method, path, version;
    std::istringstream iss(line);
    
    if (!(iss >> method >> path >> version)) {
        std::cerr << "解析请求行失败" << std::endl;
        return -1;
    }
    
    std::cout << "method: " << method << ", path: " << path << std::endl;
    
    // 只处理GET请求
    if (strcasecmp(method.c_str(), "get") != 0) {
        return -1;
    }
    
    // URL解码
    std::string decodedPath = decodeMsg(path);
    
    // 安全检查：防止目录遍历攻击（..）
    if (decodedPath.find("..") != std::string::npos) {
        sendHeadMsg(cfd, 403, "Forbidden", getFileType(".html"), -1);
        const char* msg = "<html><body><h1>403 Forbidden</h1></body></html>";
        send(cfd, msg, strlen(msg), 0);
        return -1;
    }
    
    // 处理根目录请求
    std::string filePath;
    if (decodedPath == "/") {
        filePath = "./";
    } else {
        filePath = decodedPath.substr(1); // 去掉开头的/
    }
    
    // 获取文件属性
    struct stat st;
    if (stat(filePath.c_str(), &st) == -1) {
        // 文件不存在，返回404
        sendHeadMsg(cfd, 404, "Not Found", getFileType(".html"), -1);
        
        const char* err_msg = 
            "<html><head><title>404 Not Found</title></head>"
            "<body><h1 style=\"color:red;\">404 Not Found</h1>"
            "<p>The requested file does not exist on this server.</p>"
            "</body></html>";
        send(cfd, err_msg, strlen(err_msg), 0);
        return 0;
    }
    
    // 判断是目录还是文件
    if (S_ISDIR(st.st_mode)) {
        // 发送目录列表
        sendHeadMsg(cfd, 200, "OK", getFileType(".html"), -1);
        sendDir(filePath, cfd);
    } else {
        // 发送文件
        sendHeadMsg(cfd, 200, "OK", getFileType(filePath), static_cast<int>(st.st_size));
        sendFile(filePath, cfd);
    }
    
    return 0;
}

std::string getFileType(const std::string& name) {
    // 查找最后一个点号
    auto dotPos = name.rfind('.');
    if (dotPos == std::string::npos) {
        return "text/plain; charset=utf-8";
    }
    
    std::string ext = name.substr(dotPos);
    
    // 使用静态map提高性能（C++11 lambda初始化）
    static const auto mimeTypes = []() {
        std::map<std::string, std::string> m;
        m[".html"] = "text/html; charset=utf-8";
        m[".htm"] = "text/html; charset=utf-8";
        m[".jpg"] = "image/jpeg";
        m[".jpeg"] = "image/jpeg";
        m[".gif"] = "image/gif";
        m[".png"] = "image/png";
        m[".css"] = "text/css";
        m[".au"] = "audio/basic";
        m[".wav"] = "audio/wav";
        m[".avi"] = "video/x-msvideo";
        m[".mov"] = "video/quicktime";
        m[".qt"] = "video/quicktime";
        m[".mpeg"] = "video/mpeg";
        m[".mpe"] = "video/mpeg";
        m[".vrml"] = "model/vrml";
        m[".wrl"] = "model/vrml";
        m[".midi"] = "audio/midi";
        m[".mid"] = "audio/midi";
        m[".mp3"] = "audio/mpeg";
        m[".mp4"] = "video/mp4";
        m[".ogg"] = "application/ogg";
        m[".pac"] = "application/x-ns-proxy-autoconfig";
        return m;
    }();
    
    auto it = mimeTypes.find(ext);
    if (it != mimeTypes.end()) {
        return it->second;
    }
    
    return "text/plain; charset=utf-8";
}

int sendDir(const std::string& dirName, int cfd) {
    std::ostringstream html;
    html << "<html><head><title>" << dirName 
         << "</title></head><body><table>";
    
    // 扫描目录（使用scandir）
    struct dirent** namelist = nullptr;
    int num = scandir(dirName.c_str(), &namelist, nullptr, alphasort);
    
    if (num < 0) {
        perror("scandir");
        return -1;
    }
    
    // RAII确保namelist被释放
    auto dirGuard = std::shared_ptr<void>(nullptr, [&](void*) {
        for (int i = 0; i < num; ++i) {
            free(namelist[i]);
        }
        free(namelist);
    });
    
    for (int i = 0; i < num; ++i) {
        std::string name = namelist[i]->d_name;
        
        // 跳过当前目录和父目录
        if (name == "." || name == "..") continue;
        
        // 构建完整路径
        std::string fullPath = dirName + "/" + name;
        
        struct stat st;
        if (stat(fullPath.c_str(), &st) == -1) continue;
        
        // 生成表格行
        html << "<tr><td>";
        if (S_ISDIR(st.st_mode)) {
            html << "<a href=\"" << name << "/\">" << name << "/</a>";
        } else {
            html << "<a href=\"" << name << "\">" << name << "</a>";
        }
        html << "</td><td>" << st.st_size << "</td></tr>";
    }
    
    html << "</table></body></html>";
    
    std::string response = html.str();
    send(cfd, response.c_str(), response.size(), 0);
    
    return 0;
}

int sendFile(const std::string& fileName, int cfd) {
    int fd = open(fileName.c_str(), O_RDONLY);
    if (fd == -1) {
        perror("open file failed");
        return -1;
    }
    
    // RAII确保文件描述符关闭
    auto fileGuard = std::shared_ptr<void>(nullptr, [&](void*) {
        close(fd);
    });
    
    // 获取文件大小
    off_t fileSize = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    
    off_t offset = 0;
    
    // 使用sendfile零拷贝发送
    while (offset < fileSize) {
        ssize_t sent = sendfile(cfd, fd, &offset, static_cast<size_t>(fileSize - offset));
        
        if (sent == -1) {
            if (errno == EAGAIN) {
                // 发送缓冲区满，稍等
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            } else {
                perror("sendfile error");
                return -1;
            }
        }
    }
    
    return 0;
}

int sendHeadMsg(int cfd, int status, 
                const std::string& descr, 
                const std::string& type, 
                int length) {
    std::ostringstream header;
    header << "HTTP/1.1 " << status << " " << descr << "\r\n";
    header << "content-type: " << type << "\r\n";
    
    if (length >= 0) {
        header << "content-length: " << length << "\r\n";
    }
    header << "\r\n";
    
    std::string headerStr = header.str();
    send(cfd, headerStr.c_str(), headerStr.size(), 0);
    
    return 0;
}

int hexToDec(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

std::string decodeMsg(const std::string& from) {
    std::string to;
    to.reserve(from.size()); // 预分配，避免多次扩容
    
    for (size_t i = 0; i < from.size(); ++i) {
        if (from[i] == '%' && i + 2 < from.size() && 
            std::isxdigit(from[i+1]) && std::isxdigit(from[i+2])) {
            // 解码%XX格式
            char decoded = static_cast<char>(hexToDec(from[i+1]) * 16 + hexToDec(from[i+2]));
            to.push_back(decoded);
            i += 2;
        } else {
            to.push_back(from[i]);
        }
    }
    
    return to;
}