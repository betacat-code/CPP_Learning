#pragma once
#include "Executor.h"
#include "Scheduler.h"
#include <coroutine>

struct SleepAwaiter {

    explicit SleepAwaiter(AbstractExecutor* executor, long long duration) noexcept
        : _executor(executor), _duration(duration) {}

    bool await_ready() const { return false; }

    void await_suspend(std::coroutine_handle<> handle) const {
        static Scheduler scheduler;

        scheduler.execute([this, handle]() {
            _executor->execute([handle]() {
                handle.resume();
                });
            }, _duration);
    }

    void await_resume() {}

private:
    AbstractExecutor* _executor;
    long long _duration;
};