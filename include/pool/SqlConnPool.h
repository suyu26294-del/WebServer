#pragma once

#include <string>
#include <memory>

#ifdef HAVE_MYSQL
#include <mysql/mysql.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>

/**
 * @brief MySQL 连接池（单例）
 */
class SqlConnPool {
public:
    static SqlConnPool& instance();

    void init(const char* host, uint16_t port,
              const char* user, const char* password,
              const char* dbName, int poolSize = 10);

    void destroy();
    MYSQL* getConn(int timeoutMs = 3000);
    void   freeConn(MYSQL* conn);
    int    freeCount() const;
    int    totalCount() const noexcept { return poolSize_; }

    SqlConnPool(const SqlConnPool&)            = delete;
    SqlConnPool& operator=(const SqlConnPool&) = delete;

private:
    SqlConnPool()  = default;
    ~SqlConnPool() { destroy(); }

    std::queue<MYSQL*>      pool_;
    mutable std::mutex      mutex_;
    std::condition_variable cv_;
    int                     poolSize_ = 0;
    std::string             host_, user_, password_, dbName_;
    uint16_t                port_ = 3306;
};

/**
 * @brief RAII 连接守卫
 */
class SqlConnGuard {
public:
    explicit SqlConnGuard(int timeoutMs = 3000)
        : conn_(SqlConnPool::instance().getConn(timeoutMs)) {}
    ~SqlConnGuard() {
        if (conn_) SqlConnPool::instance().freeConn(conn_);
    }
    SqlConnGuard(const SqlConnGuard&)            = delete;
    SqlConnGuard& operator=(const SqlConnGuard&) = delete;
    SqlConnGuard(SqlConnGuard&& o) noexcept : conn_(o.conn_) { o.conn_ = nullptr; }

    explicit operator bool() const noexcept { return conn_ != nullptr; }
    MYSQL* operator*()  const noexcept { return conn_; }
    MYSQL* get()        const noexcept { return conn_; }

private:
    MYSQL* conn_ = nullptr;
};

#else  // !HAVE_MYSQL  ── 无 MySQL 时的空桩，保持代码可编译 ──────────────────

struct MYSQL {};  // 前向占位，不可实例化

class SqlConnPool {
public:
    static SqlConnPool& instance() { static SqlConnPool inst; return inst; }
    void init(const char*, unsigned short, const char*, const char*,
              const char*, int = 10) {}
    void    destroy() {}
    MYSQL*  getConn(int = 3000)  { return nullptr; }
    void    freeConn(MYSQL*)     {}
    int     freeCount()    const { return 0; }
    int     totalCount()   const noexcept { return 0; }
    SqlConnPool(const SqlConnPool&)            = delete;
    SqlConnPool& operator=(const SqlConnPool&) = delete;
private:
    SqlConnPool()  = default;
    ~SqlConnPool() = default;
};

class SqlConnGuard {
public:
    explicit SqlConnGuard(int = 3000) {}
    explicit operator bool() const noexcept { return false; }
    MYSQL* operator*()  const noexcept { return nullptr; }
    MYSQL* get()        const noexcept { return nullptr; }
};

#endif  // HAVE_MYSQL
