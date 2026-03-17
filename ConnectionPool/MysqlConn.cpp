#include "MysqlConn.h"

MysqlConn::MysqlConn()
{
    m_conn = mysql_init(nullptr);
    mysql_set_character_set(m_conn, "utf8");
}
MysqlConn::~MysqlConn()
{
    if(m_conn != nullptr)
    {
        mysql_close(m_conn);
    }
    freeResult();
}
bool MysqlConn::connect(string user, string passwd, string dbName, string ip, unsigned short port)
{
    MYSQL* ptr = mysql_real_connect(m_conn, ip.c_str(), user.c_str(), passwd.c_str(), dbName.c_str(), port, nullptr, 0);
    if(ptr == nullptr)
    {
        cout << "MySQL Connect Error: "
             << mysql_error(m_conn) << endl;
        return false;
    }
    return ptr != nullptr;
}
bool MysqlConn::update(string sql)
{
    if(mysql_query(m_conn, sql.c_str()))//c_str() 把string转换为const char*,因为SQL只能识别旧一点的char*;成功返回零，失败非零
    {
        // 新增这一行，把 MySQL 的真实报错打印出来！
        cout << "MySQL Update Error: " << mysql_error(m_conn) << endl;
        return false;
    }
    return true;
}
bool MysqlConn::query(string sql)
{
    freeResult();//清空上次查询结果内存
    if(mysql_query(m_conn, sql.c_str()))
    {
        return false;
    }
    m_result = mysql_store_result(m_conn);
    if(m_result == nullptr)
    {
        return false;
    }
    return true;
}
bool MysqlConn::next()
{
    if(m_result != nullptr)
    {
        m_row = mysql_fetch_row(m_result);
        if(m_row != nullptr)
        {
            return true;
        }
    }
    return false;
}
string MysqlConn::value(int index)
{
    int rowCount = mysql_num_fields(m_result);
    if(index >= rowCount || index < 0)
    {
        return string();//有问题，返回空字符串
    }
    char* val = m_row[index];
    if(val == nullptr)
    {
        return "NULL";
    }
    unsigned long length = mysql_fetch_lengths(m_result)[index];//index位置的字段长度
    return string(val, length);//char*转回string类型;string()就不会以/0为结束符，而是以传入长度为准
}
bool MysqlConn::transaction()
{
    return mysql_autocommit(m_conn, false);//设置为手动提交
}
bool MysqlConn::commit()
{
    return mysql_commit(m_conn);
}
bool MysqlConn::rollback()
{
    return mysql_rollback(m_conn);
}
void MysqlConn::freeResult()
{
    if(m_result)
    {
        mysql_free_result(m_result);
        m_result = nullptr;
    }
}
void MysqlConn::refreshAliveTime()
{
    m_alivetime = steady_clock::now();
}
long long MysqlConn::getAliveTime()
{
    nanoseconds res = steady_clock::now() - m_alivetime;
    milliseconds millsec = duration_cast<milliseconds>(res);//从ns转换成ms
    return millsec.count();
}
