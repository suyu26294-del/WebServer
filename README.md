# ⚡ CppWebServer

> 基于 C++17 + Linux epoll + Reactor 模型的高性能 HTTP 服务器

---

## 项目简介

这是一个高性能 Linux C++ WebServer。

**核心技术栈：**

- **并发模型**：单 Reactor（主线程 epoll 事件分发）+ 固定线程池（工作线程处理业务）
- **I/O 模式**：EPOLLET（边缘触发）+ EPOLLONESHOT + 非阻塞 Socket，循环读写至 EAGAIN
- **HTTP 解析**：有限状态机（REQUEST_LINE → HEADERS → BODY → FINISH），正确处理半包/粘包
- **连接管理**：小根堆定时器超时回收 + keep-alive 支持
- **日志系统**：异步日志（生产者-消费者 BlockQueue），后台线程批量落盘，按天切分
- **数据库**：MySQL 连接池（RAII 守卫防止连接泄漏，预处理语句防止 SQL 注入）
- **静态文件**：mmap + writev 聚合发送，减少内核态/用户态数据拷贝

---

## 目录结构

```
WebServer/
├── CMakeLists.txt
├── README.md
├── conf/
│   └── server.conf          # 服务器配置文件
├── include/
│   ├── buffer/Buffer.h      # 可增长读写缓冲区
│   ├── log/
│   │   ├── BlockQueue.h     # 有界阻塞队列（模板）
│   │   └── Log.h            # 异步日志系统
│   ├── timer/TimerHeap.h    # 小根堆定时器
│   ├── pool/
│   │   ├── ThreadPool.h     # 固定大小线程池（header-only）
│   │   └── SqlConnPool.h    # MySQL 连接池
│   ├── http/
│   │   ├── HttpRequest.h    # HTTP 请求解析（状态机）
│   │   ├── HttpResponse.h   # HTTP 响应构建
│   │   └── HttpConn.h       # 单连接封装
│   ├── server/Server.h      # 服务器主类（Reactor 核心）
│   └── util/Config.h        # 配置文件读取器
├── src/                     # 对应实现文件
├── tests/                   # 单元测试
├── www/                     # 静态文件根目录
├── sql/init.sql             # 数据库初始化脚本
└── scripts/
    ├── build.sh             # 构建脚本
    └── benchmark.sh         # 压测脚本
```

---

## 快速开始

### 依赖

```bash
# Ubuntu/Debian
sudo apt install build-essential cmake libmysqlclient-dev
```

### 编译

```bash
# Release 构建
./scripts/build.sh

# Debug 构建（含 AddressSanitizer）
./scripts/build.sh debug
```

或手动：

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 运行

```bash
# 使用默认配置
./build/webserver

# 指定配置文件
./build/webserver conf/server.conf
```

### 数据库（可选）

```bash
# 初始化数据库
mysql -u root -p < sql/init.sql

# 修改配置文件
vim conf/server.conf
# 设置 sql_open = true 并填入数据库连接信息
```

---

## 配置说明

```ini
port         = 9006       # 监听端口
thread_num   = 8          # 工作线程数（默认 = CPU 核数）
timeout_ms   = 60000      # 连接超时（ms），0 = 不超时

log_open       = true     # 是否开启日志
log_level      = 1        # 0=DEBUG 1=INFO 2=WARN 3=ERROR
log_queue_size = 1024     # 异步队列大小，0 = 同步日志

sql_open      = false     # 是否启用 MySQL
sql_host      = 127.0.0.1
sql_port      = 3306
sql_user      = root
sql_pwd       = password
sql_db        = webserver
sql_pool_size = 10
```

---

## 单元测试

```bash
# 编译并运行测试
cd build && make test_buffer test_timer test_httprequest
./test_buffer
./test_timer
./test_httprequest
```

---

## 压测

```bash
# 需要先安装 wrk 或 ab
./scripts/benchmark.sh 127.0.0.1 9006

# 手动 wrk 压测
wrk -t4 -c1000 -d30s --latency http://127.0.0.1:9006/index.html
```

---

## 技术亮点

### 1. Reactor + EPOLLONESHOT 保证线程安全

使用 `EPOLLONESHOT` 确保同一连接同时只有一个线程在处理，避免竞态条件，无需对 `HttpConn` 加锁。

### 2. ET 模式 + 循环读写

边缘触发模式下，数据就绪只通知一次，必须循环读到 `EAGAIN`。结合非阻塞 fd，实现零等待的高效 I/O。

### 3. 小根堆定时器 + 惰性删除

`TimerHeap` 采用惰性删除（`cancel` 只打标记，`tick()` 时跳过），避免频繁堆调整。`getNextTick()` 返回值直接用于 `epoll_wait` 的 timeout 参数，实现精确超时。

### 4. 异步日志降低主路径延迟

前台线程只 `push` 到 `BlockQueue<string>`（无 I/O），后台线程批量 `fwrite`，大幅减少日志对请求处理的阻塞影响。

### 5. mmap + writev 静态文件零拷贝

静态文件通过 `mmap` 映射到用户空间，配合 `writev` 将响应头和文件数据聚合为一次系统调用发出，减少数据拷贝次数。

### 6. 连接池避免频繁建连

MySQL 连接建立涉及 TCP 握手 + 认证，耗时约 1-5ms。连接池预建并复用连接，高并发下避免这一开销成为瓶颈。

---



---


---


