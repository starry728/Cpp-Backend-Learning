#include "Server.h"
#include "ThreadPool.h"
#include "SubReactor.h"
#include "HttpRequest.h"
#include "Router.h"
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
static std::atomic<size_t> roundRobinCounter{0};

int initListenFd(unsigned short port) {
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd == -1) {
        perror("socket");
        return -1;
    }
    
    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
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

int epollRun(int lfd, ThreadPool& pool, 
             std::vector<std::unique_ptr<SubReactor>>& subReactors,
             std::atomic<bool>& running) {
    int epfd = epoll_create(1);
    if (epfd == -1) {
        perror("epoll_create in MainReactor");
        return -1;
    }
    
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
    
    for (auto& sub : subReactors) {
        sub->start();
    }
    
    std::vector<struct epoll_event> events(1024);
    
    while (running.load()) {
        int num = epoll_wait(epfd, events.data(), static_cast<int>(events.size()), 100);
        
        if (num == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait in MainReactor");
            break;
        }
        
        for (int i = 0; i < num; ++i) {
            int fd = events[i].data.fd;
            
            if (fd == lfd) {
                while (true) {
                    int cfd = accept(lfd, nullptr, nullptr);
                    if (cfd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        }
                        perror("accept");
                        break;
                    }
                    
                    int flags = fcntl(cfd, F_GETFL, 0);
                    if (fcntl(cfd, F_SETFL, flags | O_NONBLOCK) == -1) {
                        perror("fcntl F_SETFL");
                        close(cfd);
                        continue;
                    }
                    
                    size_t idx = roundRobinCounter++ % subReactors.size();
                    
                    if (!subReactors[idx]->addConnection(cfd)) {
                        std::cerr << "[MainReactor] 分发连接到SubReactor " << idx << " 失败" << std::endl;
                        close(cfd);
                    }
                }
            }
        }
    }
    
    std::cout << "[MainReactor] 正在关闭服务器..." << std::endl;
    
    for (auto& sub : subReactors) {
        sub->stop();
    }
    
    close(epfd);
    return 0;
}

int acceptClient(int lfd, int epfd) {
    int cfd = accept(lfd, nullptr, nullptr);
    if (cfd == -1) {
        perror("accept");
        return -1;
    }
    
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
    std::cout << "[ThreadPool] 处理连接 " << info->fd 
              << "，线程ID: " << std::this_thread::get_id() << std::endl;
    
    std::string buffer;
    buffer.reserve(4096);
    
    char tmp[1024];
    ssize_t len = 0;
    size_t contentLength = 0;
    bool headerComplete = false;
    
    while ((len = recv(info->fd, tmp, sizeof(tmp), 0)) > 0) {
        buffer.append(tmp, static_cast<size_t>(len));
        
        if (!headerComplete) {
            auto headerEnd = buffer.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                headerComplete = true;
                
                auto clPos = buffer.find("Content-Length: ");
                if (clPos != std::string::npos) {
                    auto clEnd = buffer.find("\r\n", clPos);
                    std::string clStr = buffer.substr(clPos + 16, clEnd - clPos - 16);
                    contentLength = static_cast<size_t>(std::stoul(clStr));
                }
            }
        }
        
        if (headerComplete) {
            auto headerEnd = buffer.find("\r\n\r\n") + 4;
            if (buffer.length() >= headerEnd + contentLength) {
                break;
            }
        }
        
        if (buffer.size() > 8 * 1024 * 1024) break;
    }
    
    if (buffer.empty()) {
        close(info->fd);
        return;
    }
    
    HttpRequest request;
    if (!request.parse(buffer)) {
        Router::sendError(info->fd, 400, "Bad Request");
        close(info->fd);
        return;
    }
    
    const char* basePath = getenv("WEB_ROOT");
    if (!basePath) basePath = "./";
    
    Router& router = Router::getInstance();
    bool handled = router.handleRequest(request, info->fd, basePath);
    
    if (!handled) {
        Router::sendError(info->fd, 404, "Not Found");
    }
    
    close(info->fd);
}

