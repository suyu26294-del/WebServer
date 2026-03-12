#include "http/HttpRequest.h"
#include "pool/SqlConnPool.h"
#include "log/Log.h"

#ifdef HAVE_MYSQL
#include <mysql/mysql.h>
#endif

#include <regex>
#include <algorithm>
#include <cassert>
#include <sstream>

// ── 静态成员 ─────────────────────────────────────────────────────────────────

const std::unordered_set<std::string> HttpRequest::kDefaultHtml{
    "/index", "/register", "/login", "/welcome", "/picture", "/video"
};

const std::unordered_set<std::string> HttpRequest::kDefaultHtmlType{
    "/register", "/login"
};

// ── 构造 & 重置 ──────────────────────────────────────────────────────────────

HttpRequest::HttpRequest() {
    reset();
}

void HttpRequest::reset() {
    state_  = ParseState::REQUEST_LINE;
    method_ = rawPath_ = path_ = version_ = body_ = "";
    headers_.clear();
    post_.clear();
}

// ── 主解析入口 ───────────────────────────────────────────────────────────────

bool HttpRequest::parse(Buffer& buf) {
    while (buf.readableBytes() > 0 && state_ != ParseState::FINISH) {
        if (state_ == ParseState::BODY) {
            if (!parseBody(buf)) return false;
            break;
        }

        // 按行解析 request-line 和 headers
        const char* lineEnd = buf.findCRLF();
        if (!lineEnd) return false;   // 数据不足，等待

        std::string line(buf.peek(), lineEnd);
        buf.retrieveUntil(lineEnd + 2);  // 跳过 \r\n

        switch (state_) {
        case ParseState::REQUEST_LINE:
            if (!parseRequestLine(line)) {
                state_ = ParseState::ERROR;
                return false;
            }
            parsePath();
            break;

        case ParseState::HEADERS:
            if (!parseHeader(line)) {
                // 空行 → headers 结束
                if (method_ == "GET" || method_ == "HEAD") {
                    state_ = ParseState::FINISH;
                } else {
                    state_ = ParseState::BODY;
                }
            }
            break;

        default:
            break;
        }
    }
    return (state_ == ParseState::FINISH);
}

// ── 请求行 ───────────────────────────────────────────────────────────────────

bool HttpRequest::parseRequestLine(const std::string& line) {
    // 格式：METHOD SP Request-URI SP HTTP-Version
    static const std::regex kPattern(
        R"(^(GET|POST|HEAD|PUT|DELETE|OPTIONS|PATCH)\s(\S+)\sHTTP/(1\.[01])$)");

    std::smatch match;
    if (!std::regex_match(line, match, kPattern)) {
        LOG_WARN("HttpRequest: bad request line: %s", line.c_str());
        return false;
    }
    method_  = match[1];
    rawPath_ = match[2];
    version_ = match[3];
    state_   = ParseState::HEADERS;
    return true;
}

void HttpRequest::parsePath() {
    path_ = urlDecode(rawPath_);
    if (path_ == "/") {
        path_ = "/index.html";
    } else if (kDefaultHtml.count(path_)) {
        path_ += ".html";
    }
}

// ── 头部 ─────────────────────────────────────────────────────────────────────

bool HttpRequest::parseHeader(const std::string& line) {
    if (line.empty()) return false;  // 空行

    auto colon = line.find(':');
    if (colon == std::string::npos) return true;  // 忽略格式错误的行

    std::string key = line.substr(0, colon);
    // trim leading whitespace in value
    size_t vs = colon + 1;
    while (vs < line.size() && line[vs] == ' ') ++vs;
    std::string val = line.substr(vs);

    headers_[key] = val;
    return true;
}

// ── 消息体 ───────────────────────────────────────────────────────────────────

bool HttpRequest::parseBody(Buffer& buf) {
    auto it = headers_.find("Content-Length");
    if (it == headers_.end()) {
        state_ = ParseState::FINISH;
        return true;
    }
    size_t contentLen = static_cast<size_t>(std::stoul(it->second));
    if (buf.readableBytes() < contentLen) return false;  // 等待更多数据

    body_.assign(buf.peek(), contentLen);
    buf.retrieve(contentLen);
    parsePostBody();
    state_ = ParseState::FINISH;
    return true;
}

void HttpRequest::parsePostBody() {
    auto ct = headers_.find("Content-Type");
    if (ct == headers_.end()) return;
    if (ct->second.find("application/x-www-form-urlencoded") == std::string::npos)
        return;
    parseUrlEncoded();
}

void HttpRequest::parseUrlEncoded() {
    // key=value&key2=value2
    std::istringstream ss(body_);
    std::string pair;
    while (std::getline(ss, pair, '&')) {
        auto eq = pair.find('=');
        if (eq == std::string::npos) continue;
        std::string key = urlDecode(pair.substr(0, eq));
        std::string val = urlDecode(pair.substr(eq + 1));
        post_[key] = val;
    }
}

// ── 工具函数 ─────────────────────────────────────────────────────────────────

