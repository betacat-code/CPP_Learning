#define __cpp_lib_coroutine
#include <coroutine>
#include <exception>
#include <iostream>
#include <thread>
#include <functional>
#include <mutex>
#include <list>
#include <optional>
#include <cassert>
#include <queue>
#include <future>
using namespace std;
void debug(const std::string& s) {
    printf("%d %s\n", std::this_thread::get_id(), s.c_str());
}

void debug(const std::string& s, int x) {
    printf("%d %s %d\n", std::this_thread::get_id(), s.c_str(), x);
}

// 调度器
class AbstractExecutor {
public:
    virtual void execute(std::function<void()>&& func) = 0;
};

class NoopExecutor : public AbstractExecutor {
public:
    void execute(std::function<void()>&& func) override {
        func();
    }
};

class NewThreadExecutor : public AbstractExecutor {
public:
    void execute(std::function<void()>&& func) override {
        std::thread(func).detach();
    }
};

class AsyncExecutor : public AbstractExecutor {
public:
    void execute(std::function<void()>&& func) override {
        auto future = std::async(func);
    }
};

class LooperExecutor : public AbstractExecutor {
private:
    std::condition_variable queue_condition;
    std::mutex queue_lock;
    std::queue<std::function<void()>> executable_queue;

    std::atomic<bool> is_active;
    std::thread work_thread;

    void run_loop() {
        while (is_active.load(std::memory_order_relaxed) || !executable_queue.empty()) {
            std::unique_lock lock(queue_lock);
            if (executable_queue.empty()) {
                queue_condition.wait(lock);
                if (executable_queue.empty()) {
                    continue;
                }
            }
            auto func = executable_queue.front();
            executable_queue.pop();
            lock.unlock();

            func();
        }
        debug("run_loop exit.");
    }

public:

    LooperExecutor() {
        is_active.store(true, std::memory_order_relaxed);
        work_thread = std::thread(&LooperExecutor::run_loop, this);
    }

    ~LooperExecutor() {
        shutdown(false);
        if (work_thread.joinable()) {
            work_thread.join();
        }
    }

    void execute(std::function<void()>&& func) override {
        std::unique_lock lock(queue_lock);
        if (is_active.load(std::memory_order_relaxed)) {
            executable_queue.push(func);
            lock.unlock();
            queue_condition.notify_one();
        }
    }

    void shutdown(bool wait_for_complete = true) {
        is_active.store(false, std::memory_order_relaxed);
        if (!wait_for_complete) {
            // clear queue.
            std::unique_lock lock(queue_lock);
            decltype(executable_queue) empty_queue;
            std::swap(executable_queue, empty_queue);
            lock.unlock();
        }

        queue_condition.notify_all();
    }
};

template<typename T>
struct Result
{
    explicit Result() = default;
    explicit Result(T&& value) : _value(value) {}
    explicit Result(std::exception_ptr&& exception_ptr) : _exception_ptr(exception_ptr) {}
    T get_or_throw() {
        if (_exception_ptr) {
            std::rethrow_exception(_exception_ptr);
        }
        return _value;
    }
private:
    T _value;
    std::exception_ptr _exception_ptr;
};
// 用于协程initial_suspend()时直接将运行逻辑切入调度器的等待体
struct DispatchAwaiter {

    explicit DispatchAwaiter(AbstractExecutor* executor) noexcept
        : _executor(executor) {}

    bool await_ready() const { return false; }

    void await_suspend(std::coroutine_handle<> handle) const {
        _executor->execute([handle]() {
            handle.resume();
            });
    }

    void await_resume() {}

private:
    AbstractExecutor* _executor;
};


// 前向声明
template<typename ResultType, typename Executor>
struct Task;

template<typename Result, typename Executor>
struct TaskAwaiter {
    explicit TaskAwaiter(AbstractExecutor* executor, Task<Result, Executor>&& task) noexcept
        : _executor(executor), task(std::move(task)) {}

    TaskAwaiter(TaskAwaiter&& completion) noexcept
        : _executor(completion._executor), task(std::exchange(completion.task, {})) {}

    TaskAwaiter(TaskAwaiter&) = delete;

    TaskAwaiter& operator=(TaskAwaiter&) = delete;

    constexpr bool await_ready() const noexcept {
        return false;
    }
    // 在这里增加了调度器的运行
    void await_suspend(std::coroutine_handle<> handle) noexcept {
        task.finally([handle, this]() {
            _executor->execute([handle]() {
                handle.resume();
                });
            });
    }

