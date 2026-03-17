#include "MysqlConn.h"
#include <iostream>
#include <chrono>

using namespace std;
using namespace chrono;

// 构造函数
MysqlConn::MysqlConn() : m_conn(nullptr), m_result(nullptr), m_row(nullptr), m_alivetime(steady_clock::now())
{
    m_conn = mysql_init(nullptr);
    if (m_conn == nullptr) {
        cout << "MySQL Init Failed: " << mysql_error(nullptr) << endl;
    }
    // 注意：字符集设置最好放在 connect 成功后，或者使用 mysql_options 在 init 后 connect 前设置
    // 这里暂时保留，但建议移至 connect
}

MysqlConn::~MysqlConn()
{
    // 先释放结果集
    freeResult();
    // 再关闭连接
    if (m_conn != nullptr) {
        mysql_close(m_conn);
        m_conn = nullptr;
    }
}

// 优化：使用 const 引用避免拷贝
bool MysqlConn::connect(const string& user, const string& passwd, const string& dbName, const string& ip, unsigned short port)
{
    if (m_conn == nullptr) {
        cout << "MySQL Connect Error: Connection object is null." << endl;
        return false;
    }

    MYSQL* ptr = mysql_real_connect(m_conn, ip.c_str(), user.c_str(), passwd.c_str(), 
                                    dbName.c_str(), port, nullptr, 0);
    
    if (ptr == nullptr) {
        cout << "MySQL Connect Error: " << mysql_error(m_conn) << endl;
        return false;
    }

    // 【修复】连接成功后再设置字符集
    if (mysql_set_character_set(m_conn, "utf8mb4") != 0) { // 建议用 utf8mb4 支持 emoji
        cout << "MySQL Set Charset Error: " << mysql_error(m_conn) << endl;
        // 这里可以选择是否断开连接，视严格程度而定
        // mysql_close(m_conn); m_conn = nullptr; return false;
    }

    refreshAliveTime();
    return true;
}

bool MysqlConn::update(const string& sql)
{
    if (m_conn == nullptr) return false;

    if (mysql_query(m_conn, sql.c_str())) {
        cout << "MySQL Update Error: " << mysql_error(m_conn) << " | SQL: " << sql << endl;
        return false;
    }
    return true;
}

bool MysqlConn::query(const string& sql)
{
    if (m_conn == nullptr) return false;

    // 释放旧结果集
    freeResult();

    if (mysql_query(m_conn, sql.c_str())) {
        cout << "MySQL Query Error: " << mysql_error(m_conn) << " | SQL: " << sql << endl;
        return false;
    }

    // 【修复】区分 SELECT 和非 SELECT 语句
    // mysql_field_count 返回预期结果集中的列数。如果是 UPDATE/INSERT，通常为 0
    if (mysql_field_count(m_conn) == 0) {
        // 这是一个不返回结果集的语句 (如 UPDATE, INSERT, DELETE)
        // 此时 m_result 应保持为 nullptr，不应调用 store_result
        // 如果业务希望 query() 只用于 SELECT，这里可以返回 false 并报错
        // 如果希望通用，可以返回 true 表示执行成功，但 next() 将直接返回 false
        return true; 
    }

    m_result = mysql_store_result(m_conn);
    if (m_result == nullptr) {
        // 期望有结果集但获取失败 (可能是内存不足或网络中断)
        cout << "MySQL Store Result Error: " << mysql_error(m_conn) << endl;
        return false;
    }

    return true;
}

bool MysqlConn::next()
{
    if (m_result == nullptr) {
        return false;
    }
    m_row = mysql_fetch_row(m_result);
    return (m_row != nullptr);
}

string MysqlConn::value(int index)
{
    if (m_result == nullptr || m_row == nullptr) {
        return "";
    }

    int rowCount = mysql_num_fields(m_result);
    if (index >= rowCount || index < 0) {
        return "";
    }

    char* val = m_row[index];
    if (val == nullptr) {
        return "NULL"; // 或者返回空字符串，视业务需求定
    }

    // 【优化】获取长度数组
    unsigned long* lengths = mysql_fetch_lengths(m_result);
    if (lengths == nullptr) {
        // 极端情况 fallback，尝试直接使用 strlen (不安全如果含 \0)
        return string(val); 
    }

    // 构造 string，指定长度，完美支持包含 \0 的二进制数据
    return string(val, lengths[index]);
}

bool MysqlConn::transaction()
{
    if (m_conn == nullptr) return false;
    // 【修复】mysql_autocommit 成功返回 0，失败返回非 0
    // 我们需要：成功 -> true, 失败 -> false
    return (mysql_autocommit(m_conn, false) == 0);
}

bool MysqlConn::commit()
{
    if (m_conn == nullptr) return false;
    return (mysql_commit(m_conn) == 0);
}

bool MysqlConn::rollback()
{
    if (m_conn == nullptr) return false;
    return (mysql_rollback(m_conn) == 0);
}

void MysqlConn::freeResult()
{
    if (m_result != nullptr) {
        mysql_free_result(m_result);
        m_result = nullptr;
        m_row = nullptr; // 重要：行指针也随之失效，置空防止悬空
    }
}

void MysqlConn::refreshAliveTime()
{
    m_alivetime = steady_clock::now();
}

long long MysqlConn::getAliveTime()
{
    nanoseconds res = steady_clock::now() - m_alivetime;
    return duration_cast<milliseconds>(res).count();
}