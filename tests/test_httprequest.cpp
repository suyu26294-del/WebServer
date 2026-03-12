#include "http/HttpRequest.h"
#include "buffer/Buffer.h"
#include <cassert>
#include <iostream>

static int passed = 0;
static int failed = 0;

#define CHECK(expr) do { \
    if (expr) { ++passed; } \
    else { ++failed; std::cerr << "FAIL: " #expr " at line " << __LINE__ << "\n"; } \
} while(0)

void testGetRequest() {
    HttpRequest req;
    Buffer buf;
    buf.append("GET /index.html HTTP/1.1\r\n"
               "Host: localhost:9006\r\n"
               "Connection: keep-alive\r\n"
               "\r\n");
    bool ok = req.parse(buf);
    CHECK(ok);
    CHECK(req.method()  == "GET");
    CHECK(req.path()    == "/index.html");
    CHECK(req.version() == "1.1");
    CHECK(req.isKeepAlive());
}

void testPostRequest() {
    HttpRequest req;
    Buffer buf;
    std::string body = "username=alice&password=secret123";
    std::string raw =
        "POST /login.html HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" + body;
    buf.append(raw);
    bool ok = req.parse(buf);
    CHECK(ok);
    CHECK(req.method() == "POST");
    CHECK(req.getPost("username") == "alice");
    CHECK(req.getPost("password") == "secret123");
    CHECK(!req.isKeepAlive());
}

void testHalfPacket() {
    // 分两次送达
    HttpRequest req;
    Buffer buf;
    buf.append("GET /hello HTTP/1.1\r\n");  // 不完整
    bool ok1 = req.parse(buf);
    CHECK(!ok1);  // 数据不足

    buf.append("Host: localhost\r\n\r\n");
    bool ok2 = req.parse(buf);
    CHECK(ok2);
    CHECK(req.path() == "/hello");
}

void testBadRequest() {
    HttpRequest req;
    Buffer buf;
    buf.append("INVALID_METHOD /path HTTP/1.1\r\n\r\n");
    bool ok = req.parse(buf);
    CHECK(!ok);
}

void testUrlDecode() {
    HttpRequest req;
    Buffer buf;
    buf.append("GET /hello%20world HTTP/1.1\r\n\r\n");
    bool ok = req.parse(buf);
    CHECK(ok);
    CHECK(req.path() == "/hello world");
}

int main() {
    testGetRequest();
    testPostRequest();
    testHalfPacket();
    testBadRequest();
    testUrlDecode();

    std::cout << "HttpRequest tests: " << passed << " passed, "
              << failed << " failed.\n";
    return (failed == 0) ? 0 : 1;
}
