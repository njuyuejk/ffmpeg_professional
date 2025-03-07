/**
 * @file thread_pool.h
 * @brief 高性能线程池实现
 */

#ifndef FFMPEG_STREAM_THREAD_POOL_H
#define FFMPEG_STREAM_THREAD_POOL_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>
#include <memory>
#include <stdexcept>

namespace ffmpeg_stream {

/**
 * @brief 任务优先级枚举
 */
    enum class TaskPriority {
        HIGH,    // 高优先级任务，如关键帧处理
        NORMAL,  // 普通优先级
        LOW      // 低优先级任务，如统计和监控
    };

/**
 * @class ThreadPool
 * @brief 实现高性能线程池，支持任务优先级和实时性保证
 */
    class ThreadPool {
    public:
        /**
         * @brief 构造具有指定数量线程的线程池
         * @param numThreads 线程池中的线程数，默认为硬件并发线程数
         */
        explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency());

        /**
         * @brief 析构函数，停止所有线程
         */
        ~ThreadPool();

        /**
         * @brief 提交任务到线程池
         * @param priority 任务优先级
         * @param f 任务函数
         * @param args 任务函数参数
         * @return std::future 包含任务执行结果的future
         */
        template<class F, class... Args>
        auto enqueue(TaskPriority priority, F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>;

        /**
         * @brief 获取当前线程池大小
         * @return 线程池中的线程数
         */
        size_t size() const;

        /**
         * @brief 获取当前队列中等待的任务数
         * @return 等待的任务数
         */
        size_t queueSize() const;

        /**
         * @brief 获取活跃的线程数
         * @return 当前正在执行任务的线程数
         */
        size_t activeThreads() const;

        /**
         * @brief 设置线程池大小
         * @param numThreads 新的线程池大小
         */
        void resize(size_t numThreads);

        /**
         * @brief 等待所有任务完成
         */
        void waitAll();

        /**
         * @brief 停止所有线程
         * @param waitForTasks 是否等待现有任务完成，默认为true
         */
        void stop(bool waitForTasks = true);

    private:
        /**
         * @brief 任务包装结构体，包含优先级和实际任务
         */
        struct TaskWrapper {
            TaskPriority priority;
            std::function<void()> task;

            // 为优先级队列比较运算符
            bool operator<(const TaskWrapper& other) const {
                // 注意这里反向比较，使优先级高的排在队列前面
                return priority < other.priority;
            }
        };

        // 工作线程函数
        void workerThread();

        // 线程池是否应该停止
        std::atomic<bool> stop_;

        // 等待所有任务完成的标志
        std::atomic<bool> waitingAll_;

        // 线程池
        std::vector<std::thread> workers_;

        // 任务队列，使用优先级队列确保高优先级任务先执行
        std::priority_queue<TaskWrapper> tasks_;

        // 活跃任务计数
        std::atomic<size_t> activeThreadCount_;

        // 同步原语
        mutable std::mutex queueMutex_;
        std::condition_variable condition_;
        std::condition_variable waitAllCondition_;
    };

// 模板方法实现
    template<class F, class... Args>
    auto ThreadPool::enqueue(TaskPriority priority, F&& f, Args&&... args)
    -> std::future<typename std::result_of<F(Args...)>::type> {

        using return_type = typename std::result_of<F(Args...)>::type;

        // 创建一个packaged_task，包装用户提供的函数和参数
        auto task = std::make_shared<std::packaged_task<return_type()>>(
                std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        // 获取future以便稍后检索结果
        std::future<return_type> res = task->get_future();

        {
            std::unique_lock<std::mutex> lock(queueMutex_);

            // 如果线程池已停止，抛出异常
            if (stop_) {
                throw std::runtime_error("Enqueue on stopped ThreadPool");
            }

            // 包装任务与其优先级
            TaskWrapper wrapper;
            wrapper.priority = priority;
            wrapper.task = [task]() { (*task)(); };

            // 将任务添加到队列
            tasks_.push(wrapper);
        }

        // 通知一个线程处理新任务
        condition_.notify_one();

        return res;
    }

} // namespace ffmpeg_stream

#endif // FFMPEG_STREAM_THREAD_POOL_H