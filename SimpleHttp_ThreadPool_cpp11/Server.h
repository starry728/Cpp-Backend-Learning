#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <functional>
#include <memory>

// 前向声明，减少头文件依赖
class ThreadPool;

/**
 * @brief 封装文件描述符信息的结构体（替代原来的C结构体）
 * 
 * 改进点：
 * 1. 使用智能指针管理生命周期，避免内存泄漏
 * 2. 移除pthread_t成员（C++11线程通过std::this_thread::get_id()获取ID）
 * 3. 添加构造函数，方便创建对象
 */
struct FdInfo {
    int fd = -1;           // 客户端socket文件描述符
    int epfd = -1;         // epoll实例文件描述符
    
    FdInfo(int f, int e) : fd(f), epfd(e) {}
};

/**
 * @brief 初始化监听套接字
 * @param port 监听端口
 * @return 监听套接字文件描述符，失败返回-1
 */
int initListenFd(unsigned short port);

/**
 * @brief 启动epoll事件循环（核心主循环）
 * @param lfd 监听套接字
 * @param pool 线程池引用，用于处理客户端请求
 * @return 0成功，-1失败
 * 
 * 注意：C++版本将线程池作为参数传入，避免全局变量
 */
int epollRun(int lfd, ThreadPool& pool);

/**
 * @brief 接受客户端连接
 * @param lfd 监听套接字
 * @param epfd epoll实例
 * @return 0成功，-1失败
 */
int acceptClient(int lfd, int epfd);

/**
 * @brief 接收并处理HTTP请求（工作线程执行的函数）
 * @param info 文件描述符信息，使用shared_ptr自动管理内存
 * 
 * 改为接受shared_ptr，避免手动free
 */
void recvHttpRequest(std::shared_ptr<FdInfo> info);

/**
 * @brief 解析HTTP请求行
 * @param line 请求行字符串
 * @param cfd 客户端socket
 * @return 0成功，-1失败
 */
int parseRequestLine(const std::string& line, int cfd);

/**
 * @brief 发送文件内容给客户端
 * @param fileName 文件名
 * @param cfd 客户端socket
 * @return 0成功，-1失败
 */
int sendFile(const std::string& fileName, int cfd);

/**
 * @brief 发送HTTP响应头（状态行+响应头）
 * @param cfd 客户端socket
 * @param status HTTP状态码
 * @param descr 状态描述
 * @param type Content-Type
 * @param length 内容长度，-1表示未知（ chunked或关闭连接）
 * @return 0成功，-1失败
 */
int sendHeadMsg(int cfd, int status, 
                const std::string& descr, 
                const std::string& type, 
                int length);

/**
 * @brief 根据文件后缀获取Content-Type
 * @param name 文件名
 * @return Content-Type字符串
 */
std::string getFileType(const std::string& name);

/**
 * @brief 发送目录列表（HTML格式）
 * @param dirName 目录名
 * @param cfd 客户端socket
 * @return 0成功，-1失败
 */
int sendDir(const std::string& dirName, int cfd);

/**
 * @brief 十六进制字符转十进制整数
 * @param c 十六进制字符（0-9, a-f, A-F）
 * @return 对应的十进制值
 */
int hexToDec(char c);

/**
 * @brief URL解码函数
 * @param from 编码后的字符串（如 Linux%E5%86%85%E6%A0%B8.jpg）
 * @return 解码后的字符串
 * 
 * 改进：返回std::string，避免手动管理to缓冲区的内存
 */
std::string decodeMsg(const std::string& from);

#endif // SERVER_H