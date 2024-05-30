
# 特性

一个logger类对象代表一个日志记录器，为用户提供日志记录接口。

每个 logger 对象都有一个唯一的名称，用于标识该 logger。logger 对象维护一个日志等级（如 DEBUG、INFO、WARN、ERROR 等）。只有当日志消息的等级高于或等于 logger 的当前等级时，消息才会被记录下来。

logger 提供了多种接口方法，允许用户记录不同类型的日志消息（如字符串、格式化字符串、异常信息等）。其维护一个 sink 指针数组，指向日志的输出目标（如文件、控制台、远程服务器等）。每个 sink 负责将日志消息写到不同的目标位置。


# 类关系 

log_msg 包含了logger名称、日志等级、记录log时间点、调用处信息，以及负责用户log消息等等，是一条log消息的原始组成部分；

source_loc 包含调用处的文件名、函数名、行数信息；

synchronous_factory 同步工厂，并非logger成员，用于创建非线程安全版本的logger对象；

async_factory，async_factory_nonblock，异步工厂，有2个版本，决定了当线程池缓冲区满时的策略，是阻塞等待 or 丢弃最老的，用于创建线程安全版本的logger对象；

sink 负责将log_msg转换为最终的log字符串，然后写入指定的目标文件。


# logger数据成员

```cpp
protected:
    std::string name_;
    std::vector<sink_ptr> sinks_;
    spdlog::level_t level_{level::info};
    spdlog::level_t flush_level_{level::off};
    err_handler custom_err_handler_{nullptr};
    details::backtracer tracer_;
```

设置2个level_t类型日志等级成员，是为了更精细化控制log消息。
- 当log消息log_msg的日志等级 > level_时，允许log消息写到目标文件（sink）；
- 当log消息log_msg的日志等级 > flush_level_时，允许log消息flush（冲刷）到目标文件（sink）；

当记录日志时，spdlog不会抛出异常。但构造logger或sink对象时，可能发生异常，这被认为是致命的。如果一个错误发生在记录日志时，默认情况下，库将打印一个错误信息到stderr。而custom_err_handler_是便于用户修改默认的错误处理。

tracer_ 用一个环形队列，记录最近的几个log message。

# logger函数成员

## 构造函数

根据是否传入sink对象，构造函数分为两类：
- 空sink对象
- 由调用者传入若干sink对象

sink对象有多种形式：单个sink对象、迭代器表示的范围、初始化列表。

```cpp
public:
    // Empty logger
    explicit logger(std::string name)
        : name_(std::move(name))
        , sinks_()
    {}
    // Logger with range on sinks
    template<typename It>
    logger(std::string name, It begin, It end)
        : name_(std::move(name))
        , sinks_(begin, end)
    {}
    // Logger with single sink
    logger(std::string name, sink_ptr single_sink)
        : logger(std::move(name), {std::move(single_sink)})
    {}
    // Logger with sinks init list
    logger(std::string name, sinks_init_list sinks)
        : logger(std::move(name), sinks.begin(), sinks.end())
    {}
    virtual ~logger() = default;
    logger(const logger &other);
    logger(logger &&other) SPDLOG_NOEXCEPT;
    logger &operator=(logger other) SPDLOG_NOEXCEPT;
```

析构函数使用default（编译器自动合成的），同时定义virtual，防止子类析构不完整。

拷贝构造就是正常写法。移动构造是避免即将释放的对象重复构造，也就是说，如果一个对象即将释放，用它来构造另一个对象的行为就可以改成移动构造。

```cpp
// public methods
// copy ctor
SPDLOG_INLINE logger::logger(const logger &other)
    : name_(other.name_)
    , sinks_(other.sinks_)
    , level_(other.level_.load(std::memory_order_relaxed))
    , flush_level_(other.flush_level_.load(std::memory_order_relaxed))
    , custom_err_handler_(other.custom_err_handler_)
    , tracer_(other.tracer_)
{}

// move ctor
SPDLOG_INLINE logger::logger(logger &&other) SPDLOG_NOEXCEPT
    : name_(std::move(other.name_))
    , sinks_(std::move(other.sinks_))
    , level_(other.level_.load(std::memory_order_relaxed))
    , flush_level_(other.flush_level_.load(std::memory_order_relaxed))
    , custom_err_handler_(std::move(other.custom_err_handler_))
    , tracer_(std::move(other.tracer_))
{}
```

