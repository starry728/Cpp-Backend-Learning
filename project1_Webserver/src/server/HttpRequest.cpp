#include "HttpRequest.h"
#include <cctype>
#include <iostream>
#include <cstring>

HttpRequest::HttpRequest() : method_(UNKNOWN) {}

// 从字符串转换为枚举方法
HttpRequest::Method getMethodFromString(const std::string& methodStr) {
    if (methodStr == "GET" || methodStr == "get") return HttpRequest::GET;
    if (methodStr == "POST" || methodStr == "post") return HttpRequest::POST;
    if (methodStr == "PUT" || methodStr == "put") return HttpRequest::PUT;
    if (methodStr == "DELETE" || methodStr == "delete") return HttpRequest::DELETE;
    return HttpRequest::UNKNOWN;
}

// 从枚举转换为字符串
std::string getStringFromMethod(HttpRequest::Method method) {
    switch (method) {
        case HttpRequest::GET: return "GET";
        case HttpRequest::POST: return "POST";
        case HttpRequest::PUT: return "PUT";
        case HttpRequest::DELETE: return "DELETE";
        default: return "UNKNOWN";
    }
}

bool HttpRequest::parse(const std::string& data) {
    // 查找头部结束标志
    auto headerEnd = data.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        headerEnd = data.find("\n\n");
        if (headerEnd == std::string::npos) {
            return false;
        }
        headerEnd += 2;
    } else {
        headerEnd += 4;
    }
    
    std::string headerSection = data.substr(0, headerEnd);
    size_t bodyStart = headerEnd;
    
    // 解析请求行
    auto firstLineEnd = headerSection.find("\r\n");
    if (firstLineEnd == std::string::npos) {
        firstLineEnd = headerSection.find("\n");
        if (firstLineEnd == std::string::npos) return false;
    }
    
    std::string requestLine = headerSection.substr(0, firstLineEnd);
    if (!parseRequestLine(requestLine)) {
        return false;
    }
    
    // 解析请求头
    std::string headersOnly = headerSection.substr(firstLineEnd + 2);
    if (!parseHeaders(headersOnly)) {
        return false;
    }
    
    // 解析请求体
    if (getContentLength() > 0 && bodyStart < data.size()) {
        body_ = data.substr(bodyStart, getContentLength());
    }
    
    parsed_ = true;
    return true;
}

bool HttpRequest::parseRequestLine(const std::string& line) {
    std::istringstream iss(line);
    std::string methodStr;
    if (!(iss >> methodStr >> rawUrl_ >> version_)) {
        return false;
    }
    
    // 设置HTTP方法
    method_ = getMethodFromString(methodStr);
    
    // 解码URL并分离路径和参数
    auto qPos = rawUrl_.find('?');
    if (qPos != std::string::npos) {
        path_ = urlDecode(rawUrl_.substr(0, qPos));
        parseUrlParams(rawUrl_.substr(qPos + 1));
    } else {
        path_ = urlDecode(rawUrl_);
    }
    
    return true;
}

bool HttpRequest::parseHeaders(const std::string& headerSection) {
    std::istringstream stream(headerSection);
    std::string line;
    
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) continue;
        
        auto colonPos = line.find(':');
        if (colonPos == std::string::npos) continue;
        
        std::string key = toLower(trim(line.substr(0, colonPos)));
        std::string value = trim(line.substr(colonPos + 1));
        
        headers_[key] = value;
        
        // 特殊处理Cookie头
        if (key == "cookie") {
            parseCookies(value);
        }
    }
    
    return true;
}

void HttpRequest::parseUrlParams(const std::string& paramStr) {
    std::istringstream stream(paramStr);
    std::string pair;
    
    while (std::getline(stream, pair, '&')) {
        auto eqPos = pair.find('=');
        if (eqPos != std::string::npos) {
            std::string key = urlDecode(pair.substr(0, eqPos));
            std::string value = urlDecode(pair.substr(eqPos + 1));
            params_[key] = value;
        } else {
            params_[urlDecode(pair)] = "";
        }
    }
}

void HttpRequest::parseCookies(const std::string& cookieStr) {
    std::istringstream stream(cookieStr);
    std::string pair;
    
    while (std::getline(stream, pair, ';')) {
        pair = trim(pair);
        auto eqPos = pair.find('=');
        if (eqPos != std::string::npos) {
            std::string name = trim(pair.substr(0, eqPos));
            std::string value = trim(pair.substr(eqPos + 1));
            cookies_[name] = value;
        }
    }
}

std::string HttpRequest::getHeader(const std::string& key) const {
    auto it = headers_.find(toLower(key));
    if (it != headers_.end()) {
        return it->second;
    }
    return "";
}

std::string HttpRequest::getParam(const std::string& key) const {
    auto it = params_.find(key);
    if (it != params_.end()) {
        return it->second;
    }
    return "";
}

std::string HttpRequest::getCookie(const std::string& name) const {
    auto it = cookies_.find(name);
    if (it != cookies_.end()) {
        return it->second;
    }
    return "";
}

std::string HttpRequest::getMethodString() const {
    return getStringFromMethod(method_);
}

size_t HttpRequest::getContentLength() const {
    auto cl = getHeader("Content-Length");
    if (cl.empty()) return 0;
    try {
        return static_cast<size_t>(std::stoul(cl));
    } catch (...) {
        return 0;
    }
}

bool HttpRequest::isKeepAlive() const {
    auto conn = getHeader("Connection");
    if (version_ == "HTTP/1.1") {
        return conn != "close";
    } else {
        return conn == "keep-alive";
    }
}

// 工具函数实现
std::string HttpRequest::trim(const std::string& s) const {
    auto start = s.begin();
    while (start != s.end() && std::isspace(*start)) ++start;
    
    auto end = s.end();
    while (end != start && std::isspace(*(end - 1))) --end;
    
    return std::string(start, end);
}

std::string HttpRequest::toLower(const std::string& s) const {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

std::string HttpRequest::urlDecode(const std::string& encoded) const {
    std::string result;
    result.reserve(encoded.size());
    
    for (size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.size()) {
            char decoded = static_cast<char>(hexToDec(encoded[i+1]) * 16 + hexToDec(encoded[i+2]));
            result.push_back(decoded);
            i += 2;
        } else if (encoded[i] == '+') {
            result.push_back(' ');
        } else {
            result.push_back(encoded[i]);
        }
    }
    return result;
}

int HttpRequest::hexToDec(char c) const {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}