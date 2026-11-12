#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace df::datalog {

class ThreadExecutor {
  public:
    struct Task {
        std::function<void()> call;
        std::vector<size_t> successors;
        size_t prerequisites = 0;
    };

    explicit ThreadExecutor(size_t num_workers) {
        try {
            for (size_t i = 0; i < std::max<size_t>(1, num_workers); ++i)
                workers.emplace_back([this] { work(); });
        } catch (...) {
            stop();
            throw;
        }
    }

    ThreadExecutor(const ThreadExecutor &) = delete;
    ThreadExecutor &operator=(const ThreadExecutor &) = delete;

    ~ThreadExecutor() { stop(); }

    void run(std::vector<Task> &task_queue) {
        if (task_queue.empty())
            return;
        std::unique_lock lock(mutex);
        tasks = &task_queue;
        remaining.resize(task_queue.size());
        unfinished = task_queue.size();
        err = nullptr;
        for (size_t i = 0; i < task_queue.size(); ++i) {
            remaining[i] = task_queue[i].prerequisites;
            if (remaining[i] == 0)
                ready.push_back(i);
        }
        if (ready.empty()) {
            tasks = nullptr;
            throw std::logic_error("Task graph has no available tasks");
        }
        readychanged.notify_all();
        completed.wait(lock, [this] {
            return unfinished == 0 || (err && active == 0);
        });
        tasks = nullptr;
        if (err)
            std::rethrow_exception(err);
    }

  private:
    void stop() {
        {
            std::lock_guard lock(mutex);
            stopping = true;
        }
        readychanged.notify_all();
        for (auto &worker : workers)
            worker.join();
    }

    void work() {
        std::unique_lock lock(mutex);
        while (true) {
            readychanged.wait(lock, [this] {
                return stopping || !ready.empty();
            });
            if (stopping)
                return;
            const size_t id = ready.front();
            ready.pop_front();
            ++active;
            lock.unlock();
            std::exception_ptr pending_err;
            try {
                (*tasks)[id].call();
            } catch (...) {
                pending_err = std::current_exception();
            }
            lock.lock();
            --active;
            --unfinished;
            if (pending_err && !err) {
                err = pending_err;
                ready.clear();
            }
            if (!err) {
                for (size_t next : (*tasks)[id].successors)
                    if (--remaining[next] == 0)
                        ready.push_back(next);
            }
            if (!err && active == 0 && ready.empty() && unfinished != 0)
                err = std::make_exception_ptr(
                    std::logic_error("Task graph has blocked tasks"));
            readychanged.notify_all();
            if (unfinished == 0 || (err && active == 0))
                completed.notify_one();
        }
    }

    std::mutex mutex;
    std::condition_variable readychanged;
    std::condition_variable completed;
    std::vector<std::thread> workers;
    std::vector<Task> *tasks = nullptr;
    std::vector<size_t> remaining;
    std::deque<size_t> ready;
    size_t unfinished = 0;
    size_t active = 0;
    bool stopping = false;
    std::exception_ptr err;
};

} // namespace df::datalog