## 交换操作

交换操作并没有使用通用的std::swap，因为通用的swap会构造一个新的临时对象，然后再赋值。logger实现了重载版本的swap函数：
- 对于基本类型，swap操作是直接赋值；
- 对于对象类型，优先调用对象的swap成员函数，最后才是调用通用swap操作；
- 对于原子类型，使用专用的赋值或者交换函数；

```cpp
// swap成员函数
SPDLOG_INLINE void logger::swap(spdlog::logger &other) SPDLOG_NOEXCEPT
{
    name_.swap(other.name_);
    sinks_.swap(other.sinks_);

    // swap level_
    auto other_level = other.level_.load();
    auto my_level = level_.exchange(other_level);
    other.level_.store(my_level);

    // swap flush level_
    other_level = other.flush_level_.load();
    my_level = flush_level_.exchange(other_level);
    other.flush_level_.store(my_level);

    custom_err_handler_.swap(other.custom_err_handler_);
    std::swap(tracer_, other.tracer_);
}

// 重载swap函数
SPDLOG_INLINE void swap(logger &a, logger &b)
{
    a.swap(b);
}
```

## log()记录日志消息

记录日志消息操作的目的是接受用户输入的log消息，构造一个log_msg对象，然后交给所拥有的每个sink对象，从而将log消息写到目标文件上。

```cpp
// 参数完整的记录日志接口
// 用户输入的是变长参数args
template<typename... Args>
void log(source_loc loc, level::level_enum lvl, format_string_t<Args...> fmt,  Args &&... args)
{
    log_(loc, lvl, fmt, std::forward<Args>(args)...); // 转发给private接口log_
}
```

为了简化接口，spdlog使用一组参数使用了默认值的log的重载函数，为用户提供记录日志接口。它们都调用了参数完整版的log<...>()

```cpp
template<typename... Args>
void log(level::level_enum lvl, format_string_t<Args...> fmt, Args &&... args)
{
    log(source_loc{}, lvl, fmt, std::forward<Args>(args)...); // source_loc为空
}

template<typename T>
void log(level::level_enum lvl, const T &msg)
{
    log(source_loc{}, lvl, msg); // source_loc为空, T类型能转换为格式串
}

//接受无法静态转换为格式字符串的类型的日志记录接口
template<class T, typename  std::enable_if<!is_convertible_to_any_format_string<const T &>::value, int>::type  = 0>
void log(source_loc loc, level::level_enum lvl, const T &msg)
{
    log(loc, lvl, "{}", msg); // source_loc为空, T类型不能转换为格式串, 直接将其转换为字符串
}
```

可以看出，变长参数的log其实是交给log_的来实现的，最终是交给ftm库的vformat_to函数处理了。

```cpp
// common implementation for after templated public api has been resolved
template<typename... Args>
void log_(source_loc loc, level::level_enum lvl, string_view_t fmt, Args &&...  args)
{
    bool log_enabled = should_log(lvl);   // 只有优先级不低于指定优先级的log消息, 才被允许记录
    bool traceback_enabled = tracer_.enabled(); // 是否允许回溯最近的log消息
    if (!log_enabled && !traceback_enabled)
    {
        return;
    }
    SPDLOG_TRY
    {
        memory_buf_t buf; // 二进制缓存
#ifdef SPDLOG_USE_STD_FORMAT
        fmt_lib::vformat_to(std::back_inserter(buf), fmt,  fmt_lib::make_format_args(std::forward<Args>(args)...));
#else
        fmt::detail::vformat_to(buf, fmt,  fmt::make_format_args(std::forward<Args>(args)...));
#endif
        details::log_msg log_msg(loc, name_, lvl, string_view_t(buf.data(),  buf.size()));
        log_it_(log_msg, log_enabled, traceback_enabled);
    }
    SPDLOG_LOGGER_CATCH(loc)
}
SPDLOG_INLINE void logger::log_it_(const spdlog::details::log_msg &log_msg, bool  log_enabled, bool traceback_enabled)
{
    if (log_enabled)
    {
        sink_it_(log_msg); // 将log_msg交给sink
    }
    if (traceback_enabled)
    {
        tracer_.push_back(log_msg); // 环形队列缓存log_msg
    }
}
```

