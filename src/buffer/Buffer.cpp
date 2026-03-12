#include "buffer/Buffer.h"

#include <cstring>
#include <stdexcept>
#include <algorithm>

Buffer::Buffer(size_t initSize)
    : buffer_(initSize, '\0'), readPos_(0), writePos_(0) {}

// ── 消费 ─────────────────────────────────────────────────────────────────────

void Buffer::retrieve(size_t len) {
    assert(len <= readableBytes());
    readPos_ += len;
}

void Buffer::retrieveUntil(const char* end) {
    assert(peek() <= end && end <= beginWrite());
    retrieve(static_cast<size_t>(end - peek()));
}

void Buffer::retrieveAll() {
    readPos_  = 0;
    writePos_ = 0;
}

std::string Buffer::retrieveAllAsStr() {
    std::string str(peek(), readableBytes());
    retrieveAll();
    return str;
}

std::string Buffer::retrieveAsStr(size_t len) {
    assert(len <= readableBytes());
    std::string str(peek(), len);
    retrieve(len);
    return str;
}

// ── 写入 ─────────────────────────────────────────────────────────────────────

void Buffer::ensureWritable(size_t len) {
    if (writableBytes() < len) {
        makeSpace(len);
    }
}

void Buffer::append(const char* d, size_t len) {
    ensureWritable(len);
    std::copy(d, d + len, beginWrite());
    hasWritten(len);
}

void Buffer::append(const void* d, size_t len) {
    append(static_cast<const char*>(d), len);
}

void Buffer::append(const std::string& str) {
    append(str.data(), str.size());
}

void Buffer::append(const Buffer& buf) {
    append(buf.peek(), buf.readableBytes());
}

// ── 搜索 ─────────────────────────────────────────────────────────────────────

const char* Buffer::findCRLF() const noexcept {
    return findCRLF(peek());
}

const char* Buffer::findCRLF(const char* start) const noexcept {
    assert(peek() <= start && start <= beginWrite());
    const char* crlf = std::search(start, beginWrite(),
                                   kCRLF, kCRLF + 2);
    return (crlf == beginWrite()) ? nullptr : crlf;
}

// ── fd I/O ───────────────────────────────────────────────────────────────────

/**
 * @brief 使用 readv 进行分散读。
 *        先尝试填满 buffer_ 剩余空间，溢出部分写入栈上 extrabuf，
 *        然后再 append 进 buffer_，避免不必要扩容。
 */
ssize_t Buffer::readFd(int fd, int* savedErrno) {
    char     extrabuf[65536];
    iovec    vec[2];
    const size_t writable = writableBytes();

    vec[0].iov_base = beginWrite();
    vec[0].iov_len  = writable;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len  = sizeof(extrabuf);

    const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;
    const ssize_t n  = ::readv(fd, vec, iovcnt);

    if (n < 0) {
        *savedErrno = errno;
    } else if (static_cast<size_t>(n) <= writable) {
        writePos_ += static_cast<size_t>(n);
    } else {
        writePos_ = buffer_.size();
        append(extrabuf, static_cast<size_t>(n) - writable);
    }
    return n;
}

ssize_t Buffer::writeFd(int fd, int* savedErrno) {
    const ssize_t n = ::write(fd, peek(), readableBytes());
    if (n < 0) {
        *savedErrno = errno;
    } else {
        retrieve(static_cast<size_t>(n));
    }
    return n;
}

// ── 私有 ─────────────────────────────────────────────────────────────────────

void Buffer::makeSpace(size_t len) {
    if (writableBytes() + prependableBytes() < len) {
        // 真正需要扩容
        buffer_.resize(writePos_ + len);
    } else {
        // 将可读数据左移，复用前部已消费空间
        const size_t readable = readableBytes();
        std::copy(data() + readPos_, data() + writePos_, data());
        readPos_  = 0;
        writePos_ = readable;
    }
}

// 定义 CRLF 字符串（供搜索用）
const char Buffer::kCRLF[] = "\r\n";
