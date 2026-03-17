#include "ConnectionPool.h"
#include <jsoncpp/json/json.h>
#include <fstream>
#include <thread>
using namespace Json;
ConnectionPool* ConnectionPool::getConnectPool()//静态函数
{
    static ConnectionPool pool;//pool是静态的局部对象（是线程安全的），访问范围是当前的函数，但生命周期是当程序结束后才被析构（所以不是调用一次创一次）
    return &pool;//当前类的唯一实例
}

bool ConnectionPool::parseJsonFile()//使用jsoncpp解析json文件
{
    ifstream ifs("dbconf.json");//把文件读到磁盘里面，用到ifstream这个类
    if(!ifs.is_open()) {
    cout << "dbconf.json 打开失败" << endl;
    return false;
    }
    Reader rd;//添加Reader对象
    Value root;
    rd.parse(ifs, root);//传出参数（传出一个value类型的对象，引用）
    if(root.isObject())//如果是json对象
    {
        m_ip = root["ip"].asString();//转成字符串
        m_port = root["port"].asInt();//转成整形
        m_user = root["userName"].asString();
        m_passwd = root["password"].asString();
        m_dbName = root["dbName"].asString();
        m_minSize = root["minSize"].asInt();
        m_maxSize = root["maxSize"].asInt();
        m_maxIdleTime = root["maxIdleTime"].asInt();
        m_timeout = root["timeout"].asInt();
        return true;
    }
    return false;
}
void ConnectionPool::produceConnection()//生产数据库连接，保证有足够连接可被使用
{
    while(true)
    {
        unique_lock<mutex> locker(m_mutexQ);//包装了一个互斥锁对象，由locker管理（locker创建则mutex一起被创建，locker析构则一起销毁）
        while(m_connectionCnt >= m_maxSize)//此时阻塞。用while而不是用if循环、防止超过最大数量
        {
            m_cond.wait(locker);//消费者消费掉一个后，就唤醒这里的这个生产者
        }
        //否则需创建新连接
        addConnection();
        m_cond.notify_all();//通过条件变量唤醒（把阻塞的消费者都唤醒了）
    }    
}
void ConnectionPool::recycleConnection()//回收数据库连接
{
    while(true)
    {
        this_thread::sleep_for(chrono::milliseconds(500));//休眠0.5秒
        unique_lock<mutex> locker(m_mutexQ);
        while(m_connectionQ.size() > m_minSize)
        {
            MysqlConn* conn = m_connectionQ.front();//取出队头，它是先进来的存活时间最长
            if(conn->getAliveTime() >= m_maxIdleTime)//刷新起始时间点（连接被还回来的时间点）。单位都是ms
            {
                m_connectionQ.pop();
                delete conn;
            }
            else
            {
                break;
            }
        }
    }
}
void ConnectionPool::addConnection()
{
    MysqlConn* conn = new MysqlConn;
    if(!conn->connect(m_user, m_passwd, m_dbName, m_ip, m_port)) {
        cout << "MySQL连接失败: " << m_ip << ":" << m_port << endl;
        delete conn;

        this_thread::sleep_for(chrono::seconds(1));
        return;
    }
    conn->refreshAliveTime();//连接建立成功后就需要建立时间戳了
    m_connectionQ.push(conn);
    m_connectionCnt++;
}
shared_ptr<MysqlConn> ConnectionPool::getConnection()//智能共享指针
{
    unique_lock<mutex> locker(m_mutexQ);
    while(m_connectionQ.empty())//如果是空的
    {
       if(cv_status::timeout ==  m_cond.wait_for(locker, chrono::milliseconds(m_timeout)))//wait_for会阻塞一个指定的时间长度（到时间自动解除阻塞）；wait方法不调用解除阻塞的函数则会一直阻塞
        {
            if(m_connectionQ.empty())
            {
                //return nullptr;
                continue;//继续阻塞，直到连接队列不为空
            }
        }
    }
    shared_ptr<MysqlConn> connptr(m_connectionQ.front(), [this](MysqlConn* conn) {
        lock_guard<mutex>locker(m_mutexQ);
        conn->refreshAliveTime();
        m_connectionQ.push(conn);
        });
    m_connectionQ.pop();
    m_cond.notify_all();//通过条件变量唤醒（把阻塞的生产、消费者都唤醒了；但没有影响）
    return connptr;
}
ConnectionPool::~ConnectionPool()
{
    while(!m_connectionQ.empty())
    {
        MysqlConn* conn = m_connectionQ.front();
        m_connectionQ.pop();
        delete conn;
    }
}
ConnectionPool::ConnectionPool()
{
    // 加载配置文件
    if(!parseJsonFile())//加载成功
    {
        return;
    }
    for(int i=0;i<m_minSize;++i)//创建连接
    {
        addConnection();
    }
    thread producer(&ConnectionPool::produceConnection, this);
    thread recycler(&ConnectionPool::recycleConnection, this);
    producer.detach();//防止主线程阻塞，故采用线程分离
    recycler.detach();
}
