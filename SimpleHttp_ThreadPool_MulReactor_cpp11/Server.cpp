#include "Server.h"
#include "ThreadPool.h"
#include "SubReactor.h"
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
#include <atomic>
#include <map>

// 新增：轮询计数器，用于MainReactor负载均衡分发连接
// 使用原子变量保证线程安全（多线程环境下可能被多个MainReactor线程访问，虽然本项目是单MainReactor）
static std::atomic<size_t> roundRobinCounter{0};

/**
 * @brief 初始化监听套接字
 * 
 * 创建TCP监听socket，设置端口复用和非阻塞模式
 */
int initListenFd(unsigned short port) {
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd == -1) {
        perror("socket");
        return -1;
    }
    
    // 端口复用，避免TIME_WAIT状态导致bind失败
    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // 设置监听socket为非阻塞（虽然accept本身不会阻塞太久，但好习惯）
    int flags = fcntl(lfd, F_GETFL, 0);
    fcntl(lfd, F_SETFL, flags | O_NONBLOCK);
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(lfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(lfd);
        return -1;
    }
    
    if (listen(lfd, 128) == -1) {
        perror("listen");
        close(lfd);
        return -1;
    }
    
    return lfd;
}

/**
 * @brief 多Reactor架构主入口（MainReactor）
 * 
 * 【架构设计】One Loop Per Thread + ThreadPool
 * 
 * 线程分工：
 * 1. MainReactor线程（当前线程）：运行本函数
 *    - 只监听lfd的可读事件（新连接到来）
 *    - 调用accept获取新连接cfd
 *    - 通过Round-Robin算法选择SubReactor
 *    - 将cfd分发给选中的SubReactor（线程安全地加入其epoll）
 *    - 不处理任何IO读写，保证accept的高响应速度
 * 
 * 2. SubReactor线程（多个，由subReactors管理）：
 *    - 每个线程独立运行一个epoll_wait事件循环
 *    - 管理自己的连接集合（通过epfd）
 *    - 处理连接的EPOLLIN事件：读取HTTP请求数据（IO操作）
 *    - 将读取到的数据封装为task，提交给ThreadPool处理业务逻辑
 *    - 不处理耗时操作（如文件IO、HTTP解析），保证事件响应速度
 * 
 * 3. ThreadPool线程（全局共享）：
 *    - 处理HTTP协议解析（parseRequestLine）
 *    - 文件读取和发送（sendFile/sendDir）
 *    - 发送HTTP响应后关闭连接
 *    - 处理完业务后，从SubReactor的连接集合中移除记录
 * 
 * 优势：
 * - MainReactor专注accept，不被慢连接阻塞
 * - SubReactor并行处理多个连接的IO，充分利用多核
 * - ThreadPool隔离业务逻辑和IO，避免阻塞事件循环
 * - 可扩展性：增加SubReactor数量即可提升并发连接处理能力
 */
