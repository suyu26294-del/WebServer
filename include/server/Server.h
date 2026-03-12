#pragma once

#include "http/HttpConn.h"
#include "timer/TimerHeap.h"
#include "pool/ThreadPool.h"
#include "log/Log.h"

#include <sys/epoll.h>
#include <netinet/in.h>
#include <unordered_map>
#include <memory>
#include <functional>
#include <atomic>

/**
 * @brief 服务器核心：单 Reactor + 线程池模型
 *
 * 架构：
 *   主线程  →  epoll_wait  →  事件分发
 *                           ├─ 新连接：accept → 注册到 epoll
 *                           ├─ 可读：投递到线程池（read+parse+build）
 *                           └─ 可写：writev 发送响应
 *
 * 关键设计：
 *   - EPOLLONESHOT：保证同一连接同时只被一个线程处理
 *   - ET 模式：减少事件通知次数，要求循环读写到 EAGAIN
 *   - 定时器：保护空闲连接，防止 fd 泄漏
 *   - 信号处理：SIGPIPE 设为 SIG_IGN，SIGINT/SIGTERM 优雅退出
 */
class Server {
public:
    Server(uint16_t port, int threadCount, int timeoutMs,
           bool openLog, Log::Level logLevel, int logQueueSize,
           bool openSqlPool, const char* sqlHost, uint16_t sqlPort,
           const char* sqlUser, const char* sqlPwd,
           const char* dbName, int sqlPoolSize);

    ~Server();

    void run();   // 进入事件循环（阻塞）
    void stop();  // 请求停止（可从信号处理器调用）

private:
    // ── 初始化 ──────────────────────────────────────────────────────────────
    bool initSocket();
    void initSignal();

    // ── epoll 操作 ───────────────────────────────────────────────────────────
    static int  createEpoll();
    static void addFd(int epollFd, int fd, bool oneShot, uint32_t events);
    static void modFd(int epollFd, int fd, bool oneShot, uint32_t events);
    static void delFd(int epollFd, int fd);

    // ── 事件处理 ─────────────────────────────────────────────────────────────
    void handleListen();
    void handleRead(int fd);
    void handleWrite(int fd);
    void handleClose(int fd);
    void onProcess(int fd);           // 工作线程回调结束后的重新注册

    // ── 连接管理 ─────────────────────────────────────────────────────────────
    void extendTimer(int fd);
    void closeConn(HttpConn* conn);
    void sendError(int fd, const char* msg);

    // ── 配置 ─────────────────────────────────────────────────────────────────
    uint16_t    port_;
    int         timeoutMs_;           // 连接超时（ms）
    bool        running_  = false;

    // ── 网络 ─────────────────────────────────────────────────────────────────
    int         listenFd_  = -1;
    int         epollFd_   = -1;
    static constexpr int kMaxEvents = 1024;
    epoll_event events_[kMaxEvents];

    // ── 连接表（fd → HttpConn）───────────────────────────────────────────────
    std::unordered_map<int, std::unique_ptr<HttpConn>> conns_;

    // ── 组件 ─────────────────────────────────────────────────────────────────
    TimerHeap                timer_;
    std::unique_ptr<ThreadPool> pool_;

    // ── epoll 事件标志 ───────────────────────────────────────────────────────
    static constexpr uint32_t kListenEvents = EPOLLIN | EPOLLET;
    static constexpr uint32_t kConnEvents   = EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
};
