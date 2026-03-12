#pragma once

#include <vector>
#include <string>
#include <cassert>
#include <sys/uio.h>
#include <unistd.h>

/**
 * @brief 可增长的读写缓冲区，支持分散读（readv）以减少系统调用次数
 *
 * 布局:  [已读空间 | 可读数据 | 可写空间]
 *         ^readPos_  ^writePos_  ^buffer_.end()
 */
class Buffer {
public:
    static constexpr size_t kInitialSize = 1024;

    explicit Buffer(size_t initSize = kInitialSize);

    // ── 容量查询 ────────────────────────────────────────────────────────────
    size_t readableBytes()   const noexcept { return writePos_ - readPos_; }
    size_t writableBytes()   const noexcept { return buffer_.size() - writePos_; }
    size_t prependableBytes()const noexcept { return readPos_; }
    bool   empty()           const noexcept { return readableBytes() == 0; }

    // ── 读指针 ──────────────────────────────────────────────────────────────
    const char* peek()       const noexcept { return data() + readPos_; }
    const char* beginWrite() const noexcept { return data() + writePos_; }
    char*       beginWrite()       noexcept { return data() + writePos_; }

    // ── 消费数据 ────────────────────────────────────────────────────────────
    void retrieve(size_t len);
    void retrieveUntil(const char* end);
    void retrieveAll();
    std::string retrieveAllAsStr();
    std::string retrieveAsStr(size_t len);

    // ── 写入数据 ────────────────────────────────────────────────────────────
    void ensureWritable(size_t len);
    void hasWritten(size_t len) noexcept { writePos_ += len; }

    void append(const char* data, size_t len);
    void append(const std::string& str);
    void append(const void* data, size_t len);
    void append(const Buffer& buf);

    // ── 搜索 ────────────────────────────────────────────────────────────────
    const char* findCRLF() const noexcept;
    const char* findCRLF(const char* start) const noexcept;

    // ── fd I/O（配合 ET 非阻塞使用）───────────────────────────────────────
    ssize_t readFd(int fd, int* savedErrno);
    ssize_t writeFd(int fd, int* savedErrno);

private:
    char*       data()       noexcept { return buffer_.data(); }
    const char* data() const noexcept { return buffer_.data(); }
    void        makeSpace(size_t len);

    std::vector<char> buffer_;
    size_t            readPos_  = 0;
    size_t            writePos_ = 0;

    static const char kCRLF[];
};