int epollRun(int lfd, ThreadPool& pool, std::vector<std::unique_ptr<SubReactor>>& subReactors) {
    // 【1】创建MainReactor的epoll实例（仅用于监听lfd）
    int epfd = epoll_create(1);
    if (epfd == -1) {
        perror("epoll_create in MainReactor");
        return -1;
    }
    
    // 将监听fd加入MainReactor的epoll，使用LT模式（水平触发）
    // 原因：accept操作很快，LT模式足够，且代码更简单
    struct epoll_event ev;
    ev.data.fd = lfd;
    ev.events = EPOLLIN;
    
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev) == -1) {
        perror("epoll_ctl in MainReactor");
        close(epfd);
        return -1;
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "  多Reactor高并发服务器启动" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "[MainReactor] 监听端口，分发新连接" << std::endl;
    std::cout << "[SubReactor]  数量: " << subReactors.size() << "（IO线程）" << std::endl;
    std::cout << "[ThreadPool]  工作线程: " << std::thread::hardware_concurrency() << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    
    // 【2】启动所有SubReactor（它们各自创建线程运行eventLoop）
    for (auto& sub : subReactors) {
        sub->start();
    }
    
    // 【3】MainReactor事件循环：只处理accept
    std::vector<struct epoll_event> events(1024);
    
    while (true) {
        // 阻塞等待事件（-1表示无限等待，也可改为定时轮询以实现优雅退出）
        int num = epoll_wait(epfd, events.data(), static_cast<int>(events.size()), -1);
        
        if (num == -1) {
            if (errno == EINTR) continue; // 被信号中断，重新等待
            perror("epoll_wait in MainReactor");
            break;
        }
        
        // 处理所有就绪事件（实际上只有lfd一个，但代码保持通用性）
        for (int i = 0; i < num; ++i) {
            int fd = events[i].data.fd;
            
            if (fd == lfd) {
                // 【核心逻辑】新连接到来：accept并分发给SubReactor
                
                // 循环accept直到EAGAIN（LT模式下通常一次只有一个，但保险起见）
                while (true) {
                    int cfd = accept(lfd, nullptr, nullptr);
                    if (cfd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // 当前没有更多连接了，退出accept循环
                            break;
                        }
                        perror("accept");
                        break;
                    }
                    
                    // 设置客户端socket为非阻塞模式（SubReactor要求，用于ET模式）
                    int flags = fcntl(cfd, F_GETFL, 0);
                    if (fcntl(cfd, F_SETFL, flags | O_NONBLOCK) == -1) {
                        perror("fcntl F_SETFL");
                        close(cfd);
                        continue;
                    }
                    
                    // 【负载均衡】Round-Robin轮询选择SubReactor
                    // 使用原子计数器保证线程安全，避免加锁开销
                    size_t idx = roundRobinCounter++ % subReactors.size();
                    
                    // 将连接分发给选中的SubReactor（线程安全操作）
                    if (!subReactors[idx]->addConnection(cfd)) {
                        std::cerr << "[MainReactor] 分发连接到SubReactor " << idx << " 失败" << std::endl;
                        close(cfd);
                    } else {
                        // 可选：打印分发日志（高并发时建议注释掉以减少输出）
                        // std::cout << "[MainReactor] 连接 " << cfd << " 分发给SubReactor " << idx << std::endl;
                    }
                }
            }
        }
    }
    
    // 【4】清理（实际上只有收到信号时才会执行到这里）
    std::cout << "[MainReactor] 正在关闭服务器..." << std::endl;
    
    // 停止所有SubReactor
    for (auto& sub : subReactors) {
        sub->stop();
    }
    
    close(epfd);
    return 0;
}

/**
 * @brief 【已废弃】单Reactor版本的acceptClient函数
 * 
 * 保留原因：向后兼容，防止用户代码依赖此函数
 * 实际功能：已被epollRun内的accept逻辑替代
 * 
 * 注意：此函数不再被多Reactor架构调用，fd会被直接分发给SubReactor
 */
