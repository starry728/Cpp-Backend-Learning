#include "SessionManager.h"
#include <sstream>
#include <iomanip>
#include <iostream>

SessionManager& SessionManager::getInstance() {
    static SessionManager instance;
    return instance;
}

SessionManager::SessionManager() : gen_(rd_()) {}

SessionManager::~SessionManager() {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.clear();
}

std::string SessionManager::createSession(const std::string& username, 
                                          int userId, 
                                          int durationMinutes) {
    std::string sessionId = generateSessionId();
    
    Session session;
    session.username = username;
    session.userId = userId;
    session.expireTime = std::chrono::steady_clock::now() + 
                         std::chrono::minutes(durationMinutes);
    session.isValid = true;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[sessionId] = std::move(session);
    }
    
    std::cout << "[SessionManager] 创建Session: " << sessionId 
              << " 用户: " << username << std::endl;
    return sessionId;
}

Session SessionManager::getSession(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = sessions_.find(sessionId);
    if (it == sessions_.end()) {
        return Session(); // 无效Session
    }
    
    // 检查是否过期（惰性删除）
    if (std::chrono::steady_clock::now() > it->second.expireTime) {
        std::cout << "[SessionManager] Session过期: " << sessionId << std::endl;
        sessions_.erase(it);
        return Session();
    }
    
    return it->second;
}

void SessionManager::destroySession(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(sessionId);
    std::cout << "[SessionManager] 销毁Session: " << sessionId << std::endl;
}

void SessionManager::cleanupExpired() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    
    for (auto it = sessions_.begin(); it != sessions_.end(); ) {
        if (now > it->second.expireTime) {
            std::cout << "[SessionManager] 清理过期Session: " << it->first << std::endl;
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

size_t SessionManager::getSessionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

std::string SessionManager::generateSessionId() {
    // 生成128位随机数（32个十六进制字符）
    std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    for (int i = 0; i < 32; ++i) {
        ss << std::hex << dis(gen_);
    }
    
    // 添加时间戳避免碰撞
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    ss << std::hex << (now & 0xFFFFFFFF);
    
    return ss.str();
}