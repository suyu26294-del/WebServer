#pragma once

#include <deque>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cassert>

/**
 * @brief 有界阻塞队列（生产者-消费者）
 *
 * 用于异步日志系统：前台线程 push 日志项，
 * 后台日志线程 pop 并批量落盘。
 * 队列满时 push 阻塞；队列空时 pop 阻塞。
 */
template <typename T>
class BlockQueue {
public:
    explicit BlockQueue(size_t capacity = 1024)
        : capacity_(capacity), closed_(false) {
        assert(capacity > 0);
    }

    ~BlockQueue() { close(); }

    // 禁止拷贝
    BlockQueue(const BlockQueue&)            = delete;
    BlockQueue& operator=(const BlockQueue&) = delete;

    // ── 写端 ─────────────────────────────────────────────────────────────────

    /**
     * @brief 推入元素；若队列已满则阻塞等待
     * @return false 表示队列已关闭
     */
    bool push(const T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        notFull_.wait(lock, [this] {
            return queue_.size() < capacity_ || closed_;
        });
        if (closed_) return false;
        queue_.push_back(item);
        notEmpty_.notify_one();
        return true;
    }

    bool push(T&& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        notFull_.wait(lock, [this] {
            return queue_.size() < capacity_ || closed_;
        });
        if (closed_) return false;
        queue_.push_back(std::move(item));
        notEmpty_.notify_one();
        return true;
    }

    // ── 读端 ─────────────────────────────────────────────────────────────────

    /**
     * @brief 弹出元素；若队列为空则阻塞等待
     * @return false 表示队列已关闭且为空（消费者应退出）
     */
    bool pop(T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        notEmpty_.wait(lock, [this] {
            return !queue_.empty() || closed_;
        });
        if (queue_.empty()) return false;   // closed + empty
        item = std::move(queue_.front());
        queue_.pop_front();
        notFull_.notify_one();
        return true;
    }

    /**
     * @brief 带超时的 pop，超时返回 false
     */
    bool popTimeout(T& item, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!notEmpty_.wait_for(lock, timeout, [this] {
                return !queue_.empty() || closed_;
            })) {
            return false;
        }
        if (queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop_front();
        notFull_.notify_one();
        return true;
    }

    // ── 控制 ─────────────────────────────────────────────────────────────────

    /** 关闭队列，唤醒所有等待线程 */
    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        notFull_.notify_all();
        notEmpty_.notify_all();
    }

    /** 通知消费者立即 flush（向队列插入刷盘信号用外部约定，此处仅唤醒） */
    void flush() {
        notEmpty_.notify_one();
    }

    // ── 查询 ─────────────────────────────────────────────────────────────────
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }
    bool isClosed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

private:
    mutable std::mutex      mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    std::deque<T>           queue_;
    size_t                  capacity_;
    bool                    closed_;
};
