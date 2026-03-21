#pragma once
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>

class Connection {
public:
    Connection(int fd, const sockaddr_in& addr);
    ~Connection();
    
    void HandleRequest();          // 处理HTTP请求
    void SendResponse(const std::string& response); // 发送响应
    void SendFile(const std::string& file_path);    // 发送文件
    int GetFd() const { return fd_; } // 为EpollRun()添加必要方法
private:
    int fd_;                      // 客户端socket文件描述符
    sockaddr_in addr_;            // 客户端地址
    std::string request_buffer_;  // 请求缓冲区
};