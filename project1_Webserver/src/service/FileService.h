#ifndef FILE_SERVICE_H
#define FILE_SERVICE_H

#include <string>
#include <vector>

/**
 * @brief 文件信息结构体
 */
struct FileInfo {
    std::string name;
    std::string path;
    bool isDirectory;
    size_t size;
    std::string modifiedTime;
    std::string mimeType;
};

/**
 * @brief 文件服务类
 * 
 * 提供安全的文件浏览功能：
 * - 目录列表（JSON格式，供前端AJAX调用）
 * - 文件下载（权限检查）
 * - 路径安全检查（防止目录遍历攻击）
 */
class FileService {
public:
    /**
     * @brief 获取目录内容（JSON格式）
     * @param dirPath 相对路径（如 "/" 或 "/docs"）
     * @param basePath 服务器根目录（绝对路径）
     * @return JSON字符串
     */
    static std::string getDirectoryJson(const std::string& dirPath,
                                         const std::string& basePath);

    /**
     * @brief 检查路径是否合法（防止目录遍历）
     * @param path 请求路径
     * @param basePath 允许访问的根目录
     * @return 合法返回规范化路径，非法返回空字符串
     */
    static std::string sanitizePath(const std::string& path,
                                   const std::string& basePath);

    /**
     * @brief 获取文件MIME类型
     */
    static std::string getMimeType(const std::string& filename);

private:
    static std::string escapeJson(const std::string& str);
    static std::string formatFileSize(size_t size);
};

#endif // FILE_SERVICE_H