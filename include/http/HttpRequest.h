#pragma once

#include "buffer/Buffer.h"

#include <string>
#include <unordered_map>
#include <unordered_set>

/**
 * @brief HTTP/1.1 请求解析器（有限状态机）
 *
 * 状态转移：
 *   REQUEST_LINE  →  HEADERS  →  BODY  →  FINISH
 *               ↘ ERROR  (任意状态遇到非法格式)
 *
 * 支持：
 *   - GET / POST
 *   - keep-alive（Connection: keep-alive / HTTP/1.1 默认）
 *   - URL 解码（%xx 与 '+' → 空格）
 *   - 半包：parse() 返回 false 时表示数据不足，等待更多数据
 */
class HttpRequest {
public:
    enum class ParseState {
        REQUEST_LINE,
        HEADERS,
        BODY,
        FINISH,
        ERROR,
    };

    enum class HttpCode {
        NO_REQUEST    = 0,   // 数据不足，继续等待
        GET_REQUEST,         // 完整请求，可处理
        BAD_REQUEST,         // 格式错误
        FORBIDDEN_REQUEST,   // 权限不足
        NO_RESOURCE,         // 资源不存在
        FILE_REQUEST,        // 静态资源请求
        INTERNAL_ERROR,      // 内部错误
    };

    HttpRequest();
    void reset();

    /**
     * @brief 从 Buffer 中解析请求
     * @return true 表示解析完成（FINISH），false 表示数据不足或出错
     */
    bool parse(Buffer& buf);

    // ── 访问器 ────────────────────────────────────────────────────────────
    const std::string& method()  const noexcept { return method_; }
    const std::string& path()    const noexcept { return path_; }
    const std::string& version() const noexcept { return version_; }
    const std::string& body()    const noexcept { return body_; }

    bool isKeepAlive() const;
    bool isFinished()  const noexcept { return state_ == ParseState::FINISH; }
    bool isError()     const noexcept { return state_ == ParseState::ERROR;  }

    std::string getHeader(const std::string& key) const;
    std::string getPost(const std::string& key)   const;

    // 用于业务层：注册 / 登录校验
    bool userVerify(const std::string& name,
                    const std::string& pwd,
                    bool isLogin);

private:
    bool parseRequestLine(const std::string& line);
    bool parseHeader(const std::string& line);
    bool parseBody(Buffer& buf);

    static std::string urlDecode(const std::string& src);
    void parsePath();
    void parsePostBody();
    void parseUrlEncoded();

    ParseState state_;
    std::string method_, rawPath_, path_, version_, body_;
    std::unordered_map<std::string, std::string> headers_;
    std::unordered_map<std::string, std::string> post_;

    static const std::unordered_set<std::string> kDefaultHtml;
    static const std::unordered_set<std::string> kDefaultHtmlType;
};
