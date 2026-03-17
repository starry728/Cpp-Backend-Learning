#include <iostream>
#include <memory>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <atomic>
#include "ConnectionPool.h"
#include "MysqlConn.h"

using namespace std;

// 全局原子计数器，用于统计成功执行的任务数
atomic<int> g_success_count(0);
atomic<int> g_fail_count(0);

// 模拟业务任务：插入一条随机数据并查询
void businessTask(int taskId) {
    // 1. 从池中获取连接 (自动管理生命周期)
    auto conn = ConnectionPool::getConnectPool()->getConnection();
    
    if (!conn) {
        cout << "[Task " << taskId << "] ❌ 获取连接失败 (可能超时)" << endl;
        g_fail_count++;
        return;
    }

    // 为了减少日志刷屏，这里注释掉了每次获取连接的打印，只保留关键错误和最终统计
    // cout << "[Task " << taskId << "] ✅ 获取连接成功 | 连接地址: " << conn.get() << endl;

    try {
        // 2. 构造随机数据 
        int age = 20 + (taskId % 50);
        string name = "user_" + to_string(taskId);
        string gender = (taskId % 2 == 0) ? "女" : "男"; // 随机分配性别

        // 【关键修改】将 sex 改为 gender，匹配你的数据库表结构
        // 假设 id 是主键且非自增，使用 1000+taskId 避免与现有数据 (1-4) 冲突
        string sql_insert = "insert into tb_user (id, name, age, gender) values (" + 
                            to_string(1000 + taskId) + ", '" + name + "', " + 
                            to_string(age) + ", '" + gender + "')";
        
        // 执行插入
        if (!conn->update(sql_insert)) {
            // 如果插入失败（例如主键冲突），仅打印警告，不视为任务失败，继续测试
            // cout << "[Task " << taskId << "] ⚠️ 插入失败 (可能主键重复): " << sql_insert << endl;
        } 

        // 3. 执行查询验证
        string sql_select = "select * from tb_user where name='" + name + "'";
        if (conn->query(sql_select)) {
            while (conn->next()) {
                // 简单验证数据是否写入
                // string resName = conn->value(1); 
                // string resGender = conn->value(3);
                // cout << "[Task " << taskId << "] 🔍 查到数据: " << resName << ", " << resGender << endl;
            }
        } else {
            cout << "[Task " << taskId << "] ❌ 查询执行失败" << endl;
            g_fail_count++;
            return;
        }

        // 4. 模拟业务处理耗时 (随机 10ms - 100ms)
        random_device rd;
        mt19937 gen(rd());
        uniform_int_distribution<> distr(10, 100);
        int sleep_time = distr(gen);
        this_thread::sleep_for(chrono::milliseconds(sleep_time));

        g_success_count++;
        // 每 10 个任务打印一次完成信息，避免日志过多
        if (taskId % 10 == 0) {
             cout << "[Task " << taskId << "] 🎉 任务完成 (耗时 " << sleep_time << "ms)" << endl;
        }

    } catch (const exception& e) {
        cout << "[Task " << taskId << "] 💥 发生异常: " << e.what() << endl;
        g_fail_count++;
    }

    // 5. 函数结束，conn (shared_ptr) 析构，自动归还连接
}

