/**
 * @file thread_pool.cpp
 * @brief 高性能线程池实现
 */

#include "common/threadpool.h"
#include "logger/logger.h"
#include <sstream>

namespace ffmpeg_stream {

    ThreadPool::ThreadPool(size_t numThreads)
            : stop_(false), waitingAll_(false), activeThreadCount_(0) {

        if (numThreads <= 0) {
            numThreads = std::thread::hardware_concurrency();
        }

        // 创建指定数量的工作线程
        workers_.reserve(numThreads);
        for (size_t i = 0; i < numThreads; ++i) {
            workers_.emplace_back(&ThreadPool::workerThread, this);

            // 获取线程ID并设置线程名称（仅用于调试）
            std::ostringstream threadName;
            threadName << "worker-" << i;

            Logger::debug("Created thread pool worker: %s", threadName.str().c_str());
        }

        Logger::info("Thread pool initialized with %zu threads", numThreads);
    }

    ThreadPool::~ThreadPool() {
        // 如果没有显式调用stop，在析构函数中停止所有线程
        if (!stop_) {
            stop(false);
        }
    }

    void ThreadPool::workerThread() {
        // 线程循环，不断从任务队列中获取并执行任务
        while (true) {
            std::function<void()> task;

            {
                std::unique_lock<std::mutex> lock(queueMutex_);

                // 等待条件变量通知，条件是：有新任务或线程池停止
                condition_.wait(lock, [this] {
                    return stop_ || !tasks_.empty();
                });

                // 如果线程池停止且任务队列为空，线程退出
                if (stop_ && tasks_.empty()) {
                    return;
                }

                // 从队列中获取优先级最高的任务
                task = tasks_.top().task;
                tasks_.pop();
            }

            // 增加活跃线程计数
            ++activeThreadCount_;

            // 执行任务
            try {
                task();
            } catch (const std::exception& e) {
                Logger::error("Exception in thread pool task: %s", e.what());
            } catch (...) {
                Logger::error("Unknown exception in thread pool task");
            }

            // 减少活跃线程计数
            --activeThreadCount_;

            // 如果线程池正在等待所有任务完成，检查是否应该通知waitAll
            if (waitingAll_) {
                std::unique_lock<std::mutex> lock(queueMutex_);
                if (tasks_.empty() && activeThreadCount_ == 0) {
                    waitAllCondition_.notify_all();
                }
            }
        }
    }

    size_t ThreadPool::size() const {
        return workers_.size();
    }

    size_t ThreadPool::queueSize() const {
        std::unique_lock<std::mutex> lock(queueMutex_);
        return tasks_.size();
    }

    size_t ThreadPool::activeThreads() const {
        return activeThreadCount_;
    }

    void ThreadPool::resize(size_t numThreads) {
        if (stop_) {
            Logger::warning("Cannot resize a stopped thread pool");
            return;
        }

        size_t currentSize = workers_.size();

        // 如果新尺寸大于当前尺寸，添加更多线程
        if (numThreads > currentSize) {
            workers_.reserve(numThreads);
            for (size_t i = currentSize; i < numThreads; ++i) {
                workers_.emplace_back(&ThreadPool::workerThread, this);
            }

            Logger::info("Thread pool resized from %zu to %zu threads", currentSize, numThreads);
        }
            // 如果新尺寸小于当前尺寸，需要停止一些线程
            // 注意：这种实现方式会重新创建线程池，可能不是最高效的方式
            // 更高级的实现可以发送特殊信号让特定线程自行退出
        else if (numThreads < currentSize) {
            // 停止当前线程池
            stop(true);

            // 清除所有线程
            workers_.clear();

            // 重置停止标志
            stop_ = false;

            // 创建新的线程
            workers_.reserve(numThreads);
            for (size_t i = 0; i < numThreads; ++i) {
                workers_.emplace_back(&ThreadPool::workerThread, this);
            }

            Logger::info("Thread pool resized from %zu to %zu threads", currentSize, numThreads);
        }
    }

    void ThreadPool::waitAll() {
        waitingAll_ = true;

        std::unique_lock<std::mutex> lock(queueMutex_);
        waitAllCondition_.wait(lock, [this] {
            return tasks_.empty() && activeThreadCount_ == 0;
        });

        waitingAll_ = false;
    }

    void ThreadPool::stop(bool waitForTasks) {
        if (stop_) {
            return;
        }

        if (waitForTasks) {
            waitAll();
        }

        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            stop_ = true;
        }

        // 通知所有线程，使它们可以退出
        condition_.notify_all();

        // 等待所有线程完成
        for (std::thread& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        Logger::info("Thread pool stopped");
    }

} // namespace ffmpeg_stream