上面是处理的格式串，如果普通字符串也这样处理，效率会很低。logger类提供了更高效的方法。

```cpp
// 用户输入的是普通字符串string_view_t
void log(source_loc loc, level::level_enum lvl, string_view_t msg)
{
    bool log_enabled = should_log(lvl);
    bool traceback_enabled = tracer_.enabled();
    if (!log_enabled && !traceback_enabled)
    {
        return;
    }

    details::log_msg log_msg(loc, name_, lvl, msg);
    log_it_(log_msg, log_enabled, traceback_enabled);
}

// 简化版, 调用者无需指定source_loc
void log(level::level_enum lvl, string_view_t msg)
{
    log(source_loc{}, lvl, msg);
}
```

## sink_it_：将log消息交给sink对象

sink_it_ 方法的主要作用是将日志消息对象传递给每一个 sink，并根据日志等级判断是否需要刷新（flush）日志。

```cpp
SPDLOG_INLINE void logger::sink_it_(const details::log_msg &msg) {
    for (auto &sink : sinks_) {
        if (sink->should_log(msg.level)) {
            SPDLOG_TRY { sink->log(msg); }
            SPDLOG_LOGGER_CATCH(msg.source)
        }
    }

    if (should_flush_(msg)) {
        flush_();
    }
}
SPDLOG_INLINE void logger::flush_() {
    for (auto &sink : sinks_) {
        SPDLOG_TRY { sink->flush(); }
        SPDLOG_LOGGER_CATCH(source_loc())
    }
}
```

## 写日志控制

有2个控制接口。

should_log，控制是否允许写用户传入的log消息，采用策略是log消息本身级别（用户指定） >= logger指定的日志级别（创建者指定）。

should_backtrace，控制是否允许回溯log消息，回溯策略是开启了该功能时，在写log消息同时，会将log消息加入到回溯用的环形队列tracer_中。


```cpp
// return true logging is enabled for the given level.
bool should_log(level::level_enum msg_level) const
{
    return msg_level >= level_.load(std::memory_order_relaxed);
}

// return true if backtrace logging is enabled.
bool should_backtrace() const
{
    return tracer_.enabled();
}
```

## 线程安全

spdlog 的 logger 类通过其数据成员来实现线程安全，但 logger 类本身并不直接提供线程安全保证。

```cpp
protected:
    std::string name_;
    std::vector<sink_ptr> sinks_;
    spdlog::level_t level_{level::info};
    spdlog::level_t flush_level_{level::off};
    err_handler custom_err_handler_{nullptr};
    details::backtracer tracer_;
```

name_ 通常在构造时决定，之后不再修改。

level_ 和 flush_level_是原子变量，这两个原子变量的内存布局使用松散的内存顺序（std::memory_order_relaxed）

```cpp
#if defined(SPDLOG_NO_ATOMIC_LEVELS)
using level_t = details::null_atomic_int;
#else
using level_t = std::atomic<int>;
#endif
```

custom_err_handler_ 不提供线程安全保证。因为它有一个 set 接口（set_error_handler），所以对这个成员的访问需要同步保护，以避免多线程访问时出现竞态条件。

sinks_ 的线程安全性取决于 sink 类。sink 类是一个抽象类，其具体的线程安全性由派生类决定。在 spdlog 中，sink 派生类通过模板参数 Mutex 来决定锁类型。

tracer_ 的线程安全性依赖于 backtracer 类。下面讲解backtracer类。


## backtracer类提供回溯功能

backtracer类通过一个固定大小的环形队列messages_缓存最近log消息，为logger实现回溯log消息。向backtracer插入（push_back）前，必须通过enable()指定环形队列大小，否则环形队列messages_大小为0，无法插入数据。

