#include "timer/TimerHeap.h"

#include <cassert>
#include <algorithm>

TimerHeap::TimerHeap() {
    heap_.reserve(64);
}

// ── 公有接口 ─────────────────────────────────────────────────────────────────

void TimerHeap::add(int fd, int timeoutMs, TimerCallback cb) {
    TimePoint expire = Clock::now() + Ms(timeoutMs);

    if (pos_.count(fd)) {
        // fd 已有定时器，直接更新
        size_t i        = pos_[fd];
        heap_[i].expire = expire;
        heap_[i].cb     = std::move(cb);
        heap_[i].cancelled = false;
        siftUp(i);
        siftDown(i);
    } else {
        size_t i = heap_.size();
        pos_[fd] = i;
        heap_.push_back({fd, expire, std::move(cb), false});
        siftUp(i);
    }
}

void TimerHeap::adjust(int fd, int timeoutMs) {
    if (!pos_.count(fd)) return;
    size_t i        = pos_[fd];
    heap_[i].expire = Clock::now() + Ms(timeoutMs);
    heap_[i].cancelled = false;
    // 超时刷新后 expire 变大，只需向下调整
    siftDown(i);
}

void TimerHeap::tick() {
    while (!heap_.empty()) {
        TimerNode& top = heap_.front();
        if (top.cancelled) {
            del(0);
            continue;
        }
        if (top.expire > Clock::now()) break;
        // 超时：执行回调后删除
        TimerCallback cb = std::move(top.cb);
        del(0);
        cb();
    }
}

void TimerHeap::cancel(int fd) {
    if (!pos_.count(fd)) return;
    // 惰性删除：仅打标记，tick() 或 getNextTick() 时清理
    heap_[pos_[fd]].cancelled = true;
}

int TimerHeap::getNextTick() {
    // 跳过已 cancelled 的堆顶
    while (!heap_.empty() && heap_.front().cancelled) {
        del(0);
    }
    if (heap_.empty()) return -1;

    auto diff = std::chrono::duration_cast<Ms>(
        heap_.front().expire - Clock::now());
    auto ms = diff.count();
    return static_cast<int>(ms > 0 ? ms : 0);
}

// ── 堆操作 ───────────────────────────────────────────────────────────────────

void TimerHeap::swapNode(size_t i, size_t j) {
    std::swap(heap_[i], heap_[j]);
    pos_[heap_[i].fd] = i;
    pos_[heap_[j].fd] = j;
}

void TimerHeap::siftUp(size_t i) {
    while (i > 0) {
        size_t parent = (i - 1) / 2;
        if (heap_[parent].expire <= heap_[i].expire) break;
        swapNode(parent, i);
        i = parent;
    }
}

void TimerHeap::siftDown(size_t i) {
    const size_t n = heap_.size();
    while (true) {
        size_t smallest = i;
        size_t left     = 2 * i + 1;
        size_t right    = 2 * i + 2;
        if (left  < n && heap_[left ].expire < heap_[smallest].expire) smallest = left;
        if (right < n && heap_[right].expire < heap_[smallest].expire) smallest = right;
        if (smallest == i) break;
        swapNode(i, smallest);
        i = smallest;
    }
}

void TimerHeap::del(size_t i) {
    assert(!heap_.empty() && i < heap_.size());
    size_t last = heap_.size() - 1;
    if (i < last) {
        swapNode(i, last);
        pos_.erase(heap_.back().fd);
        heap_.pop_back();
        siftUp(i);
        siftDown(i);
    } else {
        pos_.erase(heap_.back().fd);
        heap_.pop_back();
    }
}