std::string HttpRequest::urlDecode(const std::string& src) {
    std::string dst;
    dst.reserve(src.size());
    for (size_t i = 0; i < src.size(); ) {
        if (src[i] == '%' && i + 2 < src.size()) {
            char hex[3] = { src[i+1], src[i+2], '\0' };
            dst += static_cast<char>(std::strtol(hex, nullptr, 16));
            i += 3;
        } else if (src[i] == '+') {
            dst += ' ';
            ++i;
        } else {
            dst += src[i++];
        }
    }
    return dst;
}

// ── 访问器 ───────────────────────────────────────────────────────────────────

bool HttpRequest::isKeepAlive() const {
    auto it = headers_.find("Connection");
    if (it != headers_.end()) {
        const auto& v = it->second;
        if (v.find("keep-alive") != std::string::npos) return true;
        if (v.find("close")      != std::string::npos) return false;
    }
    return (version_ == "1.1");  // HTTP/1.1 默认 keep-alive
}

std::string HttpRequest::getHeader(const std::string& key) const {
    auto it = headers_.find(key);
    return (it != headers_.end()) ? it->second : "";
}

std::string HttpRequest::getPost(const std::string& key) const {
    auto it = post_.find(key);
    return (it != post_.end()) ? it->second : "";
}

// ── 用户校验（注册 / 登录）──────────────────────────────────────────────────

bool HttpRequest::userVerify(const std::string& name,
                              const std::string& pwd,
                              bool isLogin) {
    if (name.empty() || pwd.empty()) return false;

#ifdef HAVE_MYSQL
    SqlConnGuard guard;
    if (!guard) {
        LOG_ERROR("userVerify: failed to get DB connection");
        return false;
    }
    MYSQL* conn = *guard;

    // 使用预处理语句防止 SQL 注入
    const char* queryTpl = "SELECT password FROM user WHERE username=?";
    MYSQL_STMT* stmt = ::mysql_stmt_init(conn);
    if (!stmt) return false;

    if (::mysql_stmt_prepare(stmt, queryTpl,
                              static_cast<unsigned long>(strlen(queryTpl)))) {
        ::mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bindParam{};
    bindParam.buffer_type   = MYSQL_TYPE_STRING;
    bindParam.buffer        = const_cast<char*>(name.c_str());
    bindParam.buffer_length = static_cast<unsigned long>(name.size());

    if (::mysql_stmt_bind_param(stmt, &bindParam)) {
        ::mysql_stmt_close(stmt);
        return false;
    }
    if (::mysql_stmt_execute(stmt)) {
        ::mysql_stmt_close(stmt);
        return false;
    }

    char passBuf[256]{};
    unsigned long passLen = 0;
    MYSQL_BIND bindResult{};
    bindResult.buffer_type   = MYSQL_TYPE_STRING;
    bindResult.buffer        = passBuf;
    bindResult.buffer_length = sizeof(passBuf) - 1;
    bindResult.length        = &passLen;

    ::mysql_stmt_bind_result(stmt, &bindResult);
    bool found = (::mysql_stmt_fetch(stmt) == 0);
    ::mysql_stmt_close(stmt);

    if (isLogin) {
        if (!found) {
            LOG_WARN("userVerify: user '%s' not found", name.c_str());
            return false;
        }
        std::string storedPwd(passBuf, passLen);
        bool ok = (storedPwd == pwd);
        if (!ok) LOG_WARN("userVerify: wrong password for '%s'", name.c_str());
        return ok;
    } else {
        if (found) {
            LOG_WARN("userVerify: user '%s' already exists", name.c_str());
            return false;
        }
        const char* insertTpl =
            "INSERT INTO user(username, password) VALUES(?, ?)";
        MYSQL_STMT* ins = ::mysql_stmt_init(conn);
        if (!ins) return false;

        if (::mysql_stmt_prepare(ins, insertTpl,
                                 static_cast<unsigned long>(strlen(insertTpl)))) {
            ::mysql_stmt_close(ins);
            return false;
        }
        MYSQL_BIND bnd[2]{};
        bnd[0].buffer_type   = MYSQL_TYPE_STRING;
        bnd[0].buffer        = const_cast<char*>(name.c_str());
        bnd[0].buffer_length = static_cast<unsigned long>(name.size());
        bnd[1].buffer_type   = MYSQL_TYPE_STRING;
        bnd[1].buffer        = const_cast<char*>(pwd.c_str());
        bnd[1].buffer_length = static_cast<unsigned long>(pwd.size());

        ::mysql_stmt_bind_param(ins, bnd);
        bool ok = (::mysql_stmt_execute(ins) == 0);
        ::mysql_stmt_close(ins);
        if (ok) LOG_INFO("userVerify: registered user '%s'", name.c_str());
        return ok;
    }
#else
    // 无 MySQL：演示模式，固定账户
    if (isLogin) {
        return (name == "admin" && pwd == "admin123");
    }
    LOG_WARN("userVerify: MySQL not available, registration disabled");
    return false;
#endif
}