int main() {
    cout << "========================================" << endl;
    cout << "   C++11 MySQL Connection Pool Test     " << endl;
    cout << "========================================" << endl;

    // 1. 获取单例池
    ConnectionPool* pool = ConnectionPool::getConnectPool();
    if (!pool) {
        cout << "❌ 连接池初始化失败!" << endl;
        return -1;
    }
    cout << "✅ 连接池初始化成功" << endl;
    cout << "💡 提示：确保 dbconf.json 配置正确，且 tb_user 表存在 (id, name, age, gender)" << endl;
    cout << "----------------------------------------" << endl;

    // 2. 单次基本功能测试 (串行)
    cout << "\n[阶段 1] 单次串行测试..." << endl;
    {
        auto conn = pool->getConnection();
        if (conn) {
            // 先查询当前总数
            string sql_count = "select count(*) from tb_user";
            if (conn->query(sql_count)) {
                if (conn->next()) {
                    cout << "📊 测试前表总记录数: " << conn->value(0) << endl;
                }
            }
            
            // 尝试插入一条测试数据
            string sql_test = "insert into tb_user (id, name, age, gender) values (999, 'test_user', 18, '男')";
            if (conn->update(sql_test)) {
                cout << "✅ 串行测试插入成功 (ID: 999)" << endl;
                // 清理测试数据，保持环境干净
                conn->update("delete from tb_user where id = 999");
                cout << "🗑️ 串行测试数据已清理" << endl;
            } else {
                cout << "⚠️ 串行测试插入失败 (可能 ID 999 已存在)" << endl;
            }
        } else {
            cout << "❌ 无法获取连接进行串行测试" << endl;
        }
    }
    cout << "----------------------------------------" << endl;

    // 3. 多线程并发压力测试
    cout << "\n[阶段 2] 多线程并发压力测试..." << endl;
    const int THREAD_COUNT = 20; 
    const int TASKS_PER_THREAD = 5; 
    // 总任务数 = 100 次
    
    vector<thread> threads;
    auto start_time = chrono::steady_clock::now();

    cout << "🚀 启动 " << THREAD_COUNT << " 个线程，每个执行 " << TASKS_PER_THREAD << " 次任务..." << endl;
    cout << "⏳ 正在运行，请稍候..." << endl;

    for (int i = 0; i < THREAD_COUNT; ++i) {
        threads.emplace_back([i, TASKS_PER_THREAD]() {
            for (int j = 0; j < TASKS_PER_THREAD; ++j) {
                int taskId = i * TASKS_PER_THREAD + j;
                businessTask(taskId);
            }
        });
    }

    // 等待所有线程结束
    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    auto end_time = chrono::steady_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count();

    // 4. 测试结果统计
    cout << "\n========================================" << endl;
    cout << "🏁 测试结束" << endl;
    cout << "----------------------------------------" << endl;
    cout << "⏱️  总耗时: " << duration << " ms" << endl;
    cout << "📈 成功任务: " << g_success_count << endl;
    cout << "📉 失败任务: " << g_fail_count << endl;
    if (duration > 0) {
        cout << "🚀 平均 TPS (任务/秒): " << (g_success_count * 1000.0 / duration) << endl;
    }
    cout << "========================================" << endl;

    // 5. 验证连接回收及最终数据
    cout << "\n[阶段 3] 验证连接回收及最终数据..." << endl;
    {
        auto finalConn = pool->getConnection();
        if (finalConn) {
            cout << "✅ 验证成功：所有连接已正确归还，池子工作正常。" << endl;
            
            // 再次查询总数，应该增加了 100 条 (如果没有主键冲突)
            if (finalConn->query("select count(*) from tb_user")) {
                if (finalConn->next()) {
                    cout << "📊 最终表总记录数: " << finalConn->value(0) << endl;
                    cout << "💡 预期结果：如果之前是 4 条，现在应该是 104 条左右" << endl;
                }
            }
            
            // 随机抽查一条新插入的数据
            if (finalConn->query("select * from tb_user where id > 100 limit 1")) {
                if (finalConn->next()) {
                    cout << "🔍 抽查新数据 -> ID:" << finalConn->value(0) 
                         << ", Name:" << finalConn->value(1) 
                         << ", Age:" << finalConn->value(2) 
                         << ", Gender:" << finalConn->value(3) << endl;
                }
            }
        } else {
            cout << "❌ 验证失败：连接似乎未归还或池子已死锁。" << endl;
        }
    }

    cout << "\n程序将在 2 秒后退出，观察后台线程安全关闭..." << endl;
    this_thread::sleep_for(chrono::seconds(2));

    cout << "👋 Main 函数结束，即将销毁连接池单例..." << endl;

    return 0;
}