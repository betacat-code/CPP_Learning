#define __cpp_lib_coroutine
#include <coroutine>
#include <exception>
#include <iostream>
#include <thread>
#include <functional>
#include <mutex>
#include <list>
#include <optional>

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

// 前向声明
template<typename ResultType>
struct Task;

template<typename Result>
struct TaskAwaiter {
    explicit TaskAwaiter(Task<Result>&& task) noexcept
        : task(std::move(task)) {}

    TaskAwaiter(TaskAwaiter&& completion) noexcept
        : task(std::exchange(completion.task, {})) {}

    TaskAwaiter(TaskAwaiter&) = delete;

    TaskAwaiter& operator=(TaskAwaiter&) = delete;

    constexpr bool await_ready() const noexcept {
        return false;
    }

    void await_suspend(std::coroutine_handle<> handle) noexcept {
        task.finally([handle]() {
            handle.resume();
            });
    }

    Result await_resume() noexcept {
        return task.get_result();
    }

private:
    Task<Result> task;
};


template<typename ResultType>
struct TaskPromise {
    std::suspend_never initial_suspend() { return {}; }

    std::suspend_always final_suspend() noexcept { return {}; }

    Task<ResultType> get_return_object() {
        return Task{ std::coroutine_handle<TaskPromise>::from_promise(*this) };
    }

    template<typename _ResultType>
    TaskAwaiter<_ResultType> await_transform(Task<_ResultType>&& task) {
        return TaskAwaiter<_ResultType>(std::move(task));
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

template<typename ResultType>
struct Task {

    using promise_type = TaskPromise<ResultType>;

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

void debug(const std::string& s) {
    printf("%d %s\n", std::this_thread::get_id(),s.c_str());
}

void debug(const std::string& s,int x) {
    printf("%d %s %d\n", std::this_thread::get_id(), s.c_str(),x);
}

Task<int> simple_task2() {
    debug("task 2 start ...");
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(1s);
    debug("task 2 returns after 1s.");
    co_return 2;
}

Task<int> simple_task3() {
    debug("in task 3 start ...");
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(2s);
    debug("task 3 returns after 2s.");
    co_return 3;
}

Task<int> simple_task() {
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
