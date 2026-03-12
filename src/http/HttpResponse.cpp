#include "http/HttpResponse.h"
#include "log/Log.h"

#include <fcntl.h>
#include <unistd.h>
#include <cassert>
#include <sstream>

// ── 静态表 ───────────────────────────────────────────────────────────────────

const std::unordered_map<std::string, std::string> HttpResponse::kMimeTypes = {
    {".html",  "text/html; charset=utf-8"},
    {".htm",   "text/html; charset=utf-8"},
    {".css",   "text/css"},
    {".js",    "application/javascript"},
    {".json",  "application/json"},
    {".txt",   "text/plain"},
    {".xml",   "text/xml"},
    {".png",   "image/png"},
    {".jpg",   "image/jpeg"},
    {".jpeg",  "image/jpeg"},
    {".gif",   "image/gif"},
    {".svg",   "image/svg+xml"},
    {".ico",   "image/x-icon"},
    {".mp4",   "video/mp4"},
    {".mp3",   "audio/mpeg"},
    {".pdf",   "application/pdf"},
    {".woff",  "font/woff"},
    {".woff2", "font/woff2"},
    {".ttf",   "font/ttf"},
};

const std::unordered_map<int, std::string> HttpResponse::kStatusText = {
    {200, "OK"},
    {201, "Created"},
    {301, "Moved Permanently"},
    {302, "Found"},
    {304, "Not Modified"},
    {400, "Bad Request"},
    {403, "Forbidden"},
    {404, "Not Found"},
    {405, "Method Not Allowed"},
    {408, "Request Timeout"},
    {500, "Internal Server Error"},
    {503, "Service Unavailable"},
};

// ── 构造 / 析构 ──────────────────────────────────────────────────────────────

HttpResponse::HttpResponse() = default;

HttpResponse::~HttpResponse() {
    unmapFile();
}

void HttpResponse::unmapFile() {
    if (mmapData_) {
        ::munmap(mmapData_, mmapSize_);
        mmapData_ = nullptr;
        mmapSize_ = 0;
    }
}

// ── 主入口 ───────────────────────────────────────────────────────────────────

void HttpResponse::makeResponse(int code, const std::string& path,
                                 const std::string& srcDir, bool keepAlive,
                                 Buffer& header, Buffer& body) {
    code_ = code;
    unmapFile();

    if (code_ != 200) {
        // 错误响应：生成内联 HTML
        std::string msg;
        auto it = kStatusText.find(code_);
        msg = (it != kStatusText.end()) ? it->second : "Unknown Error";
        addStateLine(header);
        addErrorBody(header, body, msg);
        addHeaders(header, body.readableBytes(), keepAlive, "text/html");
        return;
    }

    std::string fullPath = srcDir + path;
    struct stat st{};
    if (::stat(fullPath.c_str(), &st) < 0 || S_ISDIR(st.st_mode)) {
        code_ = 404;
        fullPath = srcDir + "/404.html";
        ::stat(fullPath.c_str(), &st);
    } else if (!(st.st_mode & S_IROTH)) {
        code_ = 403;
        fullPath = srcDir + "/403.html";
        ::stat(fullPath.c_str(), &st);
    }

    addStateLine(header);
    std::string mime = getMimeType(fullPath);

    if (mapFile(fullPath)) {
        addHeaders(header, static_cast<size_t>(st.st_size), keepAlive, mime);
        // body 此时为空；调用者通过 mmapData_/mmapSize_ 发送文件数据
    } else {
        // 降级为读入 buffer
        code_ = 500;
        addErrorBody(header, body, "Internal Server Error");
        addHeaders(header, body.readableBytes(), keepAlive, "text/html");
    }
}

// ── 私有 ─────────────────────────────────────────────────────────────────────

void HttpResponse::addStateLine(Buffer& header) {
    std::string status;
    auto it = kStatusText.find(code_);
    status = (it != kStatusText.end()) ? it->second : "Unknown";

    header.append("HTTP/1.1 " + std::to_string(code_) + " " + status + "\r\n");
}

void HttpResponse::addHeaders(Buffer& header, size_t contentLen, bool keepAlive,
                               const std::string& contentType) {
    header.append("Content-Type: "   + contentType                    + "\r\n");
    header.append("Content-Length: " + std::to_string(contentLen)     + "\r\n");
    header.append("Connection: ");
    header.append(keepAlive ? "keep-alive" : "close");
    header.append("\r\n");
    if (keepAlive) {
        header.append("Keep-Alive: timeout=60, max=100\r\n");
    }
    header.append("Server: CppWebServer/1.0\r\n");
    header.append("\r\n");
}

bool HttpResponse::mapFile(const std::string& filePath) {
    int fd = ::open(filePath.c_str(), O_RDONLY);
    if (fd < 0) {
        LOG_ERROR("HttpResponse: open file failed: %s", filePath.c_str());
        return false;
    }

    struct stat st{};
    if (::fstat(fd, &st) < 0) {
        ::close(fd);
        return false;
    }

    void* ptr = ::mmap(nullptr, static_cast<size_t>(st.st_size),
                       PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);

    if (ptr == MAP_FAILED) {
        LOG_ERROR("HttpResponse: mmap failed: %s", filePath.c_str());
        return false;
    }

    mmapData_ = static_cast<char*>(ptr);
    mmapSize_ = static_cast<size_t>(st.st_size);
    return true;
}

void HttpResponse::addErrorBody(Buffer& header, Buffer& body,
                                 const std::string& msg) {
    std::string html =
        "<!DOCTYPE html><html><head>"
        "<meta charset=\"utf-8\">"
        "<title>" + std::to_string(code_) + " " + msg + "</title>"
        "</head><body>"
        "<h1>" + std::to_string(code_) + " " + msg + "</h1>"
        "<hr><p>CppWebServer</p>"
        "</body></html>";
    body.append(html);
}

std::string HttpResponse::getMimeType(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = path.substr(dot);
    auto it = kMimeTypes.find(ext);
    return (it != kMimeTypes.end()) ? it->second : "application/octet-stream";
}