int parseRequestLine(const std::string& line, int cfd) {
    std::string method, path, version;
    std::istringstream iss(line);
    
    if (!(iss >> method >> path >> version)) {
        std::cerr << "[ThreadPool] 解析请求行失败: " << line << std::endl;
        return -1;
    }
    
    std::cout << "[ThreadPool] 请求: " << method << " " << path << std::endl;
    
    if (strcasecmp(method.c_str(), "get") != 0) {
        std::cerr << "[ThreadPool] 不支持的HTTP方法: " << method << std::endl;
        return -1;
    }
    
    std::string decodedPath = decodeMsg(path);
    
    if (decodedPath.find("..") != std::string::npos) {
        std::cerr << "[ThreadPool] 阻止目录遍历攻击: " << decodedPath << std::endl;
        sendHeadMsg(cfd, 403, "Forbidden", getFileType(".html"), -1);
        const char* msg = "<html><body><h1>403 Forbidden</h1><p>Invalid path.</p></body></html>";
        send(cfd, msg, strlen(msg), 0);
        return -1;
    }
    
    std::string filePath;
    if (decodedPath == "/") {
        filePath = "./";
    } else {
        filePath = decodedPath.substr(1);
    }
    
    struct stat st;
    if (stat(filePath.c_str(), &st) == -1) {
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
    
    if (S_ISDIR(st.st_mode)) {
        sendHeadMsg(cfd, 200, "OK", getFileType(filePath), -1);
        sendDir(filePath, cfd);
    } else {
        sendHeadMsg(cfd, 200, "OK", getFileType(filePath), static_cast<int>(st.st_size));
        sendFile(filePath, cfd);
    }
    
    return 0;
}

std::string getFileType(const std::string& name) {
    auto dotPos = name.rfind('.');
    if (dotPos == std::string::npos) {
        return "text/plain; charset=utf-8";
    }
    
    std::string ext = name.substr(dotPos);
    
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
    
    return "text/plain; charset=utf-8";
}

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
    
    struct dirent** namelist = nullptr;
    int num = scandir(dirName.c_str(), &namelist, nullptr, alphasort);
    
    if (num < 0) {
        perror("scandir");
        return -1;
    }
    
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
        
        if (name == "." || name == "..") continue;
        
        std::string fullPath = dirName;
        if (fullPath.back() != '/') fullPath += "/";
        fullPath += name;
        
        struct stat st;
        if (stat(fullPath.c_str(), &st) == -1) continue;
        
        html << "<tr>";
        html << "<td>";
        
        if (S_ISDIR(st.st_mode)) {
            html << "📁 <a href=\"" << name << "/\">" << name << "/</a>";
        } else {
            html << "📄 <a href=\"" << name << "\">" << name << "</a>";
        }
        
        html << "</td>";
        
        html << "<td>";
        if (S_ISDIR(st.st_mode)) {
            html << "-";
        } else {
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

int sendFile(const std::string& fileName, int cfd) {
    int fd = open(fileName.c_str(), O_RDONLY);
    if (fd == -1) {
        perror("open file failed");
        return -1;
    }
    
    struct FileGuard {
        int fd;
        FileGuard(int f) : fd(f) {}
        ~FileGuard() { close(fd); }
    } fileGuard(fd);
    
    off_t fileSize = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    
    off_t offset = 0;
    
    while (offset < fileSize) {
        ssize_t sent = sendfile(cfd, fd, &offset, static_cast<size_t>(fileSize - offset));
        
        if (sent == -1) {
            if (errno == EAGAIN) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            } else {
                perror("sendfile error");
                return -1;
            }
        } else if (sent == 0) {
            break;
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
    header << "Server: MultiReactorHTTP/1.0\r\n";
    header << "Content-Type: " << type << "\r\n";
    
    if (length >= 0) {
        header << "Content-Length: " << length << "\r\n";
    }
    
    header << "Connection: close\r\n";
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
    to.reserve(from.size());
    
    for (size_t i = 0; i < from.size(); ++i) {
        if (from[i] == '%' && i + 2 < from.size() && 
            std::isxdigit(static_cast<unsigned char>(from[i+1])) && 
            std::isxdigit(static_cast<unsigned char>(from[i+2]))) {
            char decoded = static_cast<char>(hexToDec(from[i+1]) * 16 + hexToDec(from[i+2]));
            to.push_back(decoded);
            i += 2;
        } else if (from[i] == '+') {
            to.push_back(' ');
        } else {
            to.push_back(from[i]);
        }
    }
    
    return to;
}