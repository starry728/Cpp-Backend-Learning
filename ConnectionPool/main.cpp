#include <iostream>
#include <memory>
#include "MysqlConn.h"
#include "ConnectionPool.h"
using namespace std;

int query()
{
    MysqlConn conn;
    conn.connect("root","77520cxy","test1", "172.31.192.239");
    string sql = "insert into tb_user values(5, 'tony', 24, '男')";
    bool flag = conn.update(sql);
    cout << "flag value: " << flag << endl;

    sql = "select * from tb_user";
    conn.query(sql);
    while(conn.next())
    {
        cout << conn.value(0) << ", "//value方法返回来的都是字符串类型哦
            << conn.value(1) << ", "
            << conn.value(2) << ", "
            << conn.value(3) << endl;
    }

    return 0;
}
int main()
{
    {
        ConnectionPool* pool = ConnectionPool::getConnectPool();
        auto conn = pool->getConnection();

        string sql = "insert into tb_user values(45, 'toyo', 28, '男')";
        conn->update(sql);

        sql = "select * from tb_user";
        conn->query(sql);

        while(conn->next())
        {
            cout << conn->value(0) << ", "
                 << conn->value(1) << ", "
                 << conn->value(2) << ", "
                 << conn->value(3) << endl;
        }
    }

    exit(0);
}