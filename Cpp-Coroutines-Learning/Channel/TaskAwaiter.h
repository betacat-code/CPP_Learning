#pragma once
#include "Executor.h"

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