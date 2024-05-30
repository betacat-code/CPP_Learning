# 自定义异常类spdlog_ex

标准库异常类（std::exception）系列，能满足大多数使用异常的场景，但对系统调用异常及错误信息缺乏支持。spdlog通过继承std::exception，扩展对系统调用的支持，实现自定义异常类spdlog_ex。

spdlog_ex类声明只是在std::exception基础上添加了string类型的msg_成员，提供支持errno的构造函数。

```cpp
// include/spdlog/common.h
class SPDLOG_API spdlog_ex : public std::exception {
public:
    explicit spdlog_ex(std::string msg);
    spdlog_ex(const std::string &msg, int last_errno);
    const char *what() const SPDLOG_NOEXCEPT override;

private:
    std::string msg_;
};
```

## 异常

```cpp
SPDLOG_INLINE spdlog_ex::spdlog_ex(std::string msg)
    : msg_(std::move(msg)) {}

SPDLOG_INLINE spdlog_ex::spdlog_ex(const std::string &msg, int last_errno) {
#ifdef SPDLOG_USE_STD_FORMAT
    msg_ = std::system_error(std::error_code(last_errno, std::generic_category()), msg).what();
#else
    memory_buf_t outbuf;
    fmt::format_system_error(outbuf, last_errno, msg.c_str());
    msg_ = fmt::to_string(outbuf);
#endif
```
对于通用的异常，spdlog_ex只是将用户传入的异常提示信息存放到msg_。

spdlog_ex对errno的支持，主要是将errno转换为对应错误文本信息，存放到msg_字符串中。spdlog使用的是ftm库提供的format_system_error来完成转换工作。

## what()函数

```cpp
SPDLOG_INLINE const char *spdlog_ex::what() const SPDLOG_NOEXCEPT {
    return msg_.c_str(); 
}
```
what()是基类std::exception定义的virtual函数，用户通常通过该接口获取异常信息。spdlog_ex返回存放异常信息的msg_。


# 异常的使用

spdlog提供了重载函数形式的接口：throw_spdlog_ex。

```cpp
SPDLOG_INLINE void throw_spdlog_ex(const std::string &msg, int last_errno) {
    SPDLOG_THROW(spdlog_ex(msg, last_errno));
}

SPDLOG_INLINE void throw_spdlog_ex(std::string msg) { 
    SPDLOG_THROW(spdlog_ex(std::move(msg))); 
}
```

抛出异常可以是这样：

```cpp
// send flush request to the thread pool
SPDLOG_INLINE void spdlog::async_logger::flush_(){
    SPDLOG_TRY{
        auto pool_ptr = thread_pool_.lock();
        if (!pool_ptr) {
            throw_spdlog_ex("async flush: thread pool doesn't exist anymore");
        }
        std::future<void> future = pool_ptr->post_flush(shared_from_this(), overflow_policy_);
        future.get();
    }
    SPDLOG_LOGGER_CATCH(source_loc())
}
```

throw_spdlog_ex本质上也是throw spdlog_ex(..)，这里多了宏定义SPDLOG_THROW。

```cpp
#ifdef SPDLOG_NO_EXCEPTIONS
    #define SPDLOG_TRY
    #define SPDLOG_THROW(ex)                               \
        do {                                               \
            printf("spdlog fatal error: %s\n", ex.what()); \
            std::abort();                                  \
        } while (0)
    #define SPDLOG_CATCH_STD
#else
    #define SPDLOG_TRY try
    #define SPDLOG_THROW(ex) throw(ex)
    #define SPDLOG_CATCH_STD             \
        catch (const std::exception &) { \
        }
#endif
```

实际上是提供了两种模式。抛出异常，不抛出异常。
- 当没有定义宏SPDLOG_NO_EXCEPTIONS时，正常抛出异常对象；
- 当定义了宏SPDLOG_NO_EXCEPTIONS时，抛出异常替换为直接终止程序（abort）


因此，在spdlog中，捕获异常的代码块try-catch，看起来会是这样：

```cpp
SPDLOG_INLINE thread_pool::~thread_pool()
{
    // 析构函数不要抛出异常, 但释放线程池资源资源可能发生异常, 因此内部捕获并处理
    SPDLOG_TRY
    {
        for (size_t i = 0; i < threads_.size(); i++) {
            // 有几个子线程，就要post几个terminate的async_msg。
            post_async_msg_(async_msg(async_msg_type::terminate), async_overflow_policy::block);
        }

        for (auto & t : threads_) {
            t.join();
        }
    }
    SPDLOG_CATCH_STD
}
```

可以使用自定义捕获（catch）代码块，替换SPDLOG_CATCH_STD。

```cpp
SPDLOG_INLINE void spdlog::async_logger::backend_flush_()
{
    for (auto &sink : sinks_)
    {
        SPDLOG_TRY
        {
            sink->flush();
        }
        SPDLOG_LOGGER_CATCH(source_loc())
    }
}
#ifndef SPDLOG_NO_EXCEPTIONS
    #define SPDLOG_LOGGER_CATCH(location)                                                 \
        catch (const std::exception &ex) {                                                \
            if (location.filename) {                                                      \
                err_handler_(fmt_lib::format(SPDLOG_FMT_STRING("{} [{}({})]"), ex.what(), \
                                             location.filename, location.line));          \
            } else {                                                                      \
                err_handler_(ex.what());                                                  \
            }                                                                             \
        }                                                                                 \
        catch (...) {                                                                     \
            err_handler_("Rethrowing unknown exception in logger");                       \
            throw;                                                                        \
        }
#else
    #define SPDLOG_LOGGER_CATCH(location)
#endif
```

catch (const std::exception &ex)：捕获所有从 std::exception 派生的异常。

if (location.filename)：如果提供了源代码位置信息（文件名），则格式化错误消息，包括异常的 what() 信息和源代码位置。

err_handler_：这是一个错误处理函数或对象，用于处理和记录错误消息。

catch (...)：捕获所有其他类型的异常。

err_handler_("Rethrowing unknown exception in logger")：记录一个未知异常的错误消息。

throw：重新抛出捕获的异常。

如果禁用了异常处理（通过定义 SPDLOG_NO_EXCEPTIONS），这个宏展开为空。
