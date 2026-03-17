这是一个为你量身定制的 `README.md` 文件。它采用了专业的开源项目格式，清晰地介绍了项目背景、核心特性、架构改进点以及使用方法。

你可以直接将以下内容保存为项目根目录下的 `README.md` 文件。

---

# 🚀 C++11 高性能 MySQL 连接池 (High-Performance MySQL Connection Pool)

基于 **C++11** 标准实现的高性能、线程安全的 MySQL 数据库连接池。该项目解决了传统数据库操作中频繁创建/销毁连接带来的性能瓶颈，通过连接复用、自动资源管理和并发控制，显著提升了高并发场景下的数据库访问效率。

## ✨ 核心特性

-   **🔒 线程安全 (Thread-Safe)**：使用 `std::mutex` 和 `std::condition_variable` 实现严格的同步机制，确保多线程环境下连接获取与归还的原子性。
-   **♻️ 自动资源管理 (RAII)**：基于 `std::shared_ptr` 自定义删除器，实现连接使用的**自动归还**。用户无需手动释放连接，彻底杜绝资源泄漏。
-   **⚡ 高并发支持**：采用“生产 - 消费”模型，当连接池满时，请求线程自动阻塞等待，避免忙等，最大化利用系统资源。
-   **🤖 智能回收机制**：内置后台守护线程，定期扫描并关闭超过最大空闲时间 (`maxIdleTime`) 的连接，动态维持池内健康连接数。
-   **⚙️ 配置分离**：支持通过 `dbconf.json` 动态加载数据库配置（IP、端口、账号、池大小等），修改配置无需重新编译代码。
-   **📊 单例模式**：全局唯一连接池实例，确保资源集中管理。

## 🛠️ 技术栈

-   **语言标准**: C++11
-   **数据库驱动**: MySQL Connector C (`libmysqlclient`)
-   **并发库**: POSIX Threads (`pthread`), C++11 `<thread>`, `<atomic>`, `<mutex>`
-   **配置解析**: JsonCpp (`libjsoncpp`)
-   **构建工具**: CMake / Makefile

## 🏗️ 项目架构与改进亮点

相比传统的数据库连接方式，本项目在以下三个核心文件进行了深度重构与优化：

### 1. `ConnectionPool.h` & `ConnectionPool.cpp` (核心引擎)
| 改进点 | 原有实现 (推测) | **当前实现** | 优势 |
| :--- | :--- | :--- | :--- |
| **连接存储** | 无或简单列表 | `std::queue<MYSQL*>` + `std::mutex` | 实现 FIFO 复用，线程安全操作 |
| **流量控制** | 无限制或硬编码 | `std::condition_variable` + `std::atomic` | 连接满时自动挂起请求，防止数据库过载 |
| **生命周期** | 手动 `connect`/`close` | `std::shared_ptr` + **自定义删除器** | **自动归还**连接，代码更简洁，零泄漏风险 |
| **空闲清理** | 无 | **后台扫描线程** (`scanMysqlConnTask`) | 自动回收长时空闲连接，节省服务器资源 |
| **配置加载** | 硬编码字符串 | **JSON 动态解析** | 灵活适配不同环境，解耦代码与配置 |

### 2. `main.cpp` (压力测试与验证)
-   **并发压力测试**：从单线程串行测试升级为 **20 线程 × 5 任务** 的并发模型，模拟真实高负载场景。
-   **字段适配**：修正 SQL 语句以匹配实际数据库 schema (`sex` → `gender`)，确保测试数据准确写入。
-   **性能监控**：引入 `std::atomic` 计数器与高精度计时器，实时统计 **TPS (Transactions Per Second)**、成功/失败率及总耗时。
-   **完整性验证**：增加“最终数据校验”环节，自动查询数据库确认数据落盘情况，验证连接归还后的状态一致性。

## 📂 目录结构

```text
.
├── ConnectionPool.h      # 连接池头文件 (接口定义)
├── ConnectionPool.cpp    # 连接池实现 (核心逻辑)
├── MysqlConn.h           # MySQL 封装类头文件
├── MysqlConn.cpp         # MySQL 封装类实现
├── main.cpp              # 测试入口 (含高并发压力测试)
├── dbconf.json           # 数据库配置文件
├── CMakeLists.txt        # CMake 构建脚本 (如有)
└── README.md             # 项目说明文档
```

## 🚀 快速开始

### 1. 环境依赖
确保已安装以下开发库：
```bash
# Ubuntu/Debian
sudo apt-get install libmysqlclient-dev libjsoncpp-dev

# CentOS/RHEL
sudo yum install mysql-devel jsoncpp-devel
```

### 2. 配置文件
编辑 `dbconf.json`，填入你的数据库信息：
```json
{
  "ip": "172.31.192.239",
  "port": 3306,
  "username": "root",
  "password": "77520cxy",
  "dbname": "test1",
  "minSize": 5,
  "maxSize": 10,
  "maxIdleTime": 60
}
```

### 3. 数据库准备
在数据库中创建测试表（注意字段名为 `gender`）：
```sql
CREATE DATABASE IF NOT EXISTS test1;
USE test1;

CREATE TABLE IF NOT EXISTS tb_user (
  id INT PRIMARY KEY,
  name VARCHAR(50),
  age INT,
  gender VARCHAR(10)
);
```

### 4. 编译与运行

**方式 A: 使用 g++ 直接编译**
```bash
g++ -std=c++11 main.cpp ConnectionPool.cpp MysqlConn.cpp -o db_pool -lmysqlclient -lpthread -ljsoncpp
./db_pool
```

**方式 B: 使用 CMake (推荐)**
```bash
mkdir build && cd build
cmake ..
make
./db_pool
```

## 📈 预期输出示例

成功运行后，你将看到类似以下的输出，表明连接池在高并发下工作正常且数据已成功写入：

```text
========================================
   C++11 MySQL Connection Pool Test     
========================================
✅ 连接池初始化成功
...
[阶段 2] 多线程并发压力测试...
🚀 启动 20 个线程，每个执行 5 次任务...
...
========================================
🏁 测试结束
⏱️  总耗时: 850 ms
📈 成功任务: 100
🚀 平均 TPS: 117.6
========================================
[阶段 3] 验证连接回收及最终数据...
✅ 验证成功：所有连接已正确归还
📊 最终表总记录数: 104
🔍 抽查新数据 -> ID:1000, Name:user_0, Gender:女
```

## 📝 注意事项

1.  **主键冲突**：测试代码使用 `1000 + taskId` 作为插入 ID。若重复运行且未清理数据，可能会遇到主键冲突报错（这是预期的容错行为，不影响连接池功能验证）。
2.  **线程安全**：`MysqlConn` 类中的 `MYSQL*` 句柄是线程不安全的，因此本连接池保证**一个连接同一时刻只被一个线程占用**，通过互斥锁严格隔离。
3.  **异常处理**：若数据库服务宕机，连接池会在获取连接时抛出异常或返回空指针，调用方需做好判空处理。

## 📄 License

MIT License

---