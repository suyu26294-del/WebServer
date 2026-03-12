#include "log/Log.h"

#include <ctime>
#include <cstdarg>
#include <cstring>
#include <cassert>
#include <sys/stat.h>
#include <sys/time.h>
#include <filesystem>

namespace fs = std::filesystem;

// ── 单例 ─────────────────────────────────────────────────────────────────────

Log& Log::instance() {
    static Log inst;
    return inst;
}

Log::~Log() {
    if (async_ && queue_) {
        queue_->close();
        if (flushThread_.joinable()) {
            flushThread_.join();
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (fp_) {
        ::fflush(fp_);
        ::fclose(fp_);
        fp_ = nullptr;
    }
}

// ── 初始化 ───────────────────────────────────────────────────────────────────

void Log::init(const char* logDir, const char* suffix,
               int maxQueue, Level level) {
    level_   = level;
    logDir_  = logDir;
    suffix_  = suffix;
    async_   = (maxQueue > 0);

    // 确保日志目录存在
    fs::create_directories(logDir_);

    if (!openFile()) {
        // 无法打开日志文件时降级为 stderr
        fp_ = stderr;
    }

    if (async_) {
        queue_ = std::make_unique<BlockQueue<std::string>>(
            static_cast<size_t>(maxQueue));
        flushThread_ = std::thread([this] { asyncFlushThread(); });
    }
}

// ── 写日志 ───────────────────────────────────────────────────────────────────

void Log::write(Level level, const char* format, ...) {
    // 构造时间戳
    struct timeval now{};
    ::gettimeofday(&now, nullptr);
    struct tm t{};
    ::localtime_r(&now.tv_sec, &t);

    // 检查是否需要切换文件（跨天）
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (t.tm_mday != today_) {
            if (fp_ && fp_ != stderr) { ::fclose(fp_); fp_ = nullptr; }
            openFile();
        }
    }

    // 格式化日志级别标签
    static constexpr const char* kLevelStr[] = {
        "[DEBUG]", "[INFO] ", "[WARN] ", "[ERROR]"
    };
    const char* tag = kLevelStr[static_cast<int>(level)];

    // 格式化用户消息
    char userbuf[4096];
    va_list ap;
    va_start(ap, format);
    ::vsnprintf(userbuf, sizeof(userbuf), format, ap);
    va_end(ap);

    // 拼接完整日志行
    char linebuf[4096 + 128];
    ::snprintf(linebuf, sizeof(linebuf),
               "%04d-%02d-%02d %02d:%02d:%02d.%03ld %s %s\n",
               t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
               t.tm_hour, t.tm_min, t.tm_sec,
               now.tv_usec / 1000,
               tag, userbuf);

    if (async_ && queue_ && !queue_->isClosed()) {
        queue_->push(std::string(linebuf));
    } else {
        std::lock_guard<std::mutex> lock(mutex_);
        writeImpl(level, linebuf);
    }
}

void Log::writeImpl(Level /*level*/, const char* msg) {
    // 调用前需持有 mutex_（或在单线程/异步线程中调用）
    if (fp_) {
        ::fputs(msg, fp_);
    }
}

void Log::flush() {
    if (async_ && queue_) {
        queue_->flush();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (fp_) ::fflush(fp_);
}

// ── 后台日志线程 ─────────────────────────────────────────────────────────────

void Log::asyncFlushThread() {
    std::string item;
    while (queue_->pop(item)) {
        std::lock_guard<std::mutex> lock(mutex_);
        writeImpl(Level::DEBUG, item.c_str());
    }
    // 队列关闭后排空剩余（优雅退出）
    while (!queue_->empty()) {
        if (queue_->popTimeout(item, std::chrono::milliseconds(100))) {
            std::lock_guard<std::mutex> lock(mutex_);
            writeImpl(Level::DEBUG, item.c_str());
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (fp_ && fp_ != stderr) ::fflush(fp_);
}

// ── 文件管理 ─────────────────────────────────────────────────────────────────

bool Log::openFile() {
    struct tm t{};
    time_t    now = ::time(nullptr);
    ::localtime_r(&now, &t);
    today_ = t.tm_mday;

    char filename[256];
    ::snprintf(filename, sizeof(filename),
               "%s/%04d-%02d-%02d%s",
               logDir_.c_str(),
               t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
               suffix_.c_str());

    fp_ = ::fopen(filename, "a");
    return fp_ != nullptr;
}
