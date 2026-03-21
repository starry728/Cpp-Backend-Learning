#include "ConnectionPool.h"
#include <jsoncpp/json/json.h>
#include <fstream>
#include <thread>
#include <atomic>
#include <iostream>
#include <cstdlib>  // 新增：包含 getenv 函数

using namespace std;
using namespace Json;

// 单例获取
ConnectionPool* ConnectionPool::getConnectPool()
{
    static ConnectionPool pool;
    return &pool;
}

ConnectionPool::ConnectionPool() : m_isRunning(true), m_connectionCnt(0)
{
    if (!parseJsonFile()) {
        // 配置文件读取失败，可以选择抛出异常或退出，这里暂时打印错误
        cerr << "Failed to load dbconf.json, connection pool init failed." << endl;
        m_isRunning = false;
        return;
    }

    // 初始化最小连接数
    for (int i = 0; i < m_minSize; ++i) {
        addConnection();
    }

    // 启动生产者和回收者线程，保存线程对象以便后续 join
    m_producerThread = thread(&ConnectionPool::produceConnection, this);
    m_recyclerThread = thread(&ConnectionPool::recycleConnection, this);
    
    // 注意：不要 detach，要在析构时 join，确保线程安全退出
}

ConnectionPool::~ConnectionPool()
{
    // 1. 通知线程停止
    m_isRunning = false;
    m_cond.notify_all(); // 唤醒所有等待的线程，让它们检查 m_isRunning

    // 2. 等待线程结束 (避免析构时线程还在访问成员变量)
    if (m_producerThread.joinable()) {
        m_producerThread.join();
    }
    if (m_recyclerThread.joinable()) {
        m_recyclerThread.join();
    }

    // 3. 清理剩余连接 (此时已无线程竞争，可以不加锁，但为了规范还是加上)
    unique_lock<mutex> locker(m_mutexQ);
    while (!m_connectionQ.empty()) {
        MysqlConn* conn = m_connectionQ.front();
        m_connectionQ.pop();
        delete conn;
        m_connectionCnt--; 
    }
}

bool ConnectionPool::parseJsonFile()
{
    // 【关键修改】尝试多个可能的配置文件路径
    vector<string> possiblePaths = {
        "dbconf.json",                 // 当前目录
        "../dbconf.json",              // 上一级目录
        "../../dbconf.json",           // 上两级目录
    };
    
    // 添加环境变量指定的路径
    const char* webRoot = getenv("WEB_ROOT");
    if (webRoot) {
        possiblePaths.push_back(string(webRoot) + "/dbconf.json");
        possiblePaths.push_back(string(webRoot) + "/../dbconf.json");
    }
    
    ifstream ifs;
    string configPath;
    
    // 尝试所有可能的路径
    for (const auto& path : possiblePaths) {
        ifs.open(path);
        if (ifs.is_open()) {
            configPath = path;
            cout << "[ConnectionPool] 找到配置文件: " << path << endl;
            break;
        }
    }
    
    if (!ifs.is_open()) {
        cerr << "[ConnectionPool] dbconf.json 打开失败，尝试了以下路径:" << endl;
        for (const auto& path : possiblePaths) {
            cerr << "  " << path << endl;
        }
        return false;
    }

    // 1. 创建 Builder 对象
    Json::CharReaderBuilder builder;
    
    // 2. 用于接收错误信息和解析结果
    string errs;
    Json::Value root;

    // 3. 使用 parseFromStream
    if (!Json::parseFromStream(builder, ifs, &root, &errs)) {
        cout << "JSON parse error: " << errs << endl;
        return false;
    }

    // 4. 提取配置
    if (root.isObject()) {
        m_ip = root["ip"].asString();
        m_port = root["port"].asInt();
        m_user = root["userName"].asString();
        m_passwd = root["password"].asString();
        m_dbName = root["dbName"].asString();
        m_minSize = root["minSize"].asInt();
        m_maxSize = root["maxSize"].asInt();
        m_maxIdleTime = root["maxIdleTime"].asInt();
        m_timeout = root["timeout"].asInt();
        
        cout << "[ConnectionPool] 数据库配置加载成功:" << endl;
        cout << "  IP: " << m_ip << ":" << m_port << endl;
        cout << "  数据库: " << m_dbName << endl;
        cout << "  用户: " << m_user << endl;
        cout << "  连接池: " << m_minSize << " ~ " << m_maxSize << endl;
        
        return true;
    }
    
    return false;
}

void ConnectionPool::addConnection()
{
    MysqlConn* conn = new MysqlConn;
    // 重试逻辑可以在这里做，或者由调用者处理
    if (!conn->connect(m_user, m_passwd, m_dbName, m_ip, m_port)) {
        cout << "MySQL Connect Failed: " << m_ip << ":" << m_port << endl;
        delete conn;
        return; // 创建失败不增加计数
    }

    conn->refreshAliveTime();
    m_connectionQ.push(conn);
    m_connectionCnt++; // 原子操作或在锁内操作均可，此处已在锁内
    // cout << "Connection added. Total: " << m_connectionCnt << endl;
}

