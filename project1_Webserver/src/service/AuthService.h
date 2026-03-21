#ifndef AUTH_SERVICE_H
#define AUTH_SERVICE_H

#include <string>
#include <functional>
#include "MysqlConn.h"

/**
 * @brief 认证服务类
 * 
 * 封装用户认证相关业务逻辑：
 * - 用户注册（检查重复、密码加密存储）
 * - 用户登录（验证密码、创建Session）
 * - 密码修改
 * 
 * 使用ConnectionPool获取数据库连接
 * 所有操作都是同步的（由ThreadPool线程调用）
 */
class AuthService {
public:
    /**
     * @brief 注册新用户
     * @param username 用户名（3-20字符，字母数字下划线）
     * @param password 密码（明文，内部加密存储）
     * @param gender 性别（男/女/其他）
     * @param age 年龄（1-150）
     * @return 成功返回用户ID，失败返回-1
     */
    static int registerUser(const std::string& username, 
                           const std::string& password,
                           const std::string& gender = "未知",
                           int age = 0);

    /**
     * @brief 用户登录验证
     * @param username 用户名
     * @param password 明文密码
     * @return 成功返回用户ID，失败返回-1
     */
    static int loginUser(const std::string& username, 
                          const std::string& password);

    /**
     * @brief 检查用户名是否已存在
     */
    static bool isUsernameExists(const std::string& username);

private:
    // 简单密码加密（实际项目应使用bcrypt等）
    static std::string encryptPassword(const std::string& password);
    
    // 验证输入合法性
    static bool validateUsername(const std::string& username);
    static bool validatePassword(const std::string& password);
};

#endif // AUTH_SERVICE_H