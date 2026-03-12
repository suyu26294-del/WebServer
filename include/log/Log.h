#pragma once

#include "log/BlockQueue.h"

#include <cstdio>
#include <string>
#include <memory>
#include <mutex>
#include <thread>
#include <functional>

/**
 * @brief 异步日志系统（单例）
 *
 * 架构：
 *   前台线程  →  BlockQueue<string>  →  后台日志线程  →  文件
 *
 * 特性：
 *   - 支持 DEBUG / INFO / WARN / ERROR 四个级别
 *   - 按天自动切分日志文件
 *   - 异步模式下前台线程只写队列，几乎不阻塞
 *   - 优雅关闭：析构时排空队列再关文件
 */
class Log {
public:
    enum class Level { DEBUG = 0, INFO, WARN, ERROR };

    static Log& instance();

    /**
     * @param logDir   日志目录
     * @param suffix   文件后缀，默认 ".log"
     * @param maxQueue 异步队列容量，0 表示同步模式
     * @param level    最低输出级别
     */
    void init(const char* logDir   = "./log",
              const char* suffix   = ".log",
              int         maxQueue = 1024,
              Level       level    = Level::INFO);

    void write(Level level, const char* format, ...);
    void flush();

    Level  level() const noexcept { return level_; }
    bool   isOpen() const noexcept { return fp_ != nullptr; }

    // 禁止拷贝
    Log(const Log&)            = delete;
    Log& operator=(const Log&) = delete;

private:
    Log() = default;
    ~Log();

    void asyncFlushThread();   // 后台线程执行体
    bool openFile();           // 打开/切换日志文件
    void writeImpl(Level level, const char* msg);

    // 配置
    Level       level_   = Level::INFO;
    std::string logDir_;
    std::string suffix_;
    bool        async_   = false;
    int         today_   = 0;          // 当前日期（天）

    // 文件句柄
    FILE*       fp_      = nullptr;

    // 异步组件
    std::unique_ptr<BlockQueue<std::string>> queue_;
    std::thread                              flushThread_;

    mutable std::mutex mutex_;
};

// ── 日志宏 ──────────────────────────────────────────────────────────────────

#define LOG_BASE(lvl, format, ...)                                       \
    do {                                                                  \
        Log& _log_ = Log::instance();                                    \
        if (_log_.isOpen() && _log_.level() <= (lvl)) {                  \
            _log_.write((lvl), format, ##__VA_ARGS__);                   \
        }                                                                 \
    } while (0)

#define LOG_DEBUG(format, ...) LOG_BASE(Log::Level::DEBUG, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...)  LOG_BASE(Log::Level::INFO,  format, ##__VA_ARGS__)
#define LOG_WARN(format, ...)  LOG_BASE(Log::Level::WARN,  format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) LOG_BASE(Log::Level::ERROR, format, ##__VA_ARGS__)
