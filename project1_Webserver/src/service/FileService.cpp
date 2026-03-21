#include "FileService.h"
#include <jsoncpp/json/json.h>
#include <sys/stat.h>
#include <dirent.h>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <locale>
#include <codecvt>
#include <iostream>
#include <algorithm>

// 辅助函数：将宽字符字符串转换为UTF-8
std::string wstring_to_utf8(const std::wstring& str) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
    return myconv.to_bytes(str);
}

// 辅助函数：将UTF-8字符串转换为宽字符
std::wstring utf8_to_wstring(const std::string& str) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
    return myconv.from_bytes(str);
}

// 辅助函数：URL编码
std::string urlEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    
    for (char c : value) {
        // 保持字母数字字符不变
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
            continue;
        }
        
        // 其他字符都进行百分号编码
        escaped << '%' << std::setw(2) << int((unsigned char)c);
    }
    
    return escaped.str();
}

std::string FileService::getDirectoryJson(const std::string& path, 
                                           const std::string& basePath) {
    Json::Value root(Json::arrayValue);
    
    // 构建完整路径
    std::string fullPath = basePath;
    if (!path.empty() && path != "/") {
        if (fullPath.back() != '/') fullPath += '/';
        fullPath += path;
    }
    
    std::cout << "[FileService] 扫描目录: " << fullPath << std::endl;
    
    DIR* dir = opendir(fullPath.c_str());
    if (!dir) {
        std::cerr << "[FileService] 无法打开目录: " << fullPath << std::endl;
        return "[]";
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        // 跳过 . 和 .. 目录
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        std::string entryName = entry->d_name;
        std::string entryPath = fullPath;
        if (entryPath.back() != '/') entryPath += '/';
        entryPath += entryName;
        
        struct stat fileStat;
        if (stat(entryPath.c_str(), &fileStat) != 0) {
            continue;
        }
        
        Json::Value fileInfo;
        fileInfo["name"] = entryName;
        fileInfo["isDir"] = S_ISDIR(fileStat.st_mode);
        
        // 获取文件大小
        if (!S_ISDIR(fileStat.st_mode)) {
            fileInfo["size"] = static_cast<Json::Int64>(fileStat.st_size);
            
            // 人类可读的大小
            std::string sizeHuman;
            if (fileStat.st_size < 1024) {
                sizeHuman = std::to_string(fileStat.st_size) + " B";
            } else if (fileStat.st_size < 1024 * 1024) {
                sizeHuman = std::to_string(fileStat.st_size / 1024) + " KB";
            } else if (fileStat.st_size < 1024 * 1024 * 1024) {
                sizeHuman = std::to_string(fileStat.st_size / (1024 * 1024)) + " MB";
            } else {
                sizeHuman = std::to_string(fileStat.st_size / (1024 * 1024 * 1024)) + " GB";
            }
            fileInfo["sizeHuman"] = sizeHuman;
        } else {
            fileInfo["size"] = 0;
            fileInfo["sizeHuman"] = "-";
        }
        
        // 获取修改时间
        char timeBuf[64];
        struct tm* timeinfo = localtime(&fileStat.st_mtime);
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", timeinfo);
        fileInfo["modified"] = timeBuf;
        
        // 获取文件类型
        fileInfo["type"] = getMimeType(entryName);
        
        root.append(fileInfo);
    }
    
    closedir(dir);
    
    Json::StreamWriterBuilder writer;
    writer["indentation"] = ""; // 紧凑格式
    writer["emitUTF8"] = true;  // 确保输出UTF-8
    
    std::string jsonStr = Json::writeString(writer, root);
    std::cout << "[FileService] 返回JSON长度: " << jsonStr.length() << " 字节" << std::endl;
    
    return jsonStr;
}

std::string FileService::getMimeType(const std::string& filename) {
    // 转换为小写以便比较
    std::string name = filename;
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    
    // 根据扩展名返回MIME类型
    auto dotPos = name.rfind('.');
    if (dotPos != std::string::npos) {
        std::string ext = name.substr(dotPos);
        
        if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
        if (ext == ".css") return "text/css";
        if (ext == ".js") return "application/javascript";
        if (ext == ".json") return "application/json";
        if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
        if (ext == ".png") return "image/png";
        if (ext == ".gif") return "image/gif";
        if (ext == ".svg") return "image/svg+xml";
        if (ext == ".ico") return "image/x-icon";
        if (ext == ".pdf") return "application/pdf";
        if (ext == ".txt") return "text/plain; charset=utf-8";
        if (ext == ".mp4") return "video/mp4";
        if (ext == ".mp3") return "audio/mpeg";
        if (ext == ".wav") return "audio/wav";
        if (ext == ".ogg") return "audio/ogg";
        if (ext == ".zip") return "application/zip";
        if (ext == ".rar") return "application/x-rar-compressed";
        if (ext == ".7z") return "application/x-7z-compressed";
        if (ext == ".doc") return "application/msword";
        if (ext == ".docx") return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
        if (ext == ".xls") return "application/vnd.ms-excel";
        if (ext == ".xlsx") return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
        if (ext == ".ppt") return "application/vnd.ms-powerpoint";
        if (ext == ".pptx") return "application/vnd.openxmlformats-officedocument.presentationml.presentation";
    }
    
    return "application/octet-stream";
}