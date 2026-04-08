#pragma once

#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <atomic>
#include <cassert>

// ------------------------------------------------------------
//  ThreadPool
//
//  Fixed-size pool of worker threads. Tasks are std::function
//  objects submitted via submit(). Workers drain the queue
//  until stop() is called.
//
//  Usage:
//      ThreadPool pool(std::thread::hardware_concurrency());
//      pool.submit([&]{ ... });
//      pool.wait_all();   // block until all in-flight tasks done
//      pool.stop();       // join all threads
// ------------------------------------------------------------

class ThreadPool {
public:
    using Task = std::function<void()>;

    explicit ThreadPool(size_t num_threads) : stop_(false), active_(0) {
        assert(num_threads > 0);
        workers_.reserve(num_threads);
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ~ThreadPool() {
        stop();
    }

    // Non-copyable, non-movable
    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Submit a task to the pool. Thread-safe.
    void submit(Task task) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            assert(!stop_ && "ThreadPool: submit after stop");
            queue_.push(std::move(task));
        }
        cv_work_.notify_one();
    }

    // Block until all submitted tasks have completed.
    void wait_all() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_done_.wait(lock, [this] {
            return queue_.empty() && active_.load() == 0;
        });
    }

    // Signal workers to stop and join all threads.
    // Waits for in-flight tasks to finish first.
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_) return;
            stop_ = true;
        }
        cv_work_.notify_all();
        for (auto& t : workers_) {
            if (t.joinable()) t.join();
        }
        workers_.clear();
    }

    size_t thread_count() const { return workers_.size(); }

private:
    void worker_loop() {
        while (true) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_work_.wait(lock, [this] {
                    return stop_ || !queue_.empty();
                });
                if (stop_ && queue_.empty()) return;
                task = std::move(queue_.front());
                queue_.pop();
                ++active_;
            }
            task();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                --active_;
            }
            cv_done_.notify_all();
        }
    }

    std::vector<std::thread>  workers_;
    std::queue<Task>          queue_;
    std::mutex                mutex_;
    std::condition_variable   cv_work_;
    std::condition_variable   cv_done_;
    std::atomic<int>          active_;
    bool                      stop_;
};
