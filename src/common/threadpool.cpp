/**
 * @file threadpool.cpp
 * @brief 线程池实现
 */
#include "common/threadpool.h"
#include "logger/logger.h"

// 构造函数
ThreadPool::ThreadPool(size_t threads, const std::string& name)
        : stop(false), active_tasks(0), pool_name(name) {
    Logger::info("创建线程池: " + name + ", 线程数: " + std::to_string(threads));

    for (size_t i = 0; i < threads; ++i) {
        workers.emplace_back([this, i, name] {
            Logger::info(name + "-Worker-" + std::to_string(i));
            Logger::debug("线程池工作线程启动: " + name + "-Worker-" + std::to_string(i));

            while (true) {
                std::function<void()> task;

                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    this->condition.wait(lock, [this] {
                        return this->stop || !this->tasks.empty();
                    });

                    if (this->stop && this->tasks.empty()) {
                        return;
                    }

                    task = std::move(this->tasks.front());
                    this->tasks.pop();
                }

                this->active_tasks++;

                try {
                    task();
                } catch (const std::exception& e) {
                    Logger::error("线程池任务异常: " + std::string(e.what()));
                } catch (...) {
                    Logger::error("线程池任务未知异常");
                }

                this->active_tasks--;
            }
        });
    }
}

// 析构函数
ThreadPool::~ThreadPool() {
    if (!stop) {
        shutdown();
    }
}

// 获取任务队列大小
size_t ThreadPool::getQueueSize() {
    std::lock_guard<std::mutex> lock(queue_mutex);
    return tasks.size();
}

// 获取活跃任务数
int ThreadPool::getActiveTaskCount() {
    return active_tasks;
}

// 优雅关闭线程池
void ThreadPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        stop = true;
    }

    condition.notify_all();

    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    Logger::info("线程池已关闭: " + pool_name);
}

// 获取线程池名称
std::string ThreadPool::getName() const {
    return pool_name;
}