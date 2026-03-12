#pragma once

#include <functional>
#include <chrono>
#include <vector>
#include <unordered_map>

using TimerCallback = std::function<void()>;
using Clock         = std::chrono::steady_clock;
using TimePoint     = std::chrono::time_point<Clock>;
using Ms            = std::chrono::milliseconds;

/**
 * @brief 基于小根堆的定时器管理器
 *
 * 设计要点：
 *   - O(log n) 插入、更新、删除
 *   - 每个连接对应一个 fd，通过 fd 索引到堆节点
 *   - 支持 adjust（刷新超时，用于 keep-alive 连接活跃时重置）
 *   - 惰性删除：节点被 cancel 时打标记，tick() 时跳过
 *   - getNextTick() 返回距下一个超时的毫秒数，用于 epoll_wait 的 timeout 参数
 */
class TimerHeap {
public:
    TimerHeap();
    ~TimerHeap() = default;

    /**
     * @brief 添加或更新定时器
     * @param fd        连接文件描述符（作为定时器唯一 id）
     * @param timeoutMs 超时时长（毫秒）
     * @param cb        超时回调（通常是关闭连接的函数）
     */
    void add(int fd, int timeoutMs, TimerCallback cb);

    /**
     * @brief 刷新定时器（keep-alive 收到数据后调用）
     * @param fd        连接文件描述符
     * @param timeoutMs 新的超时时长
     */
    void adjust(int fd, int timeoutMs);

    /**
     * @brief 触发所有已超时的定时器回调
     */
    void tick();

    /**
     * @brief 主动取消并删除定时器（连接正常关闭时调用）
     */
    void cancel(int fd);

    /**
     * @brief 返回距下一个超时事件的毫秒数；-1 表示堆为空（永久等待）
     */
    int getNextTick();

private:
    struct TimerNode {
        int          fd;
        TimePoint    expire;
        TimerCallback cb;
        bool         cancelled = false;  // 惰性删除标记
    };

    void siftUp(size_t i);
    void siftDown(size_t i);
    void swapNode(size_t i, size_t j);
    void del(size_t i);  // 从堆中真正删除节点

    std::vector<TimerNode>        heap_;
    std::unordered_map<int, size_t> pos_;  // fd -> 堆索引
};
