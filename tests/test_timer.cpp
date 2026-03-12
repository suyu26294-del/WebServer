#include "timer/TimerHeap.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

static int passed = 0;
static int failed = 0;

#define CHECK(expr) do { \
    if (expr) { ++passed; } \
    else { ++failed; std::cerr << "FAIL: " #expr " at line " << __LINE__ << "\n"; } \
} while(0)

void testBasicExpiry() {
    TimerHeap heap;
    std::vector<int> fired;

    heap.add(1, 50,  [&] { fired.push_back(1); });
    heap.add(2, 100, [&] { fired.push_back(2); });
    heap.add(3, 200, [&] { fired.push_back(3); });

    std::this_thread::sleep_for(std::chrono::milliseconds(130));
    heap.tick();

    CHECK(fired.size() == 2);
    CHECK(fired[0] == 1);
    CHECK(fired[1] == 2);
}

void testCancel() {
    TimerHeap heap;
    bool fired = false;
    heap.add(10, 50, [&] { fired = true; });
    heap.cancel(10);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    heap.tick();
    CHECK(!fired);
}

void testAdjust() {
    TimerHeap heap;
    bool fired = false;
    heap.add(5, 80, [&] { fired = true; });
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    heap.adjust(5, 80);  // 刷新
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    heap.tick();
    CHECK(!fired);  // 还没到期
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    heap.tick();
    CHECK(fired);
}

void testGetNextTick() {
    TimerHeap heap;
    CHECK(heap.getNextTick() == -1);  // 空堆
    heap.add(99, 500, [] {});
    int ms = heap.getNextTick();
    CHECK(ms > 0 && ms <= 500);
}

int main() {
    testBasicExpiry();
    testCancel();
    testAdjust();
    testGetNextTick();

    std::cout << "Timer tests: " << passed << " passed, "
              << failed << " failed.\n";
    return (failed == 0) ? 0 : 1;
}