```cpp
class SPDLOG_API backtracer
{
    mutable std::mutex mutex_;            // 互斥锁
    std::atomic<bool> enabled_{false};    // backtracer使能状态
    circular_q<log_msg_buffer> messages_; // 环形队列

public:
    backtracer() = default; // default ctor
    backtracer(const backtracer &other); // copy ctor

    backtracer(backtracer &&other) SPDLOG_NOEXCEPT; // move ctor
    backtracer &operator=(backtracer other); // operator=

    void enable(size_t size); // 使能backtracer功能, 为环形队列指定大小
    void disable();           // 禁用backtracer, 但不会清除环形队列大小
    bool enabled() const;     // 返回backtracer使能状态
    void push_back(const log_msg &msg); // 向环形队列末尾插入一条log消息

    // pop all items in the q and apply the given fun on each of them.
    void foreach_pop(std::function<void(const details::log_msg &)> fun);
};
```

backtracer使用环形队列有2个比较重要的操作：push_back，向环形队列尾部插入一条log消息。当队列满时，并没有用阻塞等待的策略，而是用的默认的丢弃最老的log消息；

foreach_pop，逐条从环形队列头弹出log消息，并对每个弹出的log消息应用指定的fun函数。通过这种方式，让用户有机会对环形队列中的log消息进行处理。

```cpp
SPDLOG_INLINE void backtracer::foreach_pop(std::function<void(const  details::log_msg &)> fun)
{
    std::lock_guard<std::mutex> lock{mutex_};
    // 从队列messages_ 头逐个弹出log消息，并作为fun参数进行调用
    while (!messages_.empty())
    {
        auto &front_msg = messages_.front();
        fun(front_msg);
        messages_.pop_front();
    }
}
```

logger的转储dump_backtrace_()功能，就是用到了backtracer::foreach_pop，将环形队列中每条log消息都交给sink写到目标文件。该功能对于排查问题时，查看最近的log消息十分有用。

```cpp
SPDLOG_INLINE void logger::dump_backtrace_()
{
    using details::log_msg;
    if (tracer_.enabled())
    {
        sink_it_(log_msg{name(), level::info, "****************** Backtrace Start  ******************"});
        tracer_.foreach_pop([this](const log_msg &msg) { this->sink_it_(msg); });
        sink_it_(log_msg{name(), level::info, "****************** Backtrace End  ********************"});
    }
}
```


# logger类应用

## 创建logger对象

在spdlog中，用户并不直接创建logger对象，而是通过工厂方法根据不同的sink，来创建logger对象。例如，下面代码用工厂方法创建一个logger对象：


```cpp
// Create and return a shared_ptr to a multithread console logger.
#include "spdlog/sinks/stdout_color_sinks.h"
auto console = spdlog::stdout_color_mt("some_unique_name");
```

其函数内部逻辑如下：

```cpp
// stdout_color_mt声明, 模板参数Factory默认使用同步工厂synchronous_factory
template<typename Factory = spdlog::synchronous_factory>
std::shared_ptr<logger> stdout_color_mt(const std::string &logger_name, color_mode  mode = color_mode::automatic);

// stdout_color_mt定义, 使用工厂方法创建logger对象
template<typename Factory>
SPDLOG_INLINE std::shared_ptr<logger> stdout_color_mt(const std::string  &logger_name, color_mode mode)
{
    return Factory::template create<sinks::stdout_color_sink_mt>(logger_name,  mode);
}
```

## 同步工厂方法 synchronous_factory

同步工厂方法用于创建同步日志记录器，使用模板参数来决定创建的 sink 类型，并将其绑定到新建的 logger 对象上。

```cpp
struct synchronous_factory
{
    template<typename Sink, typename... SinkArgs>
    static std::shared_ptr<spdlog::logger> create(std::string logger_name, SinkArgs &&... args)
    {
        auto sink = std::make_shared<Sink>(std::forward<SinkArgs>(args)...);
        auto new_logger = std::make_shared<spdlog::logger>(std::move(logger_name), std::move(sink));
        details::registry::instance().initialize_logger(new_logger);
        return new_logger;
    }
};
```

