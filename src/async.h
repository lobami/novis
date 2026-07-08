#pragma once

// =============================================================================
// Novis Async Runtime
// =============================================================================
// `spawn fn() -> T` returns a `Task<T>`. `await task` blocks the current fiber
// until the task finishes and returns its value. Tasks run on a global worker
// pool; awaiting suspends the calling thread until the result is ready.
//
// The runtime is a header-only combination of:
//   * std::thread pool for the actual work.
//   * std::future / std::promise for one-shot value delivery.
//   * std::shared_ptr<TaskState> for type erasure, so the value type can be
//     erased away and the language can treat all Tasks uniformly.
//
// This file is consumed both by the interpreter (via `Evaluator`) and by the
// native C++ backend (via the header-only `nvrt` namespace used in
// `src/nv_runtime.h`).

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace novistd {

// ---- Task -----------------------------------------------------------------
//
// A Task<T> is a lightweight handle around a shared state. Awaiting the same
// task from multiple threads is safe; only the first await blocks for the
// result, subsequent ones observe the already-finished state.

struct TaskError {
    std::string what;
    explicit TaskError(std::string w) : what(std::move(w)) {}
};

template <typename T>
class Task;

class TaskStateBase {
public:
    TaskStateBase() = default;
    virtual ~TaskStateBase() = default;

    bool is_ready() const { return ready_.load(std::memory_order_acquire); }
    void set_ready() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            ready_.store(true, std::memory_order_release);
        }
        cv_.notify_all();
    }
    void wait() {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [this]{ return ready_.load(std::memory_order_acquire); });
    }

private:
    std::atomic<bool> ready_{false};
    mutable std::mutex mu_;
    std::condition_variable cv_;
};

template <typename T>
class TaskState : public TaskStateBase {
public:
    void set_value(T v) { value_ = std::move(v); set_ready(); }
    void set_error(std::string w) { err_ = std::move(w); set_ready(); }
    const T& value() const { return value_; }
    const std::string& error() const { return err_; }
    bool has_error() const { return !err_.empty(); }

private:
    T value_{};
    std::string err_;
};

// ---- Worker Pool -----------------------------------------------------------
//
// A small global thread pool. The first spawn creates the pool; subsequent
// spawns reuse it. Workers exit when the program terminates (process exit).

class WorkerPool {
public:
    static WorkerPool& instance() {
        static WorkerPool pool;
        return pool;
    }

    void submit(std::function<void()> job) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            queue_.push(std::move(job));
        }
        cv_.notify_one();
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) if (t.joinable()) t.join();
    }

    std::size_t size() const { return workers_.size(); }

private:
    WorkerPool() {
        std::size_t n = std::max<std::size_t>(1, std::thread::hardware_concurrency());
        workers_.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            workers_.emplace_back([this]{ this->run(); });
        }
    }

    ~WorkerPool() { shutdown(); }

    void run() {
        while (true) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait(lk, [this]{ return stop_ || !queue_.empty(); });
                if (stop_ && queue_.empty()) return;
                job = std::move(queue_.front());
                queue_.pop();
            }
            try {
                job();
            } catch (...) {
                // Swallow exceptions; the Task's promise will carry the error
                // through a different channel. This keeps the worker alive.
            }
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> queue_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    bool stop_ = false;
};

// ---- Task<T> public type ---------------------------------------------------
//
// A Task<T> is a copyable, shared-pointer to its state. The state carries the
// result (or an error) and the synchronization primitives.

template <typename T>
class Task {
public:
    Task() = default;
    explicit Task(std::shared_ptr<TaskState<T>> state) : state_(std::move(state)) {}

    bool is_ready() const { return state_ && state_->is_ready(); }

    // block on the task and return its value. Throws on error.
    T await() {
        if (!state_) throw std::runtime_error("await on empty Task");
        state_->wait();
        if (state_->has_error()) throw std::runtime_error(state_->error());
        return state_->value();
    }

    // Non-throwing variant. Returns optional-like pair: ok flag + value.
    std::pair<bool, T> await_or() {
        if (!state_) return {false, T{}};
        state_->wait();
        if (state_->has_error()) return {false, T{}};
        return {true, state_->value()};
    }

    std::shared_ptr<TaskState<T>> state() const { return state_; }

private:
    std::shared_ptr<TaskState<T>> state_;
};

// ---- spawn helper ----------------------------------------------------------
//
// `spawn(std::function<T()>)` submits the job to the worker pool and returns
// a Task<T>. The job is responsible for filling the state with a value or an
// error.

template <typename T>
Task<T> spawn(std::function<T()> job) {
    auto state = std::make_shared<TaskState<T>>();
    Task<T> task(state);
    WorkerPool::instance().submit([state, job = std::move(job)]() mutable {
        try {
            state->set_value(job());
        } catch (const std::exception& e) {
            state->set_error(std::string("async task failed: ") + e.what());
        } catch (...) {
            state->set_error("async task failed: unknown error");
        }
    });
    return task;
}

// ---- await helpers (for codegen) -------------------------------------------
//
// The C++ codegen calls these directly. We provide overloads for Value-like
// wrappers produced by the interpreter.

inline void nv_await(Task<int64_t>& t)        { (void)t.await(); }
inline void nv_await(Task<double>& t)         { (void)t.await(); }
inline void nv_await(Task<bool>& t)           { (void)t.await(); }
inline void nv_await(Task<std::string>& t)    { (void)t.await(); }
inline void nv_await(Task<Task<int64_t>>& t)  { (void)t.await().await(); }

} // namespace novistd
