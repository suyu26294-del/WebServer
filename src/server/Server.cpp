#include "server/Server.h"
#include "pool/SqlConnPool.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <cerrno>
#include <cassert>
#include <cstring>
#include <stdexcept>

// ── 构造 ─────────────────────────────────────────────────────────────────────

Server::Server(uint16_t port, int threadCount, int timeoutMs,
               bool openLog, Log::Level logLevel, int logQueueSize,
               bool openSqlPool, const char* sqlHost, uint16_t sqlPort,
               const char* sqlUser, const char* sqlPwd,
               const char* dbName, int sqlPoolSize)
    : port_(port), timeoutMs_(timeoutMs),
      pool_(std::make_unique<ThreadPool>(threadCount)) {

    // ── 日志 ──────────────────────────────────────────────────────────────
    if (openLog) {
        Log::instance().init("./log", ".log", logQueueSize, logLevel);
        LOG_INFO("==================== Server Initializing ====================");
        LOG_INFO("Port: %u | Threads: %d | Timeout: %d ms",
                 port_, threadCount, timeoutMs_);
    }

    // ── 数据库连接池 ───────────────────────────────────────────────────────
    if (openSqlPool) {
        SqlConnPool::instance().init(sqlHost, sqlPort, sqlUser, sqlPwd,
                                     dbName, sqlPoolSize);
    }

    // ── 静态文件目录 ───────────────────────────────────────────────────────
    // HttpConn::kSrcDir 已在 HttpConn.cpp 中定义

    // ── 网络 ───────────────────────────────────────────────────────────────
    initSignal();
    if (!initSocket()) {
        throw std::runtime_error("Server: initSocket failed");
    }

    epollFd_ = createEpoll();
    if (epollFd_ < 0) {
        throw std::runtime_error("Server: createEpoll failed");
    }
    addFd(epollFd_, listenFd_, false, kListenEvents);

    LOG_INFO("Server initialized. Listening on 0.0.0.0:%u", port_);
}

Server::~Server() {
    if (listenFd_ >= 0) ::close(listenFd_);
    if (epollFd_  >= 0) ::close(epollFd_);
    SqlConnPool::instance().destroy();
}

// ── 事件循环 ─────────────────────────────────────────────────────────────────

void Server::run() {
    running_ = true;
    LOG_INFO("Server started. Entering event loop...");

    while (running_) {
        // 获取下一个定时器超时时间作为 epoll 超时
        int timeout = (timeoutMs_ > 0) ? timer_.getNextTick() : -1;

        int n = ::epoll_wait(epollFd_, events_, kMaxEvents, timeout);

        // 触发已超时的定时器
        timer_.tick();

        if (n < 0) {
            if (errno == EINTR) continue;   // 信号中断，正常
            LOG_ERROR("epoll_wait error: %s", strerror(errno));
            break;
        }

        for (int i = 0; i < n; ++i) {
            int      fd  = events_[i].data.fd;
            uint32_t ev  = events_[i].events;

            if (fd == listenFd_) {
                handleListen();
            } else if (ev & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                handleClose(fd);
            } else if (ev & EPOLLIN) {
                handleRead(fd);
            } else if (ev & EPOLLOUT) {
                handleWrite(fd);
            }
        }
    }
    LOG_INFO("Server event loop exited.");
}

void Server::stop() {
    running_ = false;
}

// ── 监听处理（accept 新连接）─────────────────────────────────────────────────

