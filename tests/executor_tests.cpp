#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include <datalog/executor.hpp>
#include <gtest/gtest.h>

namespace {

using df::datalog::ThreadExecutor;

constexpr auto kWaitLimit = std::chrono::seconds(10);

TEST(ThreadExecutorTests, WorkerCountMatchesRequest) {
    for (size_t count : {1u, 2u, 4u, 8u}) {
        ThreadExecutor executor(count);
        EXPECT_EQ(executor.worker_count(), count);
    }
}

TEST(ThreadExecutorTests, SpawnOutsideTaskThrows) {
    ThreadExecutor executor(2);
    EXPECT_THROW(executor.spawn([] {}), std::logic_error);
}

TEST(ThreadExecutorTests, SuccessorWaitsForSpawnedChild) {
    ThreadExecutor executor(2);
    std::mutex mutex;
    std::condition_variable changed;
    bool child_started = false;
    bool release_child = false;
    int successor_runs = 0;

    std::vector<ThreadExecutor::Task> tasks(2);
    tasks[0].successors.push_back(1);
    tasks[1].prerequisites = 1;

    tasks[0].call = [&] {
        ThreadExecutor::current()->spawn([&] {
            std::unique_lock lock(mutex);
            child_started = true;
            changed.notify_all();
            changed.wait(lock, [&] { return release_child; });
        });
    };
    tasks[1].call = [&] {
        std::lock_guard lock(mutex);
        ++successor_runs;
    };

    std::thread caller([&] { executor.run(tasks); });

    bool child_observed = false;
    {
        std::unique_lock lock(mutex);
        child_observed = changed.wait_for(lock, kWaitLimit,
                                          [&] { return child_started; });
        EXPECT_EQ(successor_runs, 0);
    }
    EXPECT_TRUE(child_observed);

    {
        std::lock_guard lock(mutex);
        release_child = true;
    }
    changed.notify_all();
    caller.join();

    EXPECT_EQ(successor_runs, 1);
}

TEST(ThreadExecutorTests, SuccessorWaitsForNestedChild) {
    ThreadExecutor executor(4);
    std::mutex mutex;
    std::condition_variable changed;
    bool grandchild_started = false;
    bool release_grandchild = false;
    bool grandchild_finished = false;
    int successor_runs = 0;

    std::vector<ThreadExecutor::Task> tasks(2);
    tasks[0].successors.push_back(1);
    tasks[1].prerequisites = 1;

    tasks[0].call = [&] {
        ThreadExecutor::current()->spawn([&] {
            ThreadExecutor::current()->spawn([&] {
                std::unique_lock lock(mutex);
                grandchild_started = true;
                changed.notify_all();
                changed.wait(lock, [&] { return release_grandchild; });
                grandchild_finished = true;
            });
        });
    };
    tasks[1].call = [&] {
        std::lock_guard lock(mutex);
        ++successor_runs;
    };

    std::thread caller([&] { executor.run(tasks); });

    bool grandchild_observed = false;
    {
        std::unique_lock lock(mutex);
        grandchild_observed = changed.wait_for(
            lock, kWaitLimit, [&] { return grandchild_started; });
        EXPECT_EQ(successor_runs, 0);
    }
    EXPECT_TRUE(grandchild_observed);

    {
        std::lock_guard lock(mutex);
        release_grandchild = true;
    }
    changed.notify_all();
    caller.join();

    EXPECT_TRUE(grandchild_finished);
    EXPECT_EQ(successor_runs, 1);
}

TEST(ThreadExecutorTests, FailureWaitsForActiveChildAndExecutorReuse) {
    ThreadExecutor executor(4);
    std::mutex mutex;
    std::condition_variable changed;
    auto owned = std::make_shared<std::atomic<int>>(0);
    bool blocked_started = false;
    bool failure_raised = false;
    bool release_blocked = false;
    bool blocked_finished = false;

    std::vector<ThreadExecutor::Task> tasks(1);
    tasks[0].call = [&] {
        ThreadExecutor::current()->spawn([&] {
            std::unique_lock lock(mutex);
            changed.wait_for(lock, kWaitLimit,
                             [&] { return blocked_started; });
            failure_raised = true;
            changed.notify_all();
            throw std::runtime_error("intentional child failure");
        });
        ThreadExecutor::current()->spawn([&, owned] {
            std::unique_lock lock(mutex);
            blocked_started = true;
            changed.notify_all();
            changed.wait(lock, [&] { return release_blocked; });
            owned->store(42);
            blocked_finished = true;
            changed.notify_all();
        });
    };

    std::exception_ptr failure;
    std::atomic<bool> caller_finished{false};
    std::thread caller([&] {
        try {
            executor.run(tasks);
        } catch (...) {
            failure = std::current_exception();
        }
        caller_finished.store(true);
    });

    bool failure_observed = false;
    {
        std::unique_lock lock(mutex);
        failure_observed = changed.wait_for(
            lock, kWaitLimit,
            [&] { return blocked_started && failure_raised; });
        EXPECT_FALSE(caller_finished.load());
        EXPECT_FALSE(blocked_finished);
    }
    EXPECT_TRUE(failure_observed);

    {
        std::lock_guard lock(mutex);
        release_blocked = true;
    }
    changed.notify_all();
    caller.join();

    EXPECT_TRUE(caller_finished.load());
    EXPECT_NE(failure, nullptr);
    EXPECT_TRUE(blocked_finished);
    EXPECT_EQ(owned->load(), 42);
    EXPECT_EQ(executor.worker_count(), 4u);

    std::vector<ThreadExecutor::Task> reused(1);
    reused[0].call = [] {};
    EXPECT_NO_THROW(executor.run(reused));
}

TEST(ThreadExecutorTests, SingleWorkerRunsSpawnedChildBeforeSuccessor) {
    ThreadExecutor executor(1);
    std::atomic<int> order{0};
    std::atomic<int> child_order{0};
    std::atomic<int> successor_order{0};

    std::vector<ThreadExecutor::Task> tasks(2);
    tasks[0].successors.push_back(1);
    tasks[1].prerequisites = 1;
    tasks[0].call = [&] {
        ThreadExecutor::current()->spawn(
            [&] { child_order = order.fetch_add(1) + 1; });
    };
    tasks[1].call = [&] { successor_order = order.fetch_add(1) + 1; };

    executor.run(tasks);

    EXPECT_EQ(child_order.load(), 1);
    EXPECT_EQ(successor_order.load(), 2);
}

}