int acceptClient(int lfd, int epfd) {
    // 此函数在单Reactor版本中使用，多Reactor版本中逻辑已合并到epollRun
    // 保留实现以维持二进制兼容性
    int cfd = accept(lfd, nullptr, nullptr);
    if (cfd == -1) {
        perror("accept");
        return -1;
    }
    
    // 设置非阻塞模式
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
    
    // 加入epoll（ET+ONESHOT模式，单Reactor版本需要防止多线程竞争）
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

/**
 * @brief 接收HTTP请求（业务逻辑层，运行在线程池线程）
 * 
 * 【多Reactor架构中的调用位置】
 * 此函数由ThreadPool的工作线程调用，不是SubReactor的IO线程！
 * 
 * 调用链：
 * SubReactor::handleRead (IO线程) 
 *   -> 读取数据到buffer 
 *   -> pool_.enqueue提交任务 
 *   -> ThreadPool线程执行此函数
 * 
 * 职责：
 * - 解析HTTP请求（parseRequestLine）
 * - 根据请求发送文件或目录列表
 * - 发送完毕后关闭连接
 * - 从SubReactor的连接集合中移除记录（通过SubReactor内部管理）
 * 
 * 注意：此函数执行完毕后，连接生命周期结束
 */
void recvHttpRequest(std::shared_ptr<FdInfo> info) {
    // 记录处理线程ID（调试用，展示多线程处理效果）
    std::cout << "[ThreadPool] 处理连接 " << info->fd 
              << "，线程ID: " << std::this_thread::get_id() << std::endl;
    
    // 使用string动态扩容，避免固定大小数组的内存浪费或溢出风险
    std::string buffer;
    buffer.reserve(4096); // 预分配4KB，大多数HTTP请求头都能容纳
    
    char tmp[1024];
    ssize_t len = 0;
    ssize_t total = 0;
    
    // 读取HTTP请求数据（注意：ET模式下SubReactor已经确保数据全部到达）
    // 但由于TCP是流式协议，可能分多个包到达，这里需要循环读取
    while ((len = recv(info->fd, tmp, sizeof(tmp), 0)) > 0) {
        buffer.append(tmp, static_cast<size_t>(len));
        total += len;
        
        // 安全限制：防止恶意请求导致内存无限增长（最大8MB）
        if (buffer.size() > 8 * 1024 * 1024) {
            std::cerr << "[ThreadPool] 请求过大，拒绝服务 (fd=" << info->fd << ")" << std::endl;
            break;
        }
    }
    
    // 处理读取结果
    if (len == -1 && errno == EAGAIN) {
        // EAGAIN表示当前socket接收缓冲区已无数据（对于非阻塞socket）
        // 在SubReactor的ET模式下，这意味着所有数据都已读取完毕
        
        // 查找HTTP请求行结束符\r\n
        auto pos = buffer.find("\r\n");
        
        if (pos != std::string::npos) {
            // 提取请求行（第一行）
            std::string requestLine = buffer.substr(0, pos);
            // 解析请求行并处理（发送响应）
            parseRequestLine(requestLine, info->fd);
        } else {
            std::cerr << "[ThreadPool] 警告：请求格式不完整 (fd=" << info->fd << ")" << std::endl;
        }
    } else if (len == 0) {
        // 客户端主动关闭连接（发送了FIN）
        std::cout << "[ThreadPool] 客户端主动断开连接 (fd=" << info->fd << ")" << std::endl;
    } else {
        // 其他错误
        perror("recv error");
    }
    
    // 【资源清理】关闭连接
    // 注意：多Reactor架构下，epoll_ctl(EPOLL_CTL_DEL)由SubReactor在检测到断开时处理
    // 这里只需要close(fd)，因为连接已经处理完毕
    close(info->fd);
    
    // info是shared_ptr，函数返回后自动释放内存（RAII）
}

/**
 * @brief 解析HTTP请求行并处理
 * 
 * 解析格式：METHOD PATH HTTP/VERSION\r\n
 * 示例：GET /index.html HTTP/1.1
 * 
 * 安全特性：
 * - URL解码（处理%XX编码）
 * - 目录遍历防护（阻止../攻击）
 * - 只处理GET请求（其他方法返回错误）
 */
int parseRequestLine(const std::string& line, int cfd) {
    std::string method, path, version;
    std::istringstream iss(line);
    
    // 解析三个字段
    if (!(iss >> method >> path >> version)) {
        std::cerr << "[ThreadPool] 解析请求行失败: " << line << std::endl;
        return -1;
    }
    
    std::cout << "[ThreadPool] 请求: " << method << " " << path << std::endl;
    
    // 只处理GET请求（简化实现）
    if (strcasecmp(method.c_str(), "get") != 0) {
        std::cerr << "[ThreadPool] 不支持的HTTP方法: " << method << std::endl;
        return -1;
    }
    
    // URL解码（处理空格、中文等）
    std::string decodedPath = decodeMsg(path);
    
    // 【安全检查】防止目录遍历攻击（../../../etc/passwd）
    if (decodedPath.find("..") != std::string::npos) {
        std::cerr << "[ThreadPool] 阻止目录遍历攻击: " << decodedPath << std::endl;
        sendHeadMsg(cfd, 403, "Forbidden", getFileType(".html"), -1);
        const char* msg = "<html><body><h1>403 Forbidden</h1><p>Invalid path.</p></body></html>";
        send(cfd, msg, strlen(msg), 0);
        return -1;
    }
    
    // 处理根目录请求
    std::string filePath;
    if (decodedPath == "/") {
        filePath = "./"; // 当前工作目录（由main函数chdir设置）
    } else {
        filePath = decodedPath.substr(1); // 去掉开头的/
    }
    
    // 获取文件属性
    struct stat st;
    if (stat(filePath.c_str(), &st) == -1) {
        // 文件不存在，返回404
        std::cerr << "[ThreadPool] 文件不存在: " << filePath << std::endl;
        sendHeadMsg(cfd, 404, "Not Found", getFileType(".html"), -1);
        
        const char* err_msg = 
            "<html><head><title>404 Not Found</title></head>"
            "<body><h1 style=\"color:red;\">404 Not Found</h1>"
            "<p>The requested file does not exist on this server.</p>"
            "</body></html>";
        send(cfd, err_msg, strlen(err_msg), 0);
        return 0;
    }
    
    // 判断是目录还是普通文件
    if (S_ISDIR(st.st_mode)) {
        // 发送目录列表（HTML格式）
        sendHeadMsg(cfd, 200, "OK", getFileType(".html"), -1);
        sendDir(filePath, cfd);
    } else {
        // 发送文件（零拷贝优化）
        sendHeadMsg(cfd, 200, "OK", getFileType(filePath), static_cast<int>(st.st_size));
        sendFile(filePath, cfd);
    }
    
    return 0;
}

/**
 * @brief 根据文件扩展名获取MIME类型
 * 
 * 使用C++11 lambda初始化静态map，提高性能（只初始化一次）
 * 支持常见Web文件类型：html, css, js, 图片, 视频等
 */
std::string getFileType(const std::string& name) {
    // 查找最后一个点号位置
    auto dotPos = name.rfind('.');
    if (dotPos == std::string::npos) {
        return "text/plain; charset=utf-8"; // 默认类型
    }
    
    std::string ext = name.substr(dotPos);
    
    // C++11特性：lambda初始化静态const map
    static const auto mimeTypes = []() {
        std::map<std::string, std::string> m;
        m[".html"] = "text/html; charset=utf-8";
        m[".htm"] = "text/html; charset=utf-8";
        m[".css"] = "text/css";
        m[".js"] = "application/javascript";
        m[".json"] = "application/json";
        m[".jpg"] = "image/jpeg";
        m[".jpeg"] = "image/jpeg";
        m[".png"] = "image/png";
        m[".gif"] = "image/gif";
        m[".svg"] = "image/svg+xml";
        m[".ico"] = "image/x-icon";
        m[".pdf"] = "application/pdf";
        m[".txt"] = "text/plain; charset=utf-8";
        m[".mp4"] = "video/mp4";
        m[".webm"] = "video/webm";
        m[".mp3"] = "audio/mpeg";
        m[".wav"] = "audio/wav";
        m[".ogg"] = "audio/ogg";
        m[".woff"] = "font/woff";
        m[".woff2"] = "font/woff2";
        m[".ttf"] = "font/ttf";
        return m;
    }();
    
    auto it = mimeTypes.find(ext);
    if (it != mimeTypes.end()) {
        return it->second;
    }
    
    return "text/plain; charset=utf-8"; // 未知类型默认text/plain
}

/**
 * @brief 发送目录内容（HTML表格格式）
 * 
 * 使用scandir读取目录，生成带链接的HTML页面
 * 支持点击子目录进入，点击文件下载/查看
 */
int sendDir(const std::string& dirName, int cfd) {
    std::ostringstream html;
    html << "<!DOCTYPE html><html><head>";
    html << "<meta charset=\"UTF-8\">";
    html << "<title>Index of " << dirName << "</title>";
    html << "<style>"
         << "table { border-collapse: collapse; width: 80%; margin: 20px; }"
         << "th, td { text-align: left; padding: 8px; border-bottom: 1px solid #ddd; }"
         << "tr:hover { background-color: #f5f5f5; }"
         << "a { text-decoration: none; color: #0066cc; }"
         << "a:hover { text-decoration: underline; }"
         << "</style>";
    html << "</head><body>";
    html << "<h2>Index of " << dirName << "</h2>";
    html << "<table>";
    html << "<tr><th>Name</th><th>Size</th><th>Type</th></tr>";
    
    // 使用scandir读取目录（POSIX标准）
    struct dirent** namelist = nullptr;
    int num = scandir(dirName.c_str(), &namelist, nullptr, alphasort);
    
    if (num < 0) {
        perror("scandir");
        return -1;
    }
    
    // RAII确保namelist被释放（即使发生异常）
    struct DirGuard {
        struct dirent** list;
        int count;
        DirGuard(struct dirent** l, int c) : list(l), count(c) {}
        ~DirGuard() {
            for (int i = 0; i < count; ++i) {
                free(list[i]);
            }
            free(list);
        }
    } dirGuard(namelist, num);
    
    for (int i = 0; i < num; ++i) {
        std::string name = namelist[i]->d_name;
        
        // 跳过当前目录（.）和父目录（..）
        if (name == "." || name == "..") continue;
        
        // 构建完整路径获取文件信息
        std::string fullPath = dirName;
        if (fullPath.back() != '/') fullPath += "/";
        fullPath += name;
        
        struct stat st;
        if (stat(fullPath.c_str(), &st) == -1) continue; // 跳过无法访问的文件
        
        html << "<tr>";
        html << "<td>";
        
        // 根据类型生成不同链接
        if (S_ISDIR(st.st_mode)) {
            html << "📁 <a href=\"" << name << "/\">" << name << "/</a>";
        } else {
            html << "📄 <a href=\"" << name << "\">" << name << "</a>";
        }
        
        html << "</td>";
        
        // 文件大小（目录显示"-"）
        html << "<td>";
        if (S_ISDIR(st.st_mode)) {
            html << "-";
        } else {
            // 格式化文件大小（B, KB, MB）
            off_t size = st.st_size;
            if (size < 1024) {
                html << size << " B";
            } else if (size < 1024 * 1024) {
                html << (size / 1024) << " KB";
            } else {
                html << (size / (1024 * 1024)) << " MB";
            }
        }
        html << "</td>";
        
        // MIME类型
        html << "<td>" << (S_ISDIR(st.st_mode) ? "directory" : getFileType(name)) << "</td>";
        html << "</tr>";
    }
    
    html << "</table>";
    html << "<hr><p><em>Server: MultiReactorHTTP/1.0</em></p>";
    html << "</body></html>";
    
    std::string response = html.str();
    send(cfd, response.c_str(), response.size(), 0);
    
    return 0;
}

/**
 * @brief 发送文件内容（零拷贝优化）
 * 
 * 使用sendfile系统调用实现内核态零拷贝传输
 * 配合EAGAIN处理，实现高效的大文件传输
 */
int sendFile(const std::string& fileName, int cfd) {
    int fd = open(fileName.c_str(), O_RDONLY);
    if (fd == -1) {
        perror("open file failed");
        return -1;
    }
    
    // RAII确保文件描述符关闭
    struct FileGuard {
        int fd;
        FileGuard(int f) : fd(f) {}
        ~FileGuard() { close(fd); }
    } fileGuard(fd);
    
    // 获取文件大小
    off_t fileSize = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    
    off_t offset = 0;
    
    // 使用sendfile零拷贝发送（Linux特有优化）
    // 将文件内容直接从内核缓冲区发送到socket，避免用户态拷贝
    while (offset < fileSize) {
        ssize_t sent = sendfile(cfd, fd, &offset, static_cast<size_t>(fileSize - offset));
        
        if (sent == -1) {
            if (errno == EAGAIN) {
                // 发送缓冲区满，短暂休眠后继续（简单处理）
                // 生产环境可使用epoll监听写事件，但这里简化处理
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            } else {
                perror("sendfile error");
                return -1;
            }
        } else if (sent == 0) {
            // 发送0字节，可能连接已关闭
            break;
        }
    }
    
    return 0;
}

/**
 * @brief 发送HTTP响应头
 * 
 * 构造标准HTTP响应头，包括状态行和Content-Type/Content-Length
 */
int sendHeadMsg(int cfd, int status, 
                const std::string& descr, 
                const std::string& type, 
                int length) {
    std::ostringstream header;
    header << "HTTP/1.1 " << status << " " << descr << "\r\n";
    header << "Server: MultiReactorHTTP/1.0\r\n";
    header << "Content-Type: " << type << "\r\n";
    
    if (length >= 0) {
        header << "Content-Length: " << length << "\r\n";
    }
    
    header << "Connection: close\r\n"; // 短连接，发送完关闭
    header << "\r\n"; // 空行分隔头和体
    
    std::string headerStr = header.str();
    send(cfd, headerStr.c_str(), headerStr.size(), 0);
    
    return 0;
}

/**
 * @brief 十六进制字符转十进制数值
 * 
 * 支持0-9, a-f, A-F
 */
int hexToDec(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

/**
 * @brief URL解码（百分号编码）
 * 
 * 将%XX格式的编码转换为原始字符
 * 例如："hello%20world" -> "hello world"
 */
std::string decodeMsg(const std::string& from) {
    std::string to;
    to.reserve(from.size()); // 预分配，避免多次扩容
    
    for (size_t i = 0; i < from.size(); ++i) {
        if (from[i] == '%' && i + 2 < from.size() && 
            std::isxdigit(static_cast<unsigned char>(from[i+1])) && 
            std::isxdigit(static_cast<unsigned char>(from[i+2]))) {
            // 解码%XX格式
            char decoded = static_cast<char>(hexToDec(from[i+1]) * 16 + hexToDec(from[i+2]));
            to.push_back(decoded);
            i += 2; // 跳过后面两个字符
        } else if (from[i] == '+') {
            // 某些编码中+代表空格（虽然标准URL编码应该用%20）
            to.push_back(' ');
        } else {
            to.push_back(from[i]);
        }
    }
    
    return to;
}