    Result await_resume() noexcept {
        return task.get_result();
    }

private:
    Task<Result, Executor> task;
    AbstractExecutor* _executor;
};

// 对应修改增加调度器的传入
template<typename ResultType,typename Executor>
struct TaskPromise {
    //此时调度器将开始调度，执行的逻辑
    DispatchAwaiter initial_suspend() { return DispatchAwaiter(&executor); }

    std::suspend_always final_suspend() noexcept { return {}; }

    Task<ResultType, Executor> get_return_object() {
        return Task{ std::coroutine_handle<TaskPromise>::from_promise(*this) };
    }
    //在这里返回等待器对象时需要将调度器的指针带上
    template<typename _ResultType, typename _Executor>
    TaskAwaiter<_ResultType, _Executor> await_transform(Task<_ResultType, _Executor>&& task) {
        return TaskAwaiter<_ResultType, _Executor>(&executor, std::move(task));
    }

    void unhandled_exception() {
        std::lock_guard lock(completion_lock);
        result = Result<ResultType>(std::current_exception());
        completion.notify_all();
        notify_callbacks();
    }

    void return_value(ResultType value) {
        std::lock_guard lock(completion_lock);
        result = Result<ResultType>(std::move(value));
        completion.notify_all();
        notify_callbacks();
    }

    ResultType get_result() {
        std::unique_lock lock(completion_lock);
        if (!result.has_value()) {
            completion.wait(lock);
        }
        return result->get_or_throw();
    }

    void on_completed(std::function<void(Result<ResultType>)>&& func) {
        std::unique_lock lock(completion_lock);
        if (result.has_value()) {
            auto value = result.value();
            lock.unlock();
            func(value);
        }
        else {
            completion_callbacks.push_back(func);
        }
    }

private:
    std::optional<Result<ResultType>> result;
    Executor executor;
    std::mutex completion_lock;
    std::condition_variable completion;

    std::list<std::function<void(Result<ResultType>)>> completion_callbacks;

    void notify_callbacks() {
        auto value = result.value();
        for (auto& callback : completion_callbacks) {
            callback(value);
        }
        completion_callbacks.clear();
    }

};

template<typename ResultType,typename Executor = NewThreadExecutor>
struct Task {

    using promise_type = TaskPromise<ResultType, Executor>;

    ResultType get_result() {
        return handle.promise().get_result();
    }

    Task& then(std::function<void(ResultType)>&& func) {
        handle.promise().on_completed([func](auto result) {
            try {
                func(result.get_or_throw());
            }
            catch (std::exception& e) {
                // ignore.
            }
            });
        return *this;
    }

    Task& catching(std::function<void(std::exception&)>&& func) {
        handle.promise().on_completed([func](auto result) {
            try {
                result.get_or_throw();
            }
            catch (std::exception& e) {
                func(e);
            }
            });
        return *this;
    }

    Task& finally(std::function<void()>&& func) {
        handle.promise().on_completed([func](auto result) { func(); });
        return *this;
    }

    explicit Task(std::coroutine_handle<promise_type> handle) noexcept : handle(handle) {}

    Task(Task&& task) noexcept : handle(std::exchange(task.handle, {})) {}

    Task(Task&) = delete;

    Task& operator=(Task&) = delete;

    ~Task() {
        if (handle) handle.destroy();
    }

private:
    std::coroutine_handle<promise_type> handle;
};
// 这意味着每个恢复的位置都会通过 std::async 上执行
Task<int,AsyncExecutor> simple_task2() {
    debug("task 2 start ...");
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(1s);
    debug("task 2 returns after 1s.");
    co_return 2;
}
// 这意味着每个恢复的位置都会新建一个线程来执行
Task<int,NewThreadExecutor> simple_task3() {
    debug("in task 3 start ...");
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(2s);
    debug("task 3 returns after 2s.");
    co_return 3;
}
//这意味着每个恢复的位置都会在同一个线程上执行
Task<int, LooperExecutor> simple_task() {
    debug("task start ...");
    auto result2 = co_await simple_task2();
    debug("returns from task2: ", result2);
    auto result3 = co_await simple_task3();
    debug("returns from task3: ", result3);
    co_return 1 + result2 + result3;
}

int main() {
    auto simpleTask = simple_task();
    simpleTask.then([](int i) {
        debug("simple task end: ", i);
        }).catching([](std::exception& e) {
            //debug("error occurred", e.what());
        });
    try {
        auto i = simpleTask.get_result();
        debug("simple task end from get: ", i);
    }
    catch (std::exception& e) {
        //debug("error: ", e.what());
    }
    return 0;
}
