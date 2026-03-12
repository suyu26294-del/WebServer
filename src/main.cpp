#include "server/Server.h"
#include "util/Config.h"
#include "log/Log.h"

#include <csignal>
#include <iostream>
#include <stdexcept>

// 全局服务器指针（用于信号处理）
static Server* gServer = nullptr;

static void signalHandler(int sig) {
    if (gServer) gServer->stop();
}

int main(int argc, char* argv[]) {
    // ── 加载配置文件 ─────────────────────────────────────────────────────────
    Config conf;
    std::string confPath = (argc > 1) ? argv[1] : "./conf/server.conf";
    if (!conf.load(confPath)) {
        std::cerr << "[WARN] Cannot load config: " << confPath
                  << ", using defaults.\n";
    }

    // ── 读取配置项 ───────────────────────────────────────────────────────────
    uint16_t port        = static_cast<uint16_t>(conf.getInt("port",        9006));
    int      threads     = conf.getInt("thread_num",    std::thread::hardware_concurrency());
    int      timeoutMs   = conf.getInt("timeout_ms",    60000);
    bool     openLog     = conf.getBool("log_open",     true);
    int      logQueue    = conf.getInt("log_queue_size",1024);
    Log::Level logLevel  = static_cast<Log::Level>(
                               conf.getInt("log_level", static_cast<int>(Log::Level::INFO)));

    bool     openSql     = conf.getBool("sql_open",     false);
    std::string sqlHost  = conf.getString("sql_host",   "127.0.0.1");
    uint16_t sqlPort     = static_cast<uint16_t>(conf.getInt("sql_port", 3306));
    std::string sqlUser  = conf.getString("sql_user",   "root");
    std::string sqlPwd   = conf.getString("sql_pwd",    "");
    std::string dbName   = conf.getString("sql_db",     "webserver");
    int      sqlPool     = conf.getInt("sql_pool_size", 10);

    // ── 注册优雅退出信号 ─────────────────────────────────────────────────────
    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    // ── 启动服务器 ───────────────────────────────────────────────────────────
    try {
        Server server(
            port, threads, timeoutMs,
            openLog, logLevel, logQueue,
            openSql, sqlHost.c_str(), sqlPort,
            sqlUser.c_str(), sqlPwd.c_str(),
            dbName.c_str(), sqlPool
        );
        gServer = &server;
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << "\n";
        return 1;
    }

    std::cout << "Server stopped gracefully.\n";
    return 0;
}
