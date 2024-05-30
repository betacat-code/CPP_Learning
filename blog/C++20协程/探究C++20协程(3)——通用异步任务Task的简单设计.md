
为了继续学习后续的内容，需要再定义一个类型 Task 来作为协程的返回值。Task 类型可以用来封装任何返回结果的异步行为


# 实现目标

外部非协程内的函数当中访问 Task 的结果时，我们可以通过回调或者同步阻塞调用两种方式来实现：
- 需要一个结果类型来承载正常返回和异常抛出的情况。
- 需要为 Task 定义相应的 promise_type 类型（定义了协程的行为和其返回的Task类型，负责维护协程的状态和最终结果）来支持 co_return 和 co_await。
- 为 Task 实现获取结果的阻塞函数 get_result 或者用于获取返回值的回调 then 以及用于获取抛出的异常的回调 catching。

then 方法接受一个回调函数，该函数会在 Task 正常完成时调用。catching 方法同样接受一个回调，用于处理 Task 中抛出的异常。

# 结果类型的定义(Result)

其用来描述 Task 正常返回的结果和抛出的异常，定义一个具有两者的结构体即可：

```cpp
template<typename T>
struct Result
{
    explicit Result() = default;
    explicit Result(T &&value) : _value(value) {}
    explicit Result(std::exception_ptr &&exception_ptr) : _exception_ptr(exception_ptr) {}
    T get_or_throw() {
        if (_exception_ptr){
            std::rethrow_exception(_exception_ptr);
        }
        return _value;
    }
private:
    T _value;
    std::exception_ptr _exception_ptr;
};
```

其中，Result 的模板参数 T 对应于 Task 的返回值类型。有了这个结果类型就可以很方便地在需要读取结果的时候调用 get_or_throw。

# promise_type 的定义

## 基本结构

Promise type 是一个协程的基础结构，它定义了协程的返回类型、如何初始化和结束协程，以及协程在遇到 co_await、co_yield、和 co_return 时的行为。其具体行为如下:

- 初始化和最终挂起：在协程开始执行前，会创建一个 promise 对象。当协程结束或最终挂起时，通过 promise 对象来清理和收尾。
- 处理返回和异常：promise 定义了如何存储返回值或异常，以及在协程结束时如何将这些值传递给调用者。
- 协程的控制点：在协程内部，每次遇到 co_await 或 co_yield 表达式，都会调用 promise 的相关方法来确定下一步行动，比如是否挂起协程或继续执行。

以下是基本的结构

```cpp
template<typename ResultType>
struct TaskPromise {
  // 协程立即执行
  std::suspend_never initial_suspend() { return {}; }
  // 执行结束后挂起，等待外部销毁。
  std::suspend_always final_suspend() noexcept { return {}; }
  // 构造协程的返回值对象 Task
  Task<ResultType> get_return_object() {
    return Task{std::coroutine_handle<TaskPromise>::from_promise(*this)};
  }
  void unhandled_exception() {
    // 将异常存入 result
    result = Result<ResultType>(std::current_exception());
  }
  void return_value(ResultType value) {
    // 将返回值存入 result，对应于协程内部的 'co_return value'
    result = Result<ResultType>(std::move(value));
  }
 private:
  // 使用 std::optional 可以区分协程是否执行完成
  std::optional<Result<ResultType>> result;

};
```

## await_transform

还需要为 Task 添加 co_await 的支持。有以下两种方式：

- 为 Task 实现 co_await 运算符
- 在 promise_type 当中定义 await_transform

从效果上来看，二者都可以做到。但区别在于，await_transform 是 promsie_type 的内部函数，可以直接访问到 promise 内部的状态；同时，await_transform 的定义也会限制协程内部对于其他类型的 co_await 的支持，将协程内部的挂起行为更好的管控起来，方便后续做统一的线程调度。因此此处采用 await_transform 来为 Task 提供 co_await 支持：

