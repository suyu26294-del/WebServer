#ifdef HAVE_MYSQL

#include "pool/SqlConnPool.h"
#include "log/Log.h"

#include <stdexcept>
#include <chrono>

SqlConnPool& SqlConnPool::instance() {
    static SqlConnPool inst;
    return inst;
}

void SqlConnPool::init(const char* host, uint16_t port,
                       const char* user, const char* password,
                       const char* dbName, int poolSize) {
    host_     = host;
    port_     = port;
    user_     = user;
    password_ = password;
    dbName_   = dbName;
    poolSize_ = poolSize;

    for (int i = 0; i < poolSize_; ++i) {
        MYSQL* conn = ::mysql_init(nullptr);
        if (!conn) {
            LOG_ERROR("SqlConnPool: mysql_init failed");
            throw std::runtime_error("mysql_init failed");
        }
        conn = ::mysql_real_connect(conn, host_.c_str(), user_.c_str(),
                                    password_.c_str(), dbName_.c_str(),
                                    port_, nullptr, 0);
        if (!conn) {
            LOG_ERROR("SqlConnPool: mysql_real_connect failed");
            throw std::runtime_error("mysql_real_connect failed");
        }
        ::mysql_set_character_set(conn, "utf8mb4");
        pool_.push(conn);
    }
    LOG_INFO("SqlConnPool: initialized %d connections to %s:%u/%s",
             poolSize_, host, port, dbName);
}

void SqlConnPool::destroy() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!pool_.empty()) {
        ::mysql_close(pool_.front());
        pool_.pop();
    }
    ::mysql_library_end();
}

MYSQL* SqlConnPool::getConn(int timeoutMs) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                      [this] { return !pool_.empty(); })) {
        LOG_WARN("SqlConnPool: getConn timeout");
        return nullptr;
    }
    MYSQL* conn = pool_.front();
    pool_.pop();

    if (::mysql_ping(conn) != 0) {
        ::mysql_close(conn);
        conn = ::mysql_init(nullptr);
        conn = ::mysql_real_connect(conn, host_.c_str(), user_.c_str(),
                                    password_.c_str(), dbName_.c_str(),
                                    port_, nullptr, 0);
        if (!conn) {
            LOG_ERROR("SqlConnPool: reconnect failed");
            cv_.notify_one();
            return nullptr;
        }
        ::mysql_set_character_set(conn, "utf8mb4");
    }
    return conn;
}

void SqlConnPool::freeConn(MYSQL* conn) {
    if (!conn) return;
    std::lock_guard<std::mutex> lock(mutex_);
    pool_.push(conn);
    cv_.notify_one();
}

int SqlConnPool::freeCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(pool_.size());
}

#endif  // HAVE_MYSQL
