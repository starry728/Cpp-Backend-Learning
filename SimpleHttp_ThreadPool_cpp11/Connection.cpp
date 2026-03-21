#include "Connection.h"
#include <fstream>    // 修复ifstream的关键头文件！
#include <sstream>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <cstring>

Connection::Connection(int fd, const sockaddr_in& addr) 
    : fd_(fd), addr_(addr) {}

Connection::~Connection() {
    close(fd_);
}

void Connection::HandleRequest() {
    char buffer[4096] = {0};
    ssize_t bytes_read = recv(fd_, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_read <= 0) {
        // 客户端断开连接
        return;
    }
    
    request_buffer_.append(buffer, bytes_read);
    
    // 简单解析：检查是否收到完整请求头（双CRLF）
    if (request_buffer_.find("\r\n\r\n") != std::string::npos) {
        size_t pos = request_buffer_.find(' ');
        if (pos != std::string::npos) {
            std::string method = request_buffer_.substr(0, pos);
            size_t path_end = request_buffer_.find(' ', pos + 1);
            std::string path = request_buffer_.substr(pos + 1, path_end - pos - 1);
            
            if (method == "GET") {
                // 默认首页处理
                if (path == "/") path = "/index.html";
                SendFile("." + path); // 根目录映射到当前目录
            }
        }
        request_buffer_.clear();
    }
}

void Connection::SendResponse(const std::string& response) {
    send(fd_, response.c_str(), response.size(), 0);
}

void Connection::SendFile(const std::string& file_path) {
    // 检查文件是否存在
    struct stat file_stat;
    if (stat(file_path.c_str(), &file_stat) != 0) {
        SendResponse("HTTP/1.1 404 Not Found\r\n\r\n");
        return;
    }

    // 处理目录请求
    if (S_ISDIR(file_stat.st_mode)) {
        // 尝试加载目录下的index.html
        std::string index_path = file_path + "/index.html";
        if (stat(index_path.c_str(), &file_stat) == 0 && S_ISREG(file_stat.st_mode)) {
            SendFile(index_path);
            return;
        }
        // 生成目录列表（简化版）
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
            << "<h1>Directory: " << file_path << "</h1><ul>";
        
        DIR* dir = opendir(file_path.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
                oss << "<li><a href=\"" << entry->d_name << "\">" << entry->d_name << "</a></li>";
            }
            closedir(dir);
        }
        oss << "</ul>";
        SendResponse(oss.str());
        return;
    }

    // 发送普通文件
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        SendResponse("HTTP/1.1 404 Not Found\r\n\r\n");
        return;
    }

    // 读取文件内容
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::string content(file_size, '\0');
    file.read(&content[0], file_size);
    
    // 发送HTTP响应头
    std::ostringstream header;
    header << "HTTP/1.1 200 OK\r\n"
           << "Content-Length: " << file_size << "\r\n"
           << "Content-Type: " << (file_path.find(".css") != std::string::npos ? 
                   "text/css" : (file_path.find(".js") != std::string::npos ? 
                   "application/javascript" : "text/html")) << "\r\n\r\n";
    
    SendResponse(header.str());
    send(fd_, content.c_str(), content.size(), 0);
}