synchronous_factory的精妙之处在于，函数参数用来创建对象，模板参数用来指定要创建的类型。logger name对于registry全局注册表来说，是唯一标识logger对象的。

## 异步工厂方法async_factory

针对所使用的环形队列，当队列满时，如果插入数据，有两种策略：阻塞、非阻塞，分别对应工厂类型async_factory、async_factory_nonblock，都是通过async_factory_impl来实现的。

```cpp
using async_factory = async_factory_impl<async_overflow_policy::block>;  // 阻塞策略
using async_factory_nonblock =  async_factory_impl<async_overflow_policy::overrun_oldest>;  // 非阻塞策略
//async_factory_impl的实现 include/spdlog/async.h
template<async_overflow_policy OverflowPolicy = async_overflow_policy::block>
struct async_factory_impl
{
    template<typename Sink, typename... SinkArgs>
    static std::shared_ptr<async_logger> create(std::string logger_name, SinkArgs  &&... args)
    {
        auto &registry_inst = details::registry::instance();
        auto &mutex = registry_inst.tp_mutex();
        std::lock_guard<std::recursive_mutex> tp_lock(mutex);
        auto tp = registry_inst.get_tp();
        if (tp == nullptr)
        {
            tp =  std::make_shared<details::thread_pool>(details::default_async_q_size, 1U);
            registry_inst.set_tp(tp);
        }

        auto sink = std::make_shared<Sink>(std::forward<SinkArgs>(args)...);
        // 创建新async_logger对象同时, 绑定线程池
        auto new_logger = std::make_shared<async_logger>(std::move(logger_name),  std::move(sink), std::move(tp), OverflowPolicy);
        registry_inst.initialize_logger(new_logger);
        return new_logger;
    }
}
```

跟同步工厂方法最大的区别是：异步工厂方法，是依附于一个（registry单例管理的）全局线程池的。创建出来的logger对象真实类型是派生类async_logger。而async_logger通过一个弱指针指向线程池。

上面的只是工厂的类型，并非工厂方法。用户想要利用工厂方法创建对象，需要用到下面的create_async, create_async_nb方法：

```cpp
// 采用阻塞策略的异步工厂方法
template<typename Sink, typename... SinkArgs>
inline std::shared_ptr<spdlog::logger> create_async(std::string logger_name,  SinkArgs &&... sink_args)
{
    return async_factory::create<Sink>(std::move(logger_name),  std::forward<SinkArgs>(sink_args)...);
}

// 采用非阻塞策略的异步工厂方法
template<typename Sink, typename... SinkArgs>
inline std::shared_ptr<spdlog::logger> create_async_nb(std::string logger_name,  SinkArgs &&... sink_args)
{
    return async_factory_nonblock::create<Sink>(std::move(logger_name),  std::forward<SinkArgs>(sink_args)...);
}
```

在客户端，比如你想创建一个basic_logger_mt，即一个基本都用于多线程环境的async_logger，可以这样封装工厂方法，然后供APP调用：

```cpp
// include/spdlog/sinks/basic_file_sink.h

// 封装工厂方法，供APP调用
// factory functions
template<typename Factory = spdlog::synchronous_factory>
inline std::shared_ptr<logger> basic_logger_mt(
    const std::string &logger_name, const filename_t &filename, bool truncate =  false, const file_event_handlers &event_handlers = {})
{
    return Factory::template create<sinks::basic_file_sink_mt>(logger_name,  filename, truncate, event_handlers);
}

// APP端创建async_logger对象
// spdlog::init_thread_pool(32768, 1); // queue with max 32k items 1 backing  thread.
auto async_file =  spdlog::basic_logger_mt<spdlog::async_factory>("async_file_logger",  "logs/async_log.txt");
```

## 获取logger对象

spdlog中，使用工厂方法创建的logger对象，会自动注册到全局注册表registry，便于查询、管理。可用spdlog::get()方法获取已注册的loggers。

例如，创建名为"some_logger"的logger对象，并用spdlog::get获取：

