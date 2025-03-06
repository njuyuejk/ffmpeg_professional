/**
 * @file threadpool.h
 * @brief 线程池头文件
 */
#pragma once

#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>
#include <string>

/**
 * @brief 线程池类
 */
class ThreadPool {
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    std::atomic<bool> stop;
    std::atomic<int> active_tasks;
    std::string pool_name;

public:
    /**
     * @brief 构造函数
     * @param threads 线程数量
     * @param name 线程池名称
     */
    ThreadPool(size_t threads, const std::string& name = "ThreadPool");

    /**
     * @brief 析构函数
     */
    ~ThreadPool();

    /**
     * @brief 获取任务队列大小
     * @return 任务队列大小
     */
    size_t getQueueSize();

    /**
     * @brief 获取活跃任务数
     * @return 活跃任务数
     */
    int getActiveTaskCount();

    /**
     * @brief 添加任务到线程池
     * @param f 任务函数
     * @param args 函数参数
     * @return 任务结果的future对象
     */
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
    -> std::future<typename std::result_of<F(Args...)>::type>;

    /**
     * @brief 优雅关闭线程池
     */
    void shutdown();

    /**
     * @brief 获取线程池名称
     * @return 线程池名称
     */
    std::string getName() const;
};

// 模板方法实现放在头文件中
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
-> std::future<typename std::result_of<F(Args...)>::type> {
    using return_type = typename std::result_of<F(Args...)>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> result = task->get_future();

    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        if (stop) {
            throw std::runtime_error("不能向已停止的线程池添加任务");
        }

        tasks.emplace([task]() { (*task)(); });
    }

    condition.notify_one();
    return result;
}