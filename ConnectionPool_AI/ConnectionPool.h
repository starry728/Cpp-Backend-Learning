#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>       // 【新增】用于 std::thread
#include <atomic>       // 【新增】用于 std::atomic<bool>
#include <memory>       // 【新增】用于 std::shared_ptr
#include <string>
#include "MysqlConn.h"

using namespace std;

class ConnectionPool
{
public:
    // 获取单例实例
    static ConnectionPool* getConnectPool();

    // 禁用拷贝构造和赋值运算符
    ConnectionPool(const ConnectionPool& obj) = delete;
    ConnectionPool& operator=(const ConnectionPool& obj) = delete;

    // 获取数据库连接 (返回智能指针，自动归还)
    shared_ptr<MysqlConn> getConnection();

    // 析构函数
    ~ConnectionPool();

private:
    // 构造函数 (私有，单例模式)
    ConnectionPool();

    // 内部功能函数
    bool parseJsonFile();      // 解析配置文件
    void produceConnection();  // 生产者线程：创建连接
    void recycleConnection();  // 回收线程：销毁空闲连接
    void addConnection();      // 创建一个具体连接并加入队列

    // --- 配置信息 ---
    string m_ip;
    string m_user;
    string m_passwd;
    string m_dbName;
    unsigned short m_port;
    int m_minSize;
    int m_maxSize;
    int m_timeout;         // 获取连接超时时间 (ms)
    int m_maxIdleTime;     // 连接最大空闲时间 (ms)

    // --- 运行时状态 ---
    int m_connectionCnt;   // 当前总连接数 (受 m_mutexQ 保护)
    
    queue<MysqlConn*> m_connectionQ; // 连接队列

    // --- 线程同步工具 ---
    mutex m_mutexQ;                // 互斥锁
    condition_variable m_cond;     // 条件变量
    
    // --- 新增：线程控制 ---
    atomic<bool> m_isRunning;      // 【新增】控制线程运行标志 (原子操作，无锁安全读取)
    thread m_producerThread;       // 【新增】生产者线程对象
    thread m_recyclerThread;       // 【新增】回收者线程对象
};