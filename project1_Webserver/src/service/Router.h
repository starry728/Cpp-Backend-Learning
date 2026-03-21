#ifndef ROUTER_H
#define ROUTER_H

#include <string>
#include <functional>
#include <map>
#include "HttpRequest.h"

struct HttpResponse {
    int statusCode = 200;
    std::string statusText = "OK";
    std::map<std::string, std::string> headers;
    std::string body;
    
    void setContentType(const std::string& type) {
        headers["Content-Type"] = type;
    }
    
    void setCookie(const std::string& name, const std::string& value,
                   int maxAge = 3600, const std::string& path = "/") {
        std::stringstream ss;
        ss << name << "=" << value 
           << "; Path=" << path 
           << "; Max-Age=" << maxAge
           << "; HttpOnly";
        headers["Set-Cookie"] = ss.str();
    }
    
    std::string toString() const;
};

using RouteHandler = std::function<void(const HttpRequest&, HttpResponse&)>;

class Router {
public:
    static Router& getInstance();
    
    Router(const Router&) = delete;
    Router& operator=(const Router&) = delete;

    void registerRoute(const std::string& method,
                      const std::string& path,
                      RouteHandler handler);

    bool handleRequest(const HttpRequest& request, int cfd,
                       const std::string& basePath);

    static void sendResponse(int cfd, const HttpResponse& response);
    static void sendError(int cfd, int code, const std::string& message);

private:
    Router();
    void initRoutes();
    
    // 【新增】这些辅助函数需要声明在头文件中
    bool serveStaticFile(const std::string& path, int cfd,
                         const std::string& basePath);
    int sendFileInternal(const std::string& fileName, int cfd);
    
    static void parseFormBody(const std::string& body, 
                               std::string& username, 
                               std::string& password);
    static std::string urlDecode(const std::string& encoded);
    static int hexToDec(char c);
    
    std::map<std::string, std::map<std::string, RouteHandler>> routes_;
};

#endif // ROUTER_H