```cpp
template<typename ResultType>
struct TaskPromise {
  ...
  // 注意这里的模板参数
  template<typename _ResultType>
  TaskAwaiter<_ResultType> await_transform(Task<_ResultType> &&task) {
    return TaskAwaiter<_ResultType>(std::move(task));
  }
  ...
}
```
返回了一个 TaskAwaiter 的对象。**不过这里存在两个 Task**，一个是 TaskPromise 对应的 Task，一个是 co_await 表达式的操作数 Task，后者是 await_transform 的参数。

## TaskAwaiter

TaskAwaiter 是用于协程中等待异步任务（Task）完成的对象。在异步编程中，当协程需要等待一个异步任务完成时，可以使用 TaskAwaiter 对象来在协程中暂停执行，并在异步任务完成后继续执行。

```cpp
template<typename R>
struct TaskAwaiter {
  explicit TaskAwaiter(Task<R> &&task) noexcept
      : task(std::move(task)) {}

  TaskAwaiter(TaskAwaiter &&completion) noexcept
      : task(std::exchange(completion.task, {})) {}

  TaskAwaiter(TaskAwaiter &) = delete;

  TaskAwaiter &operator=(TaskAwaiter &) = delete;

  constexpr bool await_ready() const noexcept {
    return false;
  }

  void await_suspend(std::coroutine_handle<> handle) noexcept {
    // 当 task 执行完之后调用 resume
    task.finally([handle]() {
      handle.resume();
    });
  }

  // 协程恢复执行时，被等待的 Task 已经执行完，调用 get_result 来获取结果
  R await_resume() noexcept {
    return task.get_result();
  }

 private:
  Task<R> task;

};
```

当一个 Task 实例被 co_await 表达式使用时，表示当前协程需要等待该异步任务完成。一旦 co_await 表达式返回，就意味着异步任务已经完成，并且协程已经获取到了异步任务的结果。在这种情况下，对于该 Task 实例来说，其生命周期已经结束。

为了避免在协程等待完成后继续对 Task 实例进行操作，TaskAwaiter 的构造函数接收 Task &&（右值引用）参数。

## 同步阻塞获取结果

为了防止 result 被外部随意访问，特意将其改为私有成员。接下来还需要提供相应的方式方便外部访问 result。

```cpp
template<typename ResultType>
struct TaskPromise {
  ...

  void unhandled_exception() {
    std::lock_guard lock(completion_lock);
    result = Result<ResultType>(std::current_exception());
    // 通知 get_result 当中的 wait
    completion.notify_all();
  }

  void return_value(ResultType value) {
    std::lock_guard lock(completion_lock);
    result = Result<ResultType>(std::move(value));
    // 通知 get_result 当中的 wait
    completion.notify_all();
  }

  ResultType get_result() {
    // 如果 result 没有值，说明协程还没有运行完，等待值被写入再返回
    std::unique_lock lock(completion_lock);
    if (!result.has_value()) {
      // 等待写入值之后调用 notify_all
      completion.wait(lock);
    }
    // 如果有值，则直接返回（或者抛出异常）
    return result->get_or_throw();
  }

 private:
  std::optional<Result<ResultType>> result;
  std::mutex completion_lock;
  std::condition_variable completion;
}
```

## 异步结果回调

对比同步回调，分为返回结果的回调和返回异常的回调，对于功能上需要支持回调函数的注册和回调的调用。