void ConnectionPool::produceConnection()
{
    while (m_isRunning)
    {
        unique_lock<mutex> locker(m_mutexQ);
        
        // 如果连接数达到上限，等待
        // 使用 while 防止虚假唤醒，并检查是否应该停止
        while (m_connectionCnt >= m_maxSize && m_isRunning)
        {
            m_cond.wait(locker);//wait内部会：解锁locker（让其他人可以去拿这个锁），阻塞等待；被唤醒后重新上锁
        }

        if (!m_isRunning) break;

        // 双重检查，防止等待期间其他线程已经创建了连接导致超标（虽然上面的循环控制了，但习惯上好一点）
        // 实际上上面的 while 已经保证了 m_connectionCnt < m_maxSize 才会下来，除非被唤醒后逻辑变了
        // 这里直接创建即可
        addConnection();
        
        // 创建完成后，通知可能有线程在等待连接 (getConnection)
        m_cond.notify_all(); 
    }
}

void ConnectionPool::recycleConnection()
{
    while (m_isRunning)
    {
        this_thread::sleep_for(chrono::milliseconds(500));
        
        unique_lock<mutex> locker(m_mutexQ);
        
        // 只有当空闲连接数多于最小连接数时，才考虑回收
        while (static_cast<int>(m_connectionQ.size()) > m_minSize && m_isRunning)
        {
            MysqlConn* conn = m_connectionQ.front();
            
            // 检查存活时间
            if (conn->getAliveTime() >= m_maxIdleTime)
            {
                m_connectionQ.pop();
                delete conn;
                m_connectionCnt--; // 【关键修复】删除连接时必须减少计数
                // 通知生产者可以创建新连接了（如果之前因为满员而阻塞）
                m_cond.notify_one(); 
            }
            else
            {
                // 队列是按时间排序的吗？不一定。
                // 如果队列不是严格按时间排序，break 可能会漏掉后面超时的连接。
                // 但通常连接是先进先出，最早回来的最早超时。
                // 为了严谨，如果不排序，应该遍历或保持当前逻辑（假设 FIFO 近似 LRU）。
                // 当前逻辑：一旦发现一个没超时的，就停止本次扫描。
                // 优化：如果队列不是严格按时间排序，这里不能 break，应该继续检查下一个？
                // 但 queue 不支持遍历。如果要精确回收，需要用 list/deque 并配合定时排序，或者接受这种近似。
                // 鉴于性能，通常假设队头是最旧的。
                break; 
            }
        }
    }
}

shared_ptr<MysqlConn> ConnectionPool::getConnection()
{
    unique_lock<mutex> locker(m_mutexQ);

    // 等待直到队列不为空 或 超时
    // 必须用 while 循环处理虚假唤醒
    while (m_connectionQ.empty())
    {
        if (!m_isRunning) {
            return nullptr; // 池子已关闭
        }

        cv_status status = m_cond.wait_for(locker, chrono::milliseconds(m_timeout));
        
        if (status == cv_status::timeout)
        {
            if (m_connectionQ.empty())
            {
                // 超时且仍为空
                // 可以选择抛出异常或返回 nullptr，这里选择继续等待（模拟永久阻塞直到有连接）
                // 如果希望超时返回空，则：return nullptr;
                // 根据你的原代码逻辑是 continue (继续等)，那就不返回
                continue; 
            }
        }
        // 如果被唤醒且队列不空，跳出循环
        if (!m_connectionQ.empty()) break;
    }

    if (m_connectionQ.empty()) {
        return nullptr; // 防御性代码
    }

    MysqlConn* conn = m_connectionQ.front();
    m_connectionQ.pop();
    
    // 构造 shared_ptr，绑定自定义删除器
    // 捕获 this 指针有风险：如果池子析构了，shared_ptr 还没释放怎么办？
    // 解决方案：shared_ptr 控制连接生命周期，池子析构前必须确保所有借出的连接都归还。
    // 上面的析构函数 join 了线程，但没有强制回收借出的连接。
    // 这是一个设计难点。通常做法是：程序退出时，强制等待所有 shared_ptr 释放，或者在删除器中检查 m_isRunning。
    
    shared_ptr<MysqlConn> connptr(conn, [this](MysqlConn* p) {
        // 归还连接
        if (!m_isRunning) {
            // 如果池子已经关闭，直接删除，不再放回队列
            delete p;
            return;
        }
        
        unique_lock<mutex> locker(m_mutexQ);
        p->refreshAliveTime();
        m_connectionQ.push(p);
        // 通知可能有线程在等待连接
        m_cond.notify_one(); 
    });

    return connptr;
}
