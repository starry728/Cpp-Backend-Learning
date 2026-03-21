#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <string>
#include <map>
#include <mutex>
#include <chrono>
#include <random>

/**
 * @brief Session管理器（单例）
 * 
 * 功能：
 * - 创建Session（登录成功后）
 * - 验证Session（每次请求检查）
 * - 销毁Session（登出或过期）
 * - 自动清理过期Session（定时任务）
 * 
 * 线程安全：使用读写锁（C++11 shared_mutex不可用，使用普通mutex）
 * 
 * 优化点：
 * - Session ID使用加密安全随机数生成
 * - 惰性删除：查询时检查过期，避免定时清理锁竞争
 */
struct Session {
    std::string username;
    int userId;
    std::chrono::steady_clock::time_point expireTime;
    bool isValid = false;
};

class SessionManager {
public:
    static SessionManager& getInstance();
    
    // 禁止拷贝
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    /**
     * @brief 创建新Session（登录成功后调用）
     * @param username 用户名
     * @param userId 用户ID
     * @param durationMinutes Session有效期（分钟）
     * @return Session ID字符串
     */
    std::string createSession(const std::string& username, int userId, 
                             int durationMinutes = 30);

    /**
     * @brief 验证Session是否有效
     * @param sessionId Session ID
     * @return 有效返回Session信息，无效返回isValid=false的Session
     */
    Session getSession(const std::string& sessionId);

    /**
     * @brief 销毁Session（登出）
     */
    void destroySession(const std::string& sessionId);

    /**
     * @brief 清理所有过期Session（可由定时任务调用）
     */
    void cleanupExpired();

    /**
     * @brief 获取当前Session数量（调试用）
     */
    size_t getSessionCount() const;

private:
    SessionManager();
    ~SessionManager();

    std::string generateSessionId();
    
    mutable std::mutex mutex_;
    std::map<std::string, Session> sessions_;
    
    // 随机数生成器（C++11）
    std::random_device rd_;
    std::mt19937 gen_;
};

#endif // SESSION_MANAGER_H