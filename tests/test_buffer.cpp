#include "buffer/Buffer.h"
#include <cassert>
#include <cstring>
#include <iostream>

static int passed = 0;
static int failed = 0;

#define CHECK(expr) do { \
    if (expr) { ++passed; } \
    else { ++failed; std::cerr << "FAIL: " #expr " at line " << __LINE__ << "\n"; } \
} while(0)

void testBasicAppendRetrieve() {
    Buffer buf;
    buf.append("Hello, World!", 13);
    CHECK(buf.readableBytes() == 13);
    CHECK(std::string(buf.peek(), 5) == "Hello");
    buf.retrieve(7);
    CHECK(buf.readableBytes() == 6);
    std::string rest = buf.retrieveAllAsStr();
    CHECK(rest == "World!");
    CHECK(buf.empty());
}

void testGrowth() {
    Buffer buf(8);
    std::string data(1024, 'A');
    buf.append(data.data(), data.size());
    CHECK(buf.readableBytes() == 1024);
    buf.retrieveAll();
    CHECK(buf.empty());
}

void testFindCRLF() {
    Buffer buf;
    buf.append("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    const char* crlf = buf.findCRLF();
    CHECK(crlf != nullptr);
    std::string line(buf.peek(), crlf);
    CHECK(line == "GET / HTTP/1.1");
}

void testMultilineConsume() {
    Buffer buf;
    buf.append("Line1\r\nLine2\r\nLine3\r\n");
    auto consumeLine = [&]() -> std::string {
        const char* crlf = buf.findCRLF();
        if (!crlf) return "";
        std::string line(buf.peek(), crlf);
        buf.retrieveUntil(crlf + 2);
        return line;
    };
    CHECK(consumeLine() == "Line1");
    CHECK(consumeLine() == "Line2");
    CHECK(consumeLine() == "Line3");
    CHECK(buf.empty());
}

int main() {
    testBasicAppendRetrieve();
    testGrowth();
    testFindCRLF();
    testMultilineConsume();

    std::cout << "Buffer tests: " << passed << " passed, "
              << failed << " failed.\n";
    return (failed == 0) ? 0 : 1;
}