```cpp
auto my_logger = spdlog::basic_logger_mt("some_logger"); // 使用默认的同步工厂方法
...
auto some_logger = spdlog::get("some_logger");
```

## 使用logger对象

获取到logger对象后，就能调用对应public接口了，譬如调用trace/log等接口就可以写log消息了。

例如，下面代码往日志文件（"logs/async_log.txt"）写内容"Async message #a"。

```cpp
auto async_file = spdlog::basic_logger_mt<spdlog::async_factory>("async_file_logger", "logs/async_log.txt");
int a = 10;
async_file->info("Async message #{}", a);
```

# async_logger类

async_logger类是logger类的派生类，专门用于接收用户log消息，然后交给线程池异步写入目标文件。用户提交log消息的线程，称为前端线程；将log消息写到目标文件的线程，称为后端线程。

## async_logger数据成员

async_logger并非线程池的创建者，而线程池会用到logger的共享指针，而该指针可能指向async_logger对象，因此，async_logger使用thread_pool的弱指针。

在通过线程池往环形队列添加log消息时，可以指明所需的阻塞策略。async_logger给了调用者在构造时，就指定阻塞策略的机会，通过数据成员overflow_policy_记录。

```cpp
private:
    std::weak_ptr<details::thread_pool> thread_pool_;
    async_overflow_policy overflow_policy_;         // 环形队列满时 阻塞策略
```

## 前端接收log消息

从前端线程接收用户log消息，然后将其交给线程池；线程池空闲时，会调用async_logger来处理当前log消息，将其写到目标文件（sink）。

protected方法sink_it_就是用于前端线程，将接收到的用户log消息转交给线程池；flush_是向线程池发送一条flush异步消息，通知线程池尽早将log消息写到目标文件。

```cpp
// send the log message to the thread pool
SPDLOG_INLINE void spdlog::async_logger::sink_it_(const details::log_msg &msg)
{
    if (auto pool_ptr = thread_pool_.lock())
    {
        pool_ptr->post_log(shared_from_this(), msg, overflow_policy_); 
        // 将log消息转交给线程池
    }
    else
    {
        throw_spdlog_ex("async log: thread pool doesn't exist anymore");
    }
}

// send flush request to the thread pool
SPDLOG_INLINE void spdlog::async_logger::flush_()
{
    if (auto pool_ptr = thread_pool_.lock())
    {
        pool_ptr->post_flush(shared_from_this(), overflow_policy_); // 发送一条flush消息给线程池, 将缓存内容尽早flush到文件
    }
    else
    {
        throw_spdlog_ex("async flush: thread pool doesn't exist anymore");
    }
}
```

## 后端写log消息

backend_sink_it_和backend_flush_是运行于后端线程（线程池子线程），分别对应前端任务sink_it_和flush_。

```cpp
// backend functions - called from the thread pool to do the actual job
SPDLOG_INLINE void spdlog::async_logger::backend_sink_it_(const details::log_msg  &msg)
{
    for (auto &sink : sinks_)
    {
        if (sink->should_log(msg.level))
        {
            SPDLOG_TRY
            {
                sink->log(msg); // 将log消息交给sink对象，写到目标文件
            }
            SPDLOG_LOGGER_CATCH(msg.source)
        }
    }

    if (should_flush_(msg)) // 如果允许的话，自动在后端flush
    {
        backend_flush_();
    }
}

SPDLOG_INLINE void spdlog::async_logger::backend_flush_()
{
    for (auto &sink : sinks_)
    {
        SPDLOG_TRY
        {
            sink->flush(); // 通知sink冲刷缓存到文件
        }
        SPDLOG_LOGGER_CATCH(source_loc())
    }
}
```


# 小结

**模板参数指定类型**：使用模板参数指定 Sink 类型，允许创建不同类型的 sink，如文件 sink、控制台 sink 等。这个设计使得一个工厂方法可以创建多种类型的日志记录器。其余参数完美转发，确保类型安全地传递构造函数参数，避免了类型转换错误。

**同步和异步工厂方法统一接口**：提供了统一的接口，用户可以通过简单的模板参数指定来创建不同类型的日志记录器

