#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <functional>
#include <memory>
#include <vector>

// 前向声明
class ThreadPool;
class SubReactor;

/**
 * @brief 连接信息结构体（多Reactor版本）
 * 
 * 变更说明：
 * - 移除 epfd 字段：多Reactor架构下，每个SubReactor有自己的epfd，
 *   连接关闭时由SubReactor自行处理，无需在FdInfo中传递epfd
 * - 保留 fd：用于数据读写
 * - 使用 shared_ptr 管理生命周期，确保线程安全
 */
struct FdInfo {
    int fd = -1;
    explicit FdInfo(int f) : fd(f) {}
};

/**
 * @brief 初始化监听套接字
 * @param port 监听端口
 * @return 成功返回监听fd，失败返回-1
 * 
 * 设置SO_REUSEADDR选项，使用非阻塞模式
 */
int initListenFd(unsigned short port);

/**
 * @brief 启动多Reactor服务器（MainReactor入口）
 * @param lfd 监听套接字
 * @param pool 全局业务逻辑线程池（用于处理HTTP解析等耗时操作）
 * @param subReactors SubReactor数组，用于IO事件分发
 * @return 0成功，-1失败
 * 
 * 【多Reactor架构核心】：
 * - MainReactor（本函数所在线程）：只负责accept新连接，通过Round-Robin分发给SubReactor
 * - SubReactor（多个独立线程）：各自管理一个epoll实例，处理连接的读写事件
 * - ThreadPool（全局）：处理HTTP解析、文件读取等业务逻辑，避免阻塞IO线程
 */
int epollRun(int lfd, ThreadPool& pool, std::vector<std::unique_ptr<SubReactor>>& subReactors);

/**
 * @brief 【已废弃】单Reactor版本的accept处理
 * 
 * 保留原因：向后兼容，但实际不再使用
 * 多Reactor版本中，accept逻辑已内联到epollRun的MainReactor循环中
 */
int acceptClient(int lfd, int epfd);

/**
 * @brief 接收并处理HTTP请求（业务逻辑层）
 * @param info 连接信息（包含fd）
 * 
 * 【重要】调用链说明（多Reactor架构）：
 * 1. MainReactor: accept连接 → 分发给SubReactor[i]
 * 2. SubReactor: epoll_wait触发读事件 → 直接recv数据（IO线程）
 * 3. SubReactor: 数据读取完毕 → 封装为task提交给ThreadPool
 * 4. ThreadPool线程: 调用本函数进行HTTP解析和业务处理
 * 5. ThreadPool线程: 发送响应 → 关闭连接 → 清理资源
 * 
 * 注意：本函数在线程池线程中执行，不是IO线程！
 */
void recvHttpRequest(std::shared_ptr<FdInfo> info);

/**
 * @brief 解析HTTP请求行
 * @param line 请求行字符串（如 "GET /index.html HTTP/1.1"）
 * @param cfd 客户端socket（用于发送响应）
 * @return 0成功，-1失败
 * 
 * 解析method、path、version，进行URL解码和安全检查（防目录遍历）
 */
int parseRequestLine(const std::string& line, int cfd);

/**
 * @brief 发送文件内容（零拷贝优化）
 * @param fileName 文件路径
 * @param cfd 客户端socket
 * @return 0成功，-1失败
 * 
 * 使用sendfile系统调用实现零拷贝，配合EAGAIN处理实现高效传输
 */
int sendFile(const std::string& fileName, int cfd);

/**
 * @brief 发送HTTP响应头
 * @param cfd 客户端socket
 * @param status HTTP状态码（200, 404等）
 * @param descr 状态描述（"OK", "Not Found"等）
 * @param type Content-Type（如 "text/html"）
 * @param length Content-Length（-1表示不设置，用于chunked或关闭连接）
 * @return 0成功，-1失败
 */
int sendHeadMsg(int cfd, int status, 
                const std::string& descr, 
                const std::string& type, 
                int length);

/**
 * @brief 根据文件名获取MIME类型
 * @param name 文件名（如 "index.html"）
 * @return MIME类型字符串（如 "text/html; charset=utf-8"）
 * 
 * 使用静态map缓存常见类型，提高性能
 */
std::string getFileType(const std::string& name);

/**
 * @brief 发送目录列表（HTML格式）
 * @param dirName 目录路径
 * @param cfd 客户端socket
 * @return 0成功，-1失败
 * 
 * 生成带表格的HTML页面，列出目录内文件和子目录
 */
int sendDir(const std::string& dirName, int cfd);

/**
 * @brief 十六进制字符转十进制
 * @param c 十六进制字符（0-9, a-f, A-F）
 * @return 对应的十进制数值（0-15）
 */
int hexToDec(char c);

/**
 * @brief URL解码（%XX格式）
 * @param from 编码后的字符串（如 "hello%20world"）
 * @return 解码后的字符串（如 "hello world"）
 * 
 * 处理%XX格式的URL编码，支持UTF-8字符
 */
std::string decodeMsg(const std::string& from);

#endif // SERVER_H