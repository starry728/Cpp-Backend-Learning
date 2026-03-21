# Multi-Reactor Web Server

一个基于C++11的多Reactor高并发Web服务器，支持静态文件服务、用户认证和文件管理功能。

## 🎯 功能特性

- **高性能架构**：基于多Reactor模型，实现高并发处理
- **静态文件服务**：支持HTML、CSS、JavaScript、图片、视频等静态资源
- **用户认证系统**：注册、登录、会话管理
- **文件管理系统**：在线文件浏览、预览、下载
- **数据库连接池**：MySQL连接池管理，提高数据库访问效率
- **线程池**：业务逻辑异步处理，避免I/O阻塞
- **HTTP/1.1支持**：完整的HTTP协议实现

## 📁 项目结构

```
project1_Webserver/
├── webserver                 # 可执行文件
├── Makefile                  # 编译配置
├── README.md                 # 项目说明
├── dbconf.json              # 数据库配置文件
├── www/                     # 前端文件目录
│   ├── index.html          # 登录页面
│   ├── register.html       # 注册页面
│   ├── files.html          # 文件管理页面
│   └── dbconf.json         # 数据库配置
├── src/                     # 源代码目录
│   ├── main.cpp            # 程序入口
│   ├── server/             # 服务器核心模块
│   │   ├── Server.h/cpp    # 主Reactor
│   │   ├── SubReactor.h/cpp # 子Reactor
│   │   └── HttpRequest.h/cpp # HTTP请求解析
│   ├── pool/               # 连接池模块
│   │   ├── ThreadPool.h/cpp # 线程池
│   │   ├── ConnectionPool.h/cpp # 数据库连接池
│   │   └── MysqlConn.h/cpp # MySQL连接封装
│   └── service/            # 业务服务模块
│       ├── Router.h/cpp     # 请求路由
│       ├── SessionManager.h/cpp # 会话管理
│       ├── AuthService.h/cpp    # 认证服务
│       └── FileService.h/cpp    # 文件服务
└── 其他资源目录/           # 用户指定的资源目录
```

## 🔧 技术栈

- **语言**: C++11
- **网络库**: Linux epoll (边缘触发模式)
- **数据库**: MySQL
- **JSON解析**: jsoncpp
- **线程模型**: One Loop Per Thread + 线程池
- **HTTP协议**: 完整的HTTP/1.1实现

## 🚀 快速开始

### 环境要求

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install g++ make cmake mysql-server libmysqlclient-dev libjsoncpp-dev

# CentOS/RHEL
sudo yum install gcc-c++ make cmake mysql-server mysql-devel jsoncpp-devel
```

### 数据库设置

1. 启动MySQL服务：
```bash
sudo systemctl start mysql
sudo systemctl enable mysql
```

2. 创建数据库和用户表：
```sql
CREATE DATABASE IF NOT EXISTS test1;
USE test1;

CREATE TABLE IF NOT EXISTS tb_user (
    id INT PRIMARY KEY AUTO_INCREMENT,
    name VARCHAR(20) NOT NULL UNIQUE,
    age INT DEFAULT 0,
    gender VARCHAR(10) DEFAULT '未知',
    password VARCHAR(64) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

3. 配置数据库连接：
```json
// 编辑 www/dbconf.json
{
    "ip": "127.0.0.1",
    "port": 3306,
    "userName": "root",
    "password": "your_password",
    "dbName": "test1",
    "minSize": 5,
    "maxSize": 20,
    "maxIdleTime": 5000,
    "timeout": 1000
}
```

### 编译项目

```bash
# 克隆项目
git clone <repository-url>
cd project1_Webserver

# 编译
make clean
make

# 或者使用调试模式
make debug
```

### 运行服务器

```bash
# 基本用法
./webserver <端口> <资源目录> [子Reactor数量]

# 示例1：浏览图片目录
./webserver 8080 /home/user/Pictures 4

# 示例2：使用www目录作为资源
./webserver 8080 ./www 4

# 示例3：使用默认线程数
./webserver 8080 /path/to/files
```

## 📋 配置说明

### 命令行参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| 端口 | 服务器监听端口 | 必填 |
| 资源目录 | 要分享的文件目录 | 必填 |
| 子Reactor数量 | 工作线程数 | CPU核心数 |

### 环境变量

- `WEB_ROOT`: 前端文件目录（自动检测）
- `PIC_ROOT`: 资源文件目录（命令行参数设置）

## 🌐 API接口

### 用户认证
- `POST /api/login` - 用户登录
- `POST /api/register` - 用户注册
- `POST /api/logout` - 用户退出

### 文件管理
- `GET /api/files?path=<目录路径>` - 获取文件列表

### 响应格式
```json
// 成功响应
{
    "success": true,
    "data": [...]
}

// 错误响应
{
    "success": false,
    "message": "错误描述"
}
```

## 🎮 使用示例

1. **启动服务器**：
```bash
./webserver 8080 /home/user/Documents 4
```

2. **访问Web界面**：
   - 打开浏览器访问 `http://localhost:8080/`
   - 注册新账户或使用现有账户登录
   - 浏览和管理文件

3. **查看服务器日志**：
```bash
[Main] 服务器监听端口: 8080
[Main] 资源目录: /home/user/Documents
[Router] 处理请求: GET /api/files?path=/
[FileService] 扫描目录: /home/user/Documents
```

## 🔍 性能优化

### 架构特点
- **主从Reactor模式**：主线程负责连接分发，子线程负责I/O处理
- **边缘触发(ET)**：减少epoll_wait调用次数
- **线程池**：业务逻辑异步处理
- **零拷贝**：使用sendfile传输文件
- **连接池**：复用数据库连接

### 配置建议
- 子Reactor数量建议设置为CPU核心数
- 数据库连接池大小根据并发量调整
- 线程池任务队列大小根据业务负载调整

## 🐛 故障排除

### 常见问题

1. **端口占用**
```bash
# 查看端口占用
sudo lsof -i :8080
# 杀死占用进程
sudo kill -9 <PID>
```

2. **数据库连接失败**
   - 检查MySQL服务状态
   - 验证dbconf.json配置
   - 确认用户权限

3. **文件权限问题**
```bash
# 确保资源目录可读
chmod -R 755 /path/to/resources
```

4. **中文乱码**
   - 确保系统使用UTF-8编码
   - 检查文件名编码正确

### 调试模式
```bash
# 编译调试版本
make debug
# 运行并查看详细日志
./webserver 8080 ./www 4
```

## 📈 性能测试

### 测试工具
```bash
# 安装ab工具
sudo apt-get install apache2-utils

# 进行压力测试
ab -n 10000 -c 100 http://localhost:8080/
```

### 预期性能
- 静态文件：QPS 5000+
- API请求：QPS 3000+
- 并发连接：1000+

## 📄 许可证

本项目采用MIT许可证。详见LICENSE文件。

## 🤝 贡献指南

1. Fork本仓库
2. 创建功能分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 开启Pull Request

## 📚 学习资源

- https://book.douban.com/subject/24722611/
- https://book.douban.com/subject/27036085/
- https://beej.us/guide/bgnet/

## ✨ 致谢

感谢所有为本项目提供帮助的开发者，特别感谢：
- Linux epoll文档
- MySQL C API
- jsoncpp库
- 开源社区的支持

---

**Happy Coding!** 🚀

如果有任何问题或建议，请通过Issue提交反馈。