#pragma once

#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include "buffer/Buffer.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <atomic>
#include <string>
#include <memory>

/**
 * @brief 单个 HTTP 连接的封装
 *
 * 职责：
 *   - 持有读写 Buffer
 *   - 通过 HttpRequest 解析请求
 *   - 通过 HttpResponse 构建响应
 *   - 提供 read/write 接口供 EventLoop 调用
 *   - 通过 toClose() 告知外部是否应关闭连接
 *
 * 线程安全：
 *   - process() 在工作线程中调用（解析+构建响应）
 *   - read/write 在主线程（epoll 线程）中调用
 *   - 设计上同一时刻只有一个线程处理该连接（EPOLLONESHOT 保证）
 */
class HttpConn {
public:
    static const std::string kSrcDir;        // 静态文件根目录
    static std::atomic<int>  userCount;      // 当前连接数

    HttpConn();
    ~HttpConn();

    void init(int fd, const sockaddr_in& addr);
    void close();

    /**
     * @brief 从 fd 读数据到 readBuf_（ET 模式：循环读到 EAGAIN）
     * @return 本次读取的字节数，-1 表示错误
     */
    ssize_t read(int* savedErrno);

    /**
     * @brief 将 writeBuf_ + mmap 数据写出（writev 聚合发送）
     * @return 本次写出的字节数，-1 表示错误
     */
    ssize_t write(int* savedErrno);

    /**
     * @brief 在工作线程中调用：解析请求，构建响应
     * @return true 表示处理完毕，false 表示需要关闭连接
     */
    bool process();

    // ── 状态查询 ─────────────────────────────────────────────────────────
    int         fd()         const noexcept { return fd_; }
    sockaddr_in addr()       const noexcept { return addr_; }
    std::string peerIP()     const noexcept;
    uint16_t    peerPort()   const noexcept;
    bool        isKeepAlive()const noexcept { return request_.isKeepAlive(); }
    bool        toClose()    const noexcept { return close_; }

    /** 还有待写出的数据（header buffer + mmap） */
    bool toWrite() const noexcept {
        return (writeBuf_.readableBytes() > 0) || response_.mmapSize() > 0;
    }

    size_t toWriteBytes() const noexcept {
        return writeBuf_.readableBytes() + response_.mmapSize();
    }

private:
    int         fd_    = -1;
    bool        close_ = false;
    sockaddr_in addr_{};

    Buffer       readBuf_;    // 接收缓冲区
    Buffer       writeBuf_;   // 发送缓冲区（响应头）
    HttpRequest  request_;
    HttpResponse response_;
};
