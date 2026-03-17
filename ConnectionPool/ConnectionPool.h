#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include "MysqlConn.h"
using namespace std;
class ConnectionPool//单例模式（有且只有一个连接池对象）：懒汉模式（使用时才创建）
{
public:
    static ConnectionPool* getConnectPool();//静态成员函数，得到连接池的一个实例（在C++11里面，使用静态的局部变量是线程安全的）
    ConnectionPool(const ConnectionPool& obj) = delete;//=delete显示删掉对应函数（这里是删拷贝构造函数）
    ConnectionPool& operator=(const ConnectionPool& obj) =delete;//防止对象的复制
    shared_ptr<MysqlConn> getConnection();//用户取得一个连接
    ~ConnectionPool();
private:
    ConnectionPool();
    bool parseJsonFile();//解析json文件
    void produceConnection();
    void recycleConnection();
    void addConnection();

    string m_ip;
    string m_user;
    string m_passwd;
    string m_dbName;
    unsigned short m_port;
    int m_minSize;
    int m_maxSize;//连接池最大连接数
    int m_timeout;//超时时长,单位是毫秒
    int m_maxIdleTime;//最大空闲时间,单位是毫秒
    int m_connectionCnt;   // 当前连接池总连接数
    queue<MysqlConn*> m_connectionQ;
    mutex m_mutexQ;
    condition_variable m_cond;//条件变量->生产者、消费者线程。因为比较简单、故共用的同一个条件（m_cond既阻塞生产者、又阻塞消费者）
};