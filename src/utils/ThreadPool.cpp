#include "utils/ThreadPool.hpp"

#include <utility>

namespace fse {

ThreadPool::ThreadPool(std::size_t threadCount) {
    if (threadCount == 0) {
        threadCount = 1;
    }

    workers_.reserve(threadCount);
    for (std::size_t i = 0; i < threadCount; ++i) {
        workers_.emplace_back([this]() { workerLoop(); });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    workAvailable_.notify_all();

    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(std::move(task));
    }
    workAvailable_.notify_one();
}

void ThreadPool::waitAll() {
    std::unique_lock<std::mutex> lock(mutex_);
    allDone_.wait(lock, [this]() { return tasks_.empty() && activeTasks_ == 0; });
}

void ThreadPool::workerLoop() {
    for (;;) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            workAvailable_.wait(lock,
                                [this]() { return stop_ || !tasks_.empty(); });

            if (stop_ && tasks_.empty()) {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
            ++activeTasks_;
        }

        task();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            --activeTasks_;
        }
        allDone_.notify_all();
    }
}

}  // namespace fse
