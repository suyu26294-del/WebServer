#pragma once

#include "buffer/Buffer.h"

#include <string>
#include <unordered_map>
#include <sys/mman.h>
#include <sys/stat.h>

/**
 * @brief HTTP 响应构建器
 *
 * 支持：
 *   - 静态文件响应（mmap + writev 零拷贝）
 *   - 动态文本/JSON 响应
 *   - 错误页面
 *   - Content-Type 自动推断
 *   - 持久连接头
 */
class HttpResponse {
public:
    HttpResponse();
    ~HttpResponse();

    // 禁止拷贝（含 mmap 指针）
    HttpResponse(const HttpResponse&)            = delete;
    HttpResponse& operator=(const HttpResponse&) = delete;

    /**
     * @brief 准备响应
     * @param code      HTTP 状态码
     * @param path      文件路径（静态资源）
     * @param srcDir    静态文件根目录
     * @param keepAlive 是否 keep-alive
     * @param header    响应头 Buffer（写出状态行+头）
     * @param body      响应体 Buffer（写出 body 或指向 mmap）
     */
    void makeResponse(int code, const std::string& path,
                      const std::string& srcDir, bool keepAlive,
                      Buffer& header, Buffer& body);

    /**
     * @brief 对应静态文件的 mmap 指针（如果有）
     *        调用者应在发送后 munmap，或由本对象析构时 munmap
     */
    char*  mmapData()  const noexcept { return mmapData_; }
    size_t mmapSize()  const noexcept { return mmapSize_; }

    void unmapFile();   // 主动释放 mmap

    int statusCode() const noexcept { return code_; }

private:
    void addStateLine(Buffer& header);
    void addHeaders(Buffer& header, size_t contentLen, bool keepAlive,
                    const std::string& contentType);
    bool mapFile(const std::string& filePath);
    void addErrorBody(Buffer& header, Buffer& body, const std::string& msg);

    static const std::unordered_map<std::string, std::string> kMimeTypes;
    static const std::unordered_map<int, std::string>         kStatusText;

    static std::string getMimeType(const std::string& path);

    int    code_      = -1;
    char*  mmapData_  = nullptr;
    size_t mmapSize_  = 0;
};
