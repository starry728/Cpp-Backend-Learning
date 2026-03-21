#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <string>
#include <map>
#include <sstream>
#include <algorithm>

/**
 * @brief 完整HTTP请求解析类
 */
class HttpRequest {
public:
    // 枚举HTTP方法
    enum Method {
        GET,
        POST,
        PUT,
        DELETE,
        UNKNOWN
    };
    
    HttpRequest();
    
    /**
     * @brief 从原始HTTP数据解析请求
     */
    bool parse(const std::string& data);
    
    // Getters
    Method getMethod() const { return method_; }
    std::string getMethodString() const;  // 新增：获取方法字符串
    const std::string& getPath() const { return path_; }
    const std::string& getVersion() const { return version_; }
    const std::string& getBody() const { return body_; }
    
    /**
     * @brief 获取请求头
     */
    std::string getHeader(const std::string& key) const;
    
    /**
     * @brief 获取URL参数
     */
    std::string getParam(const std::string& key) const;
    
    /**
     * @brief 获取Cookie值
     */
    std::string getCookie(const std::string& name) const;  // 新增
    
    /**
     * @brief 获取Content-Length
     */
    size_t getContentLength() const;
    
    /**
     * @brief 检查是否为Keep-Alive连接
     */
    bool isKeepAlive() const;
    
    /**
     * @brief 获取原始URL（含参数）
     */
    const std::string& getRawUrl() const { return rawUrl_; }

private:
    // 解析辅助函数
    bool parseRequestLine(const std::string& line);
    bool parseHeaders(const std::string& headerSection);
    bool parseBody(const std::string& data, size_t headerEnd);
    void parseUrlParams(const std::string& url);
    void parseCookies(const std::string& cookieStr);
    
    // 工具函数
    std::string trim(const std::string& s) const;
    std::string toLower(const std::string& s) const;
    std::string urlDecode(const std::string& encoded) const;
    int hexToDec(char c) const;

    // 请求行
    Method method_;                     // 修改为枚举类型
    std::string path_;                  // 解码后的路径（不含参数）
    std::string rawUrl_;               // 原始URL（含参数）
    std::string version_;
    
    // 请求头（小写存储）
    std::map<std::string, std::string> headers_;
    
    // URL参数
    std::map<std::string, std::string> params_;
    
    // Cookies
    std::map<std::string, std::string> cookies_;
    
    // 请求体
    std::string body_;
    
    // 解析状态
    bool parsed_ = false;
};

#endif // HTTP_REQUEST_H