#include "Router.h"
#include "SessionManager.h"
#include "AuthService.h"
#include "FileService.h"
#include "HttpRequest.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/sendfile.h>
#include <iostream>
#include <sstream>
#include <cstring>
#include <sys/socket.h>
#include <thread>
#include <chrono>

// 序列化HTTP响应
std::string HttpResponse::toString() const {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n";
    oss << "Server: MultiReactorWeb/1.0\r\n";
    
    for (const auto& pair : headers) {
        oss << pair.first << ": " << pair.second << "\r\n";
    }
    
    if (body.length() > 0 && headers.find("Content-Length") == headers.end()) {
        oss << "Content-Length: " << body.length() << "\r\n";
    }
    
    oss << "Connection: keep-alive\r\n";
    oss << "\r\n";
    oss << body;
    
    return oss.str();
}

Router& Router::getInstance() {
    static Router instance;
    return instance;
}

Router::Router() {
    initRoutes();
}

void Router::initRoutes() {
    // 1. 登录API
    // 在 /api/login 路由处理器中修改
    routes_["POST"]["/api/login"] = [](const HttpRequest& req, HttpResponse& resp) {
        std::string username, password;
        parseFormBody(req.getBody(), username, password);
        
        if (username.empty() || password.empty()) {
            resp.statusCode = 400;
            resp.setContentType("application/json");
            resp.body = R"({"success":false,"message":"缺少用户名或密码"})";
            return;
        }
        
        int userId = AuthService::loginUser(username, password);
        if (userId > 0) {
            std::string sessionId = SessionManager::getInstance()
                .createSession(username, userId, 30);
            
            // 【关键修复】简化Cookie设置
            std::string cookieValue = "session_id=" + sessionId + 
                                    "; Path=/; Max-Age=1800; SameSite=Lax; HttpOnly";
            resp.headers["Set-Cookie"] = cookieValue;
            
            // 【修复】同时设置一个JavaScript可访问的username Cookie
            std::string userCookie = "username=" + username + 
                                    "; Path=/; Max-Age=1800";
            resp.headers["Set-Cookie-2"] = userCookie;
            
            resp.setContentType("application/json");
            resp.body = R"({"success":true,"message":"登录成功"})";
            
            std::cout << "[Router] /api/login 设置Cookie: " << cookieValue << std::endl;
        } else {
            resp.statusCode = 401;
            resp.setContentType("application/json");
            resp.body = R"({"success":false,"message":"用户名或密码错误"})";
        }
    };
    
    // 2. 注册API
    routes_["POST"]["/api/register"] = [](const HttpRequest& req, HttpResponse& resp) {
        std::string username, password;
        parseFormBody(req.getBody(), username, password);
        
        int userId = AuthService::registerUser(username, password);
        if (userId > 0) {
            resp.setContentType("application/json");
            resp.body = R"({"success":true,"message":"注册成功"})";
        } else {
            resp.statusCode = 400;
            resp.setContentType("application/json");
            resp.body = R"({"success":false,"message":"注册失败，用户名可能已存在"})";
        }
    };
    
    // 3. 文件列表API（需要登录）
    // 在 /api/files 路由处理器中修改
    routes_["GET"]["/api/files"] = [](const HttpRequest& req, HttpResponse& resp) {
        // 验证Session
        std::string sessionId = req.getCookie("session_id");
        std::cout << "[Router] /api/files 收到请求，sessionId: " << (sessionId.empty() ? "空" : sessionId) << std::endl;
        
        auto session = SessionManager::getInstance().getSession(sessionId);
        
        if (!session.isValid) {
            std::cout << "[Router] /api/files Session无效，返回403" << std::endl;
            resp.statusCode = 403;
            resp.setContentType("application/json");
            resp.body = R"({"success":false,"message":"未登录或会话已过期"})";
            return;
        }
        
        std::cout << "[Router] /api/files Session验证通过，用户: " << session.username << std::endl;
        
        std::string path = req.getParam("path");
        if (path.empty()) path = "/";
        std::cout << "[Router] /api/files 请求路径: " << path << std::endl;
        
        // 【关键修改】从环境变量获取图片目录
        const char* picPath = getenv("PIC_ROOT");
        if (!picPath) {
            // 如果环境变量不存在，使用命令行参数指定的路径
            // 这里需要从 main.cpp 传递过来的信息
            picPath = "/home/cxy/my_Learning_Projects/myPictures";
        }
        std::cout << "[Router] /api/files 图片目录: " << picPath << std::endl;
        
        std::string json = FileService::getDirectoryJson(path, picPath);
        resp.setContentType("application/json");
        resp.body = json;
        std::cout << "[Router] /api/files 返回数据长度: " << json.length() << " 字节" << std::endl;
    };
    
    // 4. 登出API
    routes_["POST"]["/api/logout"] = [](const HttpRequest& req, HttpResponse& resp) {
        std::string sessionId = req.getCookie("session_id");
        if (!sessionId.empty()) {
            SessionManager::getInstance().destroySession(sessionId);
        }
        resp.setContentType("application/json");
        resp.body = R"({"success":true,"message":"已登出"})";
    };
    
    std::cout << "[Router] 路由注册完成，共 " << routes_.size() << " 个方法" << std::endl;
}

