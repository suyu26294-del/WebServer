#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <stdexcept>
#include <atomic>

/**
 * @brief 固定大小线程池
 *
 * 特性：
 *   - 支持提交任意 callable（通过 std::packaged_task 包装，可获取 future）
 *   - 任务队列无界（上层通过 BlockQueue 限流）
 *   - RAII 管理线程生命周期
 *   - 停止后拒绝新任务，存量任务处理完毕后工作线程退出
 */
class ThreadPool {
public:
    explicit ThreadPool(size_t threadCount = std::thread::hardware_concurrency())
        : stop_(false) {
        if (threadCount == 0) threadCount = 1;
        workers_.reserve(threadCount);
        for (size_t i = 0; i < threadCount; ++i) {
            workers_.emplace_back([this] { workerLoop(); });
        }
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& w : workers_) {
            if (w.joinable()) w.join();
        }
    }

    // 禁止拷贝和移动
    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /**
     * @brief 提交任务（最常用接口，无需关心返回值）
     */
    void submit(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_) throw std::runtime_error("ThreadPool: submit on stopped pool");
            tasks_.push(std::move(task));
        }
        cv_.notify_one();
    }

    /**
     * @brief 提交任务并获取 future（用于等待结果）
     */
    template <typename F, typename... Args>
    auto submitWithResult(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {
        using R = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<R()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        auto fut = task->get_future();
        submit([task] { (*task)(); });
        return fut;
    }

    size_t threadCount() const noexcept { return workers_.size(); }

    size_t pendingTasks() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.size();
    }

private:
    void workerLoop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            // 执行任务时不持有锁，最大化并行度
            try {
                task();
            } catch (const std::exception& e) {
                // 任务内部异常不允许传播到线程，记录后继续
                // 此处不引入 Log 头以保持 pool 模块独立性
                (void)e;
            }
        }
    }

    std::vector<std::thread>           workers_;
    std::queue<std::function<void()>>  tasks_;
    mutable std::mutex                 mutex_;
    std::condition_variable            cv_;
    bool                               stop_;
};