```cpp
template<typename ResultType>
struct TaskPromise {
  ...
  void unhandled_exception() {
    std::lock_guard lock(completion_lock);
    result = Result<ResultType>(std::current_exception());
    completion.notify_all();
    // 调用回调
    notify_callbacks();
  }

  void return_value(ResultType value) {
    std::lock_guard lock(completion_lock);
    result = Result<ResultType>(std::move(value));
    completion.notify_all();
    // 调用回调
    notify_callbacks();
  }

  void on_completed(std::function<void(Result<ResultType>)> &&func) {
    std::unique_lock lock(completion_lock);
    // 加锁判断 result
    if (result.has_value()) {
      // result 已经有值
      auto value = result.value();
      // 解锁之后再调用 func
      lock.unlock();
      func(value);
    } else {
      // 否则添加回调函数，等待调用
      completion_callbacks.push_back(func);
    }
  }

 private:
  ...

  // 回调列表，允许对同一个 Task 添加多个回调
  std::list<std::function<void(Result<ResultType>)>> completion_callbacks;
  
  void notify_callbacks() {
    auto value = result.value();
    for (auto &callback : completion_callbacks) {
      callback(value);
    }
    // 调用完成，清空回调
    completion_callbacks.clear();
  }

}
```


# Task 的实现

首先实现一个无调度器版本的Task，其实质是单线程的异步。

```cpp
template<typename ResultType>
struct Task {

  // 声明 promise_type 为 TaskPromise 类型
  using promise_type = TaskPromise<ResultType>;

  ResultType get_result() {
    return handle.promise().get_result();
  }

  Task &then(std::function<void(ResultType)> &&func) {
    handle.promise().on_completed([func](auto result) {
      try {
        func(result.get_or_throw());
      } catch (std::exception &e) {
        // 忽略异常
      }
    });
    return *this;
  }

  Task &catching(std::function<void(std::exception &)> &&func) {
    handle.promise().on_completed([func](auto result) {
      try {
        // 忽略返回值
        result.get_or_throw();
      } catch (std::exception &e) {
        func(e);
      }
    });
    return *this;
  }

  Task &finally(std::function<void()> &&func) {
    handle.promise().on_completed([func](auto result) { func(); });
    return *this;
  }

  explicit Task(std::coroutine_handle<promise_type> handle) noexcept: handle(handle) {}

  Task(Task &&task) noexcept: handle(std::exchange(task.handle, {})) {}
  
  Task(Task &) = delete;
  Task &operator=(Task &) = delete;

  ~Task() {
    if (handle) handle.destroy();
  }

 private:
  std::coroutine_handle<promise_type> handle;
};
```

# 整体分析

在这个异步任务Task中，调用链条遵循以下步骤：

定义返回Task模板的函数，Task模板中存在promise_type成员，其就是一个协程函数。

promise_type中，在initial_suspend中从不挂起，即函数调用时直接执行，在final_suspend总是挂起，其销毁和result(接受了协程返回值)生命周期一致，避免提前销毁出现意外。

使用co_await时，调用了promise_type中await_transform函数，返回了等待体TaskAwaiter（参数类型是co_await后面跟着的类型）。promise_type其中封装了变量Task<Result> task。之后协程恢复，调用等待体的await_resume，返回了等待体的值。


协程函数中最后使用co_return，调用promise_type中的return_value，完成返回值的传递。


# 结果展示




# 完整代码

```cpp
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
```

创建了一个名为 simpleTask 的异步任务对象，该对象是通过调用 simple_task() 函数创建的。这个函数内部包含了一系列协程操作，通过 co_await 关键字等待其他异步任务的完成，并在异步任务完成后返回结果。

simpleTask.then([](int i) { /* 回调函数 */ })：然后，使用 then() 方法添加了一个回调函数，该回调函数将在异步任务成功完成后被调用，接受异步任务的结果作为参数。

simpleTask.catchin([](std::exception& e) { /* 异常处理回调函数 */ })：此外，使用 catching() 方法添加了一个异常处理回调函数，该回调函数将在异步任务抛出异常时被调用，接受抛出的异常对象作为参数。

try { /* 异步等待任务完成并获取结果 */ } 
catch (std::exception& e) { /* 异常处理 */ }：

最后，在 try-catch 块中，使用 simpleTask.get_result() 方法来同步等待异步任务的完成并获取结果。如果任务成功完成，则获取到结果并输出；如果任务抛出异常，则捕获并处理异常。