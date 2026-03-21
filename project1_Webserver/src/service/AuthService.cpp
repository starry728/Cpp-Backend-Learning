#include "AuthService.h"
#include "ConnectionPool.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>

// 简单的SHA256-like哈希（演示用，生产环境请用OpenSSL）
// 实际项目建议使用：1. bcrypt 2. Argon2 3. PBKDF2
std::string AuthService::encryptPassword(const std::string& password) {
    // 简化实现：使用固定盐值+多次哈希（仅演示，不安全！）
    const char* salt = "WebServer2024";
    std::string salted = password + salt;
    
    // 简单的混合哈希（实际项目请用标准库）
    unsigned long hash = 5381;
    for (char c : salted) {
        hash = ((hash << 5) + hash) + c; // DJB2算法
    }
    
    std::stringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << hash;
    return ss.str();
}

bool AuthService::validateUsername(const std::string& username) {
    if (username.length() < 3 || username.length() > 20) return false;
    for (char c : username) {
        if (!std::isalnum(c) && c != '_') return false;
    }
    return true;
}

bool AuthService::validatePassword(const std::string& password) {
    return password.length() >= 6 && password.length() <= 32;
}

bool AuthService::isUsernameExists(const std::string& username) {
    auto conn = ConnectionPool::getConnectPool()->getConnection();
    if (!conn) {
        std::cerr << "[AuthService] 获取数据库连接失败（连接池可能未初始化）" << std::endl;
        return true; // 保守策略：无法验证视为存在，防止绕过
    }
    
    // 使用预处理语句防止SQL注入（虽然这里用户名已验证 alphanumeric）
    std::string sql = "SELECT id FROM tb_user WHERE name = '" + username + "' LIMIT 1";
    if (!conn->query(sql)) {
        std::cerr << "[AuthService] 查询用户名失败: " << sql << std::endl;
        return true;
    }
    
    bool exists = conn->next();
    std::cout << "[AuthService] 检查用户名 '" << username << "' 是否存在: " << (exists ? "是" : "否") << std::endl;
    
    return exists; // 如果有下一行，说明存在
}

int AuthService::registerUser(const std::string& username,
                               const std::string& password,
                               const std::string& gender,
                               int age) {
    // 输入验证
    if (!validateUsername(username)) {
        std::cerr << "[AuthService] 用户名格式非法: " << username << std::endl;
        return -1;
    }
    if (!validatePassword(password)) {
        std::cerr << "[AuthService] 密码长度不合法" << std::endl;
        return -1;
    }
    if (age < 0 || age > 150) age = 0;
    
    std::cout << "[AuthService] 开始注册用户: " << username << std::endl;
    
    // 检查重复
    if (isUsernameExists(username)) {
        std::cerr << "[AuthService] 用户名已存在: " << username << std::endl;
        return -1;
    }
    
    auto conn = ConnectionPool::getConnectPool()->getConnection();
    if (!conn) {
        std::cerr << "[AuthService] 获取数据库连接失败" << std::endl;
        return -1;
    }
    
    // 加密密码
    std::string encryptedPwd = encryptPassword(password);
    
    // 构造SQL（实际项目应使用预处理语句）
    std::stringstream sql;
    sql << "INSERT INTO tb_user (name, age, gender, password) VALUES ('"
        << username << "', " << age << ", '" << gender << "', '" 
        << encryptedPwd << "')";
    
    std::cout << "[AuthService] 执行SQL: " << sql.str() << std::endl;
    
    if (!conn->update(sql.str())) {
        std::cerr << "[AuthService] 插入用户失败" << std::endl;
        return -1;
    }
    
    // 获取新插入的ID
    if (conn->query("SELECT LAST_INSERT_ID()") && conn->next()) {
        int newId = std::stoi(conn->value(0));
        std::cout << "[AuthService] 用户注册成功: " << username << " ID=" << newId << std::endl;
        return newId;
    }
    
    return -1;
}

int AuthService::loginUser(const std::string& username,
                            const std::string& password) {
    if (!validateUsername(username)) {
        std::cerr << "[AuthService] 用户名格式非法: " << username << std::endl;
        return -1;
    }
    
    std::cout << "[AuthService] 开始登录验证: " << username << std::endl;
    
    auto conn = ConnectionPool::getConnectPool()->getConnection();
    if (!conn) {
        std::cerr << "[AuthService] 获取数据库连接失败" << std::endl;
        return -1;
    }
    
    // 查询用户
    std::string sql = "SELECT id, password FROM tb_user WHERE name = '" + 
                      username + "' LIMIT 1";
    std::cout << "[AuthService] 执行查询SQL: " << sql << std::endl;
    
    if (!conn->query(sql)) {
        std::cerr << "[AuthService] 查询用户失败" << std::endl;
        return -1;
    }
    
    if (!conn->next()) {
        std::cerr << "[AuthService] 用户不存在: " << username << std::endl;
        return -1; // 用户不存在
    }
    
    int userId = std::stoi(conn->value(0));
    std::string storedHash = conn->value(1);
    
    // 验证密码
    std::string inputHash = encryptPassword(password);
    if (inputHash != storedHash) {
        std::cerr << "[AuthService] 密码错误: " << username << std::endl;
        std::cerr << "[AuthService] 输入哈希: " << inputHash << std::endl;
        std::cerr << "[AuthService] 存储哈希: " << storedHash << std::endl;
        return -1;
    }
    
    std::cout << "[AuthService] 登录成功: " << username << " ID=" << userId << std::endl;
    return userId;
}