// 辅助函数：解析表单数据
void Router::parseFormBody(const std::string& body, 
                            std::string& username, 
                            std::string& password) {
    size_t uPos = body.find("username=");
    size_t pPos = body.find("password=");
    
    if (uPos != std::string::npos) {
        size_t start = uPos + 9;
        size_t end = body.find('&', start);
        username = body.substr(start, end == std::string::npos ? 
                              std::string::npos : end - start);
        username = urlDecode(username);
    }
    
    if (pPos != std::string::npos) {
        size_t start = pPos + 9;
        size_t end = body.find('&', start);
        password = body.substr(start, end == std::string::npos ? 
                              std::string::npos : end - start);
        password = urlDecode(password);
    }
}

std::string Router::urlDecode(const std::string& encoded) {
    std::string result;
    for (size_t i = 0; i < encoded.length(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.length()) {
            int hex1 = hexToDec(encoded[i+1]);
            int hex2 = hexToDec(encoded[i+2]);
            result += static_cast<char>(hex1 * 16 + hex2);
            i += 2;
        } else if (encoded[i] == '+') {
            result += ' ';
        } else {
            result += encoded[i];
        }
    }
    return result;
}

int Router::hexToDec(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

bool Router::handleRequest(const HttpRequest& request, int cfd,
                            const std::string& basePath) {
    // 【修复】使用 getMethodString() 而不是 getMethod()
    const std::string& method = request.getMethodString();
    const std::string& path = request.getPath();
    
    std::cout << "[Router] handleRequest: " << method << " " << path << std::endl;
    std::cout << "[Router] Cookie头: " << request.getHeader("Cookie") << std::endl;
    
    // 查找API路由
    auto methodIt = routes_.find(method);
    if (methodIt != routes_.end()) {
        auto pathIt = methodIt->second.find(path);
        if (pathIt != methodIt->second.end()) {
            HttpResponse response;
            pathIt->second(request, response);
            sendResponse(cfd, response);
            return true;
        }
    }
    
    // 不是API请求，尝试静态文件
    if (path.find("/api/") == 0) {
        return false;
    }
    
    return serveStaticFile(path, cfd, basePath);
}

bool Router::serveStaticFile(const std::string& path, int cfd,
                                const std::string& basePath) {
    std::string filePath;
    
    std::cout << "[Router] serveStaticFile 请求路径: " << path << std::endl;
    std::cout << "[Router] basePath: " << basePath << std::endl;
    
    // 如果是根路径，查找 index.html
    if (path == "/" || path.empty()) {
        filePath = "index.html";
        struct stat st;
        if (stat(filePath.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            std::cout << "[Router] 找到默认页面: " << filePath << std::endl;
            return sendFileInternal(filePath, cfd) == 0;
        }
        
        std::cout << "[Router] 默认页面不存在: " << filePath << std::endl;
        return false;
    }
    
    // 处理其他静态文件
    std::string requestPath = path;
    
    // 【关键修复】处理URL编码的中文字符
    std::string decodedPath = urlDecode(requestPath);
    std::cout << "[Router] 解码后路径: " << decodedPath << std::endl;
    
    // 移除开头的斜杠
    if (!decodedPath.empty() && decodedPath[0] == '/') {
        decodedPath = decodedPath.substr(1);
    }
    
    std::cout << "[Router] 尝试访问文件: " << decodedPath << std::endl;
    
    // 首先尝试在前端目录查找
    std::string frontendPath = decodedPath;
    struct stat st;
    
    if (stat(frontendPath.c_str(), &st) == 0) {
        std::cout << "[Router] 找到前端文件: " << frontendPath << std::endl;
        return sendFileInternal(frontendPath, cfd) == 0;
    }
    
    // 在图片目录查找
    const char* picPath = getenv("PIC_ROOT");
    if (picPath) {
        std::string picFilePath = std::string(picPath) + "/" + decodedPath;
        std::cout << "[Router] 尝试图片目录: " << picFilePath << std::endl;
        
        if (stat(picFilePath.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            std::cout << "[Router] 找到图片文件: " << picFilePath << std::endl;
            return sendFileInternal(picFilePath, cfd) == 0;
        }
        
        // 如果找不到，尝试使用原始路径（未解码的）查找
        std::cout << "[Router] 解码后路径查找失败，尝试原始路径..." << std::endl;
        
        std::string originalPath = path;
        if (!originalPath.empty() && originalPath[0] == '/') {
            originalPath = originalPath.substr(1);
        }
        
        picFilePath = std::string(picPath) + "/" + originalPath;
        std::cout << "[Router] 尝试原始路径: " << picFilePath << std::endl;
        
        if (stat(picFilePath.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            std::cout << "[Router] 找到原始路径文件: " << picFilePath << std::endl;
            return sendFileInternal(picFilePath, cfd) == 0;
        }
    } else {
        std::cout << "[Router] PIC_ROOT 环境变量未设置" << std::endl;
    }
    
    std::cout << "[Router] 文件未找到: " << decodedPath << std::endl;
    return false;
}

int Router::sendFileInternal(const std::string& fileName, int cfd) {
    std::cout << "[Router] sendFileInternal: " << fileName << std::endl;
    
    int fd = open(fileName.c_str(), O_RDONLY);
    if (fd == -1) {
        std::cerr << "[Router] 无法打开文件: " << fileName << std::endl;
        return -1;
    }
    
    struct stat st;
    if (fstat(fd, &st) == -1) {
        close(fd);
        return -1;
    }
    
    // 获取MIME类型
    std::string mimeType = FileService::getMimeType(fileName);
    
    // 发送响应头
    std::string header = "HTTP/1.1 200 OK\r\n";
    header += "Server: MultiReactorWeb/1.0\r\n";
    header += "Content-Type: " + mimeType + "\r\n";
    
    // 对于文本文件，确保指定UTF-8编码
    if (mimeType.find("text/") == 0 || mimeType.find("application/json") == 0) {
        if (mimeType.find("charset=") == std::string::npos) {
            header = "HTTP/1.1 200 OK\r\n";
            header += "Server: MultiReactorWeb/1.0\r\n";
            header += "Content-Type: " + mimeType + "; charset=utf-8\r\n";
        }
    }
    
    header += "Content-Length: " + std::to_string(st.st_size) + "\r\n";
    header += "Connection: keep-alive\r\n";
    header += "Cache-Control: no-cache, no-store, must-revalidate\r\n";
    header += "Pragma: no-cache\r\n";
    header += "Expires: 0\r\n";
    header += "\r\n";
    
    send(cfd, header.c_str(), header.length(), 0);
    
    off_t offset = 0;
    while (offset < st.st_size) {
        ssize_t sent = sendfile(cfd, fd, &offset, st.st_size - offset);
        if (sent == -1) {
            if (errno == EAGAIN) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            close(fd);
            return -1;
        }
    }
    
    close(fd);
    return 0;
}

void Router::sendResponse(int cfd, const HttpResponse& response) {
    std::string data = response.toString();
    
    // 调试输出响应头
    std::cout << "[Router] 发送响应，状态码: " << response.statusCode << std::endl;
    for (const auto& header : response.headers) {
        std::cout << "[Router] 响应头: " << header.first << ": " << header.second << std::endl;
    }
    
    send(cfd, data.c_str(), data.length(), 0);
}

void Router::sendError(int cfd, int code, const std::string& message) {
    HttpResponse resp;
    resp.statusCode = code;
    resp.statusText = message;
    resp.setContentType("text/html");
    resp.body = "<html><body><h1>" + std::to_string(code) + " " + message + "</h1></body></html>";
    sendResponse(cfd, resp);
}