void Server::handleListen() {
    sockaddr_in addr{};
    socklen_t   addrLen = sizeof(addr);

    // ET 模式：循环 accept 直到 EAGAIN
    while (true) {
        int connFd = ::accept4(listenFd_,
                               reinterpret_cast<sockaddr*>(&addr),
                               &addrLen,
                               SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (connFd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            LOG_WARN("accept4 error: %s", strerror(errno));
            break;
        }

        if (static_cast<int>(conns_.size()) >= 65535) {
            sendError(connFd, "HTTP/1.1 503 Service Unavailable\r\n\r\n"
                              "Server is busy, please try again later.");
            ::close(connFd);
            LOG_WARN("Too many connections (%zu), rejected %d",
                     conns_.size(), connFd);
            continue;
        }

        auto conn = std::make_unique<HttpConn>();
        conn->init(connFd, addr);
        conns_[connFd] = std::move(conn);

        if (timeoutMs_ > 0) {
            timer_.add(connFd, timeoutMs_, [this, connFd] {
                handleClose(connFd);
            });
        }

        addFd(epollFd_, connFd, true, kConnEvents);
    }
}

// ── 可读处理（投递到线程池）──────────────────────────────────────────────────

void Server::handleRead(int fd) {
    auto it = conns_.find(fd);
    if (it == conns_.end()) return;
    HttpConn* conn = it->second.get();

    extendTimer(fd);

    // 先在主线程读数据（减少线程池内的 syscall）
    int savedErrno = 0;
    conn->read(&savedErrno);

    if (conn->toClose()) {
        handleClose(fd);
        return;
    }

    // 将 HTTP 解析 + 响应构建投递到线程池
    pool_->submit([this, conn, fd] {
        conn->process();
        onProcess(fd);
    });
}

// ── 可写处理 ─────────────────────────────────────────────────────────────────

void Server::handleWrite(int fd) {
    auto it = conns_.find(fd);
    if (it == conns_.end()) return;
    HttpConn* conn = it->second.get();

    extendTimer(fd);

    int savedErrno = 0;
    conn->write(&savedErrno);

    if (conn->toClose() || savedErrno == EBADF) {
        handleClose(fd);
        return;
    }

    if (conn->toWrite()) {
        // 还有数据没发完，继续监听 EPOLLOUT
        modFd(epollFd_, fd, true, kConnEvents | EPOLLOUT);
    } else if (conn->isKeepAlive()) {
        // 发送完毕且 keep-alive，等待下一个请求
        modFd(epollFd_, fd, true, kConnEvents);
    } else {
        handleClose(fd);
    }
}

// ── 连接关闭 ─────────────────────────────────────────────────────────────────

void Server::handleClose(int fd) {
    auto it = conns_.find(fd);
    if (it == conns_.end()) return;

    timer_.cancel(fd);
    delFd(epollFd_, fd);
    conns_.erase(it);   // unique_ptr 析构 → HttpConn::close()
}

// ── 工作线程完成回调 ──────────────────────────────────────────────────────────

void Server::onProcess(int fd) {
    auto it = conns_.find(fd);
    if (it == conns_.end()) return;
    HttpConn* conn = it->second.get();

    if (conn->toWrite()) {
        modFd(epollFd_, fd, true, kConnEvents | EPOLLOUT);
    } else if (conn->isKeepAlive()) {
        modFd(epollFd_, fd, true, kConnEvents);
    } else {
        handleClose(fd);
    }
}

// ── 定时器刷新 ───────────────────────────────────────────────────────────────

void Server::extendTimer(int fd) {
    if (timeoutMs_ > 0) {
        timer_.adjust(fd, timeoutMs_);
    }
}

// ── 错误发送 ─────────────────────────────────────────────────────────────────

void Server::sendError(int fd, const char* msg) {
    ::send(fd, msg, strlen(msg), 0);
}

// ── 初始化：Socket ───────────────────────────────────────────────────────────

bool Server::initSocket() {
    listenFd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listenFd_ < 0) {
        LOG_ERROR("socket() failed: %s", strerror(errno));
        return false;
    }

    // 端口复用（快速重启时避免 TIME_WAIT 占用端口）
    int opt = 1;
    ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    // 调整发送缓冲区
    int sndbuf = 1 << 20;  // 1 MB
    ::setsockopt(listenFd_, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port_);

    if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG_ERROR("bind() failed: %s", strerror(errno));
        return false;
    }

    if (::listen(listenFd_, SOMAXCONN) < 0) {
        LOG_ERROR("listen() failed: %s", strerror(errno));
        return false;
    }
    return true;
}

// ── 初始化：信号 ─────────────────────────────────────────────────────────────

void Server::initSignal() {
    // 忽略 SIGPIPE（客户端断开时写操作不应终止进程）
    ::signal(SIGPIPE, SIG_IGN);
}

// ── epoll 操作 ───────────────────────────────────────────────────────────────

int Server::createEpoll() {
    return ::epoll_create1(EPOLL_CLOEXEC);
}

void Server::addFd(int epollFd, int fd, bool oneShot, uint32_t events) {
    epoll_event ev{};
    ev.data.fd = fd;
    ev.events  = events;
    if (oneShot) ev.events |= EPOLLONESHOT;
    ::epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &ev);
}

void Server::modFd(int epollFd, int fd, bool oneShot, uint32_t events) {
    epoll_event ev{};
    ev.data.fd = fd;
    ev.events  = events;
    if (oneShot) ev.events |= EPOLLONESHOT;
    ::epoll_ctl(epollFd, EPOLL_CTL_MOD, fd, &ev);
}

void Server::delFd(int epollFd, int fd) {
    ::epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, nullptr);
}
