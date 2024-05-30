
spdlog 中的 formatter 类用于定义日志消息的格式化规则。它负责将日志记录器接收到的日志消息按照预定义的格式转换成最终的输出字符串。这使得开发者可以灵活地控制日志的输出格式，包括时间戳、日志等级、消息内容等信息。

# 相关概念

Scope 指的是一个日志模式标记（pattern flag）对应的字段显示宽度范围。当使用固定宽度的 pattern flag（如 "%8X"、"%-8X"、"%=8!X"）时，该字段有一个特定的宽度，即 scope。如果 pattern flag 没有指定宽度，那么它被称为 null-scope。

Pad 是指在字段显示宽度不足时，用特定字符（通常是空格）进行填充的过程。填充器（padder）是执行填充操作的类。包含填充信息的类被称为 padding_info，这个类包含有关如何进行填充的详细信息。

Pattern flag 是日志格式化字符串中的占位符，表示要显示的特定字段。例如，"%8X" 表示一个宽度为 8 的字段，"%-8X" 表示一个左对齐的宽度为 8 的字段，"%=8!X" 表示一个居中的宽度为 8 的字段。


# formatter类

logger对象负责接收用户log消息，sink对象负责将log消息写到目标文件。每个sink对象拥有一个formatter类对象的成员，用于将原始的log消息格式化为字符串。pattern（模式串）用于指定期望的格式，在formatter构造时传入。

formatter是一个接口类，规定派生类必须实现的接口。

format接口：格式化原始的log消息, 将转换结果存放到dest二进制缓存；
clone接口：克隆一个当前formatter类对象，注意clone的对象由unique_ptr管理，无需手动释放内存；

```cpp
// include/spdlog/formatter.h
class formatter {
public:
    virtual ~formatter() = default;
    virtual void format(const details::log_msg &msg, memory_buf_t &dest) = 0;
    virtual std::unique_ptr<formatter> clone() const = 0;
};
```

在日志记录中，不同的 sink 可能需要使用相同的格式化规则。例如，一个日志记录器可能同时向控制台和文件输出日志，这两个输出需要相同的格式（如时间戳、日志等级、消息内容等）。

如果允许共享相同的 formatter 实例，任何对 formatter 的修改都会影响所有共享该实例的 sink，这可能导致不一致的日志格式。

通过 clone() 方法，可以创建 formatter 对象的独立副本。这些副本共享相同的格式化规则，但它们是独立的对象实例，修改其中一个不会影响其他。

# pattern_formatter类

pattern_formatter 类在 spdlog 库中负责根据用户指定的模式字符串（pattern）对日志消息进行格式化，并将格式化后的结果存储到二进制缓存中。这个类有两个主要功能：
- 解析用户模式字符串（compile_pattern_）：解析用户提供的模式字符串，将其分解成若干模式标志和普通字符串。
- 格式化日志消息（format）：根据解析后的模式标志和普通字符串，对日志消息进行格式化处理。

模式字符串描述了最终日志消息的格式，包括哪些内容及其顺序。它由普通字符串和模式标志组成，模式标志以 "%" 开头。例如：%v 表示实际要记录的文本。%t 表示线程 ID。%P 表示进程 ID。%+（缺省模式）表示一个完整的日志格式，如 [2022-10-31 23:46:59.678] [mylogger] [info] Some message。

一个完整的pattern flag，包含宽度（width）、对齐方式（alignment）、截断（truncate）、模式标志（flag），语法格式如下：

```cpp
%<alignment><width>!<flag>
```

```cpp
// 根据pattern对log消息进行格式化
class SPDLOG_API pattern_formatter final : public formatter
{
public:
    // 用户自定义模式标志, 每个字符对应一个自定义标志格式custom_flag_formatter对象, 每个custom_flag_formatter对应一个模式标志
    using custom_flags = std::unordered_map<char,  std::unique_ptr<custom_flag_formatter>>;
    
    // 由调用者决定pattern
    explicit pattern_formatter(std::string pattern, pattern_time_type time_type =  pattern_time_type::local,
        std::string eol = spdlog::details::os::default_eol, custom_flags  custom_user_flags = custom_flags());
    
    // 使用默认的pattern(%+)
    // default_eol 是行结束符, 两种风格: "\r\n"(Windows), "\n"(Unix)
    // use default pattern is not given
    explicit pattern_formatter(pattern_time_type time_type =  pattern_time_type::local, std::string eol = spdlog::details::os::default_eol);

    // 禁用copy操作
    pattern_formatter(const pattern_formatter &other) = delete;
    pattern_formatter &operator=(const pattern_formatter &other) = delete;

    // 实现父类pure virtual函数, 克隆对象
    std::unique_ptr<formatter> clone() const override;
    void format(const details::log_msg &msg, memory_buf_t &dest) override;

    // 添加模式标志flag
    template<typename T, typename... Args>
    pattern_formatter &add_flag(char flag, Args &&... args);

    // 设置模式串
    void set_pattern(std::string pattern);
    // 设置是否需要localtime (对应need_localtime_的值)
    void need_localtime(bool need = true);

private:
    std::string pattern_; // 模式串
    std::string eol_;     // 行结束符
    pattern_time_type pattern_time_type_; // 模式时间类型: local or utc time
    bool need_localtime_; // 决定用户可指定最终输出log消息的时间是使用本地时间, 还是使用log消息自带的时间(log_msg构造时决定)
    std::tm cached_tm_;   // 搭配need_localtime_使用, 用于缓存最终输出的log消息时间
    std::chrono::seconds last_log_secs_;  // 最近一次写log的时间(秒)
    std::vector<std::unique_ptr<details::flag_formatter>> formatters_; // 标志格式数组
    custom_flags custom_handlers_;        // 用户自定义模式标志

    std::tm get_time_(const details::log_msg &msg); // 获取当前时间, 由pattern_time_type_决定local time or utc time
    template<typename Padder>
    void handle_flag_(char flag, details::padding_info padding);      // 处理模式标志字符flag

    // 提取给定pad规范, 如%8X, 返回padding_info
    // Extract given pad spec (e.g. %8X)
    // Advance the given it pass the end of the padding spec found (if any)
    // Return padding.
    static details::padding_info handle_padspec_(std::string::const_iterator &it,  std::string::const_iterator end);
    
    // 编译pattern, 抽取出模式标志
    void compile_pattern_(const std::string &pattern);
};
```

## 构造与析构

pattern_formatter定义了2个版本的构造函数：第一个用于用户指定模式串pattern的情形，第二个用于用户未指定的情形。当用户未指定pattern时，使用默认模式串"%+"，输出log形如"[2022-10-31 23:46:59.678] [mylogger] [info] Some message"。

也就是说，构造时需要指定pattern，然后调用compile_pattern_对pattern进行“编译”（即解析）。

```cpp
// 根据用户指定pattern构造pattern_formatter对象
SPDLOG_INLINE pattern_formatter::pattern_formatter(
    std::string pattern, pattern_time_type time_type, std::string eol,  custom_flags custom_user_flags)
    : pattern_(std::move(pattern))
    , eol_(std::move(eol))
    , pattern_time_type_(time_type)
    , need_localtime_(false)
    , last_log_secs_(0)
    , custom_handlers_(std::move(custom_user_flags))
{
    std::memset(&cached_tm_, 0, sizeof(cached_tm_));
    compile_pattern_(pattern_); // 编译pattern
}

// 使用默认的pattern构造pattern_formatter对象
// use by default full formatter for if pattern is not given
SPDLOG_INLINE pattern_formatter::pattern_formatter(pattern_time_type time_type,  std::string eol)
    : pattern_("%+")
    , eol_(std::move(eol))
    , pattern_time_type_(time_type)
    , need_localtime_(true)
    , last_log_secs_(0)
{
     std::memset(&cached_tm_, 0, sizeof(cached_tm_));
     // full_formatter对应pattern: [%Y-%m-%d %H:%M:%S.%e] [%n] [%l] [%s:%#] %v
     formatters_.push_back(details::make_unique<details::full_formatter>(details::padding_info{}));
}
```

## compile_pattern_编译pattern

attern_formatter::compile_pattern_ 函数的详细解析。这个函数的作用是解析用户提供的模式字符串（pattern），并将其转换为一组 formatter 对象，以便后续对日志消息进行格式化处理。

```cpp
SPDLOG_INLINE void pattern_formatter::compile_pattern_(const std::string &pattern)
{
    auto end = pattern.end();
    // 用于存放普通用户字符串
    std::unique_ptr<details::aggregate_formatter> user_chars;
    formatters_.clear();

    // 逐字符遍历pattern
    for (auto it = pattern.begin(); it != end; ++it)
    {
        // 处理跟着"%"的字符串
        if (*it == '%')
        {
            // 将目前为止的普通用户字符加入formatters_
            if (user_chars)
            {
                formatters_.push_back(std::move(user_chars));
            }
            // 按pad规范处理模式标志, 得到padding_info, 需要指定<width>
            // 注意handle_padspec_会改变迭代器it
            auto padding = handle_padspec_(++it, end);
            if (it != end) // 此时it指向的模式标志 非空
            {
                if (padding.enabled()) // 有效的pad
                {
                    handle_flag_<details::scoped_padder>(*it, padding);
                }
                else // 无效的pad
                {
                    handle_flag_<details::null_scoped_padder>(*it, padding);
                }
            }
            else // "%"之后无模式标志, 出错, 无需继续
            {
                break;
            }
        }
        // 处理未跟着"%"符号的字符
        else
        {
            if (!user_chars)
            {
                user_chars = details::make_unique<details::aggregate_formatter>();
            }
            user_chars->add_ch(*it);
        }
    }
    // 不要忘了最后一个"%"后的字符串
    if (user_chars)
    {
        formatters_.push_back(std::move(user_chars));
    }
}
```

## handle_padspec_按pad规格处理

在上述过程中出现了handle_padspec_函数，其负责解析填充规范（padding spec），并返回包含填充信息的 padding_info 对象。

```cpp
// 按pad规格, 逐字符解析模式标志, 即%后的3段: <alignment><width>[!]<flag>
// handle_padspec_可能改变迭代器it, 正常时, 最终指向<flag>或者end; 或者中间出错, 如没有指定<width>

SPDLOG_INLINE details::padding_info  pattern_formatter::handle_padspec_(std::string::const_iterator &it,  std::string::const_iterator end)
{
    using details::padding_info;
    using details::scoped_padder;
    const size_t max_width = 64;
    if (it == end) // 迭代器范围为空, 就返回空的padding_info
    {
        return padding_info{}; 
    }

    // 判断对齐方式<alignment>
    padding_info::pad_side side;
    switch (*it)
    {
    case '-': // 右对齐
        side = padding_info::pad_side::right;
        ++it;
        break;
    case '=': // 中对齐
        side = padding_info::pad_side::center;
        ++it;
        break;
    default: // 左对齐
        side = details::padding_info::pad_side::left;
        break;
    }

    // 非数字就返回空padding_info
    // 没有指定<width>, 就无所谓对齐, 也无所谓填充
    if (it == end || !std::isdigit(static_cast<unsigned char>(*it)))
    {
        return padding_info{}; // no padding if no digit found here
    }

    // 宽度<width>
    auto width = static_cast<size_t>(*it) - '0';
    // 将数字字符串转换为十进制数
    for (++it; it != end && std::isdigit(static_cast<unsigned char>(*it)); ++it)
    {
        auto digit = static_cast<size_t>(*it) - '0';
        width = width * 10 + digit;
    }

    // 截断标记[!]
    bool truncate;
    if (it != end && *it == '!')
    {
        truncate = true;
        ++it;
    }
    else
    {
        truncate = false;
    }
    // 此时it指向最后的<flag>
    // 返回当前pad对应的padding_info
    return details::padding_info{std::min<size_t>(width, max_width), side,  truncate};
}
```

## handle_flag_ 处理模式标志字符

该函数根据传入的模式标志字符（flag）和填充信息（padding），为每个模式标志生成相应的 flag_formatter 对象，并将其存入 formatters_ 数组中。

handle_flag_是转发给flag_formatter子类模板，在子类模板的接口format()实现中利用Padder的构造与析构，完成左侧、右侧空格填充。

Padder 会有两个主要实现：

- scoped_padder：用于处理有效的填充，执行实际的空格填充。
- null_scoped_padder：用于处理无效的填充，不执行任何填充操作。

```cpp
// Padder: 转发给formatter, 用类RAII方法填充空格的类型
// flag: 模式标志字符, 如"%-8!X" 中的'X'
// padding: 填充信息
template<typename Padder>
SPDLOG_INLINE void pattern_formatter::handle_flag_(char flag,  details::padding_info padding)
{
    // 处理用户自定义的flag, 会clone一个custom_flag_formatter对象, 加入formatters_数组
    // process custom flags
    auto it = custom_handlers_.find(flag);
    if (it != custom_handlers_.end())
    {
        auto custom_handler = it->second->clone(); // clone一个自定义flag_formatter对象(custom_flag_formatter类型)
        custom_handler->set_padding_info(padding); // 设置padding_info属性
        formatters_.push_back(std::move(custom_handler));
        return;
    }

    // 处理内置flag
    // process built-in flags
    switch (flag)
    {
    case ('+'): // default formatter
        // spdlog默认format, i.e. "[2022-10-31 23:46:59.678] [mylogger] [info] Some message"
         formatters_.push_back(details::make_unique<details::full_formatter>(padding));
        need_localtime_ = true;
        break;
    case 'n': // logger name
         formatters_.push_back(details::make_unique<details::name_formatter<Padder>>(padding)); // Padder转发给name_formatter
        break;
    case 'l': // level
         formatters_.push_back(details::make_unique<details::level_formatter<Padder>>(padding)); // Padder转发给level_formatter
        break;
    case 'L': // short level
         formatters_.push_back(details::make_unique<details::short_level_formatter<Padder>>(padding));
        break;
    case ('t'): // thread id
         formatters_.push_back(details::make_unique<details::t_formatter<Padder>>(padding));
        break;
    case ('v'): // the message text
         formatters_.push_back(details::make_unique<details::v_formatter<Padder>>(padding));
        break;
    case ('a'): // weekday
         formatters_.push_back(details::make_unique<details::a_formatter<Padder>>(padding));
        need_localtime_ = true;
        break;
    case ('A'): // short weekday
         formatters_.push_back(details::make_unique<details::A_formatter<Padder>>(padding));
        need_localtime_ = true;
        break;
    case ('b'):
    case ('h'): // month
         formatters_.push_back(details::make_unique<details::b_formatter<Padder>>(padding));
        need_localtime_ = true;
        break;
    case ('B'): // short month
         formatters_.push_back(details::make_unique<details::B_formatter<Padder>>(padding));
        need_localtime_ = true;
        break;
    case ('c'): // datetime
         formatters_.push_back(details::make_unique<details::c_formatter<Padder>>(padding));
        need_localtime_ = true;
        break;
    case ('C'): // year 2 digits
         formatters_.push_back(details::make_unique<details::C_formatter<Padder>>(padding));
        need_localtime_ = true;
        break;
    case ('Y'): // year 4 digits
         formatters_.push_back(details::make_unique<details::Y_formatter<Padder>>(padding));
        need_localtime_ = true;
        break;
    case ('D'):
    case ('x'): // datetime MM/DD/YY
         formatters_.push_back(details::make_unique<details::D_formatter<Padder>>(padding));
        need_localtime_ = true;
        break;
    case ('m'): // month 1-12
         formatters_.push_back(details::make_unique<details::m_formatter<Padder>>(padding));
        need_localtime_ = true;
        break;
    case ('d'): // day of month 1-31
         formatters_.push_back(details::make_unique<details::d_formatter<Padder>>(padding));
        need_localtime_ = true;
        break;
    case ('H'): // hours 24
         formatters_.push_back(details::make_unique<details::H_formatter<Padder>>(padding));
        need_localtime_ = true;
        break;
    case ('I'): // hours 12
         formatters_.push_back(details::make_unique<details::I_formatter<Padder>>(padding));
        need_localtime_ = true;
        break;
    case ('M'): // minutes
         formatters_.push_back(details::make_unique<details::M_formatter<Padder>>(padding));
        need_localtime_ = true;
        break;
    case ('S'): // seconds
         formatters_.push_back(details::make_unique<details::S_formatter<Padder>>(padding));
        need_localtime_ = true;
        break;
    case ('e'): // milliseconds
         formatters_.push_back(details::make_unique<details::e_formatter<Padder>>(padding));
        break;
    case ('f'): // microseconds
         formatters_.push_back(details::make_unique<details::f_formatter<Padder>>(padding));
        break;
    case ('F'): // nanoseconds
         formatters_.push_back(details::make_unique<details::F_formatter<Padder>>(padding));
        break;
    case ('E'): // seconds since epoch
         formatters_.push_back(details::make_unique<details::E_formatter<Padder>>(padding));
        break;
    case ('p'): // am/pm
         formatters_.push_back(details::make_unique<details::p_formatter<Padder>>(padding));
        need_localtime_ = true;
        break;
    case ('r'): // 12 hour clock 02:55:02 pm
         formatters_.push_back(details::make_unique<details::r_formatter<Padder>>(padding));
        need_localtime_ = true;
        break;
    case ('R'): // 24-hour HH:MM time
         formatters_.push_back(details::make_unique<details::R_formatter<Padder>>(padding));
        need_localtime_ = true;
        break;
    case ('T'):
    case ('X'): // ISO 8601 time format (HH:MM:SS)
         formatters_.push_back(details::make_unique<details::T_formatter<Padder>>(padding));
        need_localtime_ = true;
        break;
    case ('z'): // timezone
         formatters_.push_back(details::make_unique<details::z_formatter<Padder>>(padding));
        need_localtime_ = true;
        break;
    case ('P'): // pid
         formatters_.push_back(details::make_unique<details::pid_formatter<Padder>>(padding));
        break;
    case ('^'): // color range start
         formatters_.push_back(details::make_unique<details::color_start_formatter>(padding));
        break;
    case ('$'): // color range end
         formatters_.push_back(details::make_unique<details::color_stop_formatter>(padding));
        break;
    case ('@'): // source location (filename:filenumber)
         formatters_.push_back(details::make_unique<details::source_location_formatter<Padder>>(padding));
        break;
    case ('s'): // short source filename - without directory name
         formatters_.push_back(details::make_unique<details::short_filename_formatter<Padder>>(padding));
        break;
    case ('g'): // full source filename
         formatters_.push_back(details::make_unique<details::source_filename_formatter<Padder>>(padding));
        break;
    case ('#'): // source line number
         formatters_.push_back(details::make_unique<details::source_linenum_formatter<Padder>>(padding));
        break;
    case ('!'): // source funcname
         formatters_.push_back(details::make_unique<details::source_funcname_formatter<Padder>>(padding));
        break;
    case ('%'): // % char
        formatters_.push_back(details::make_unique<details::ch_formatter>('%'));
        break;
    case ('u'): // elapsed time since last log message in nanos
         formatters_.push_back(details::make_unique<details::elapsed_formatter<Padder,  std::chrono::nanoseconds>>(padding));
        break;
    case ('i'): // elapsed time since last log message in micros
         formatters_.push_back(details::make_unique<details::elapsed_formatter<Padder,  std::chrono::microseconds>>(padding));
        break;
    case ('o'): // elapsed time since last log message in millis
         formatters_.push_back(details::make_unique<details::elapsed_formatter<Padder,  std::chrono::milliseconds>>(padding));
        break;
    case ('O'): // elapsed time since last log message in seconds
         formatters_.push_back(details::make_unique<details::elapsed_formatter<Padder,  std::chrono::seconds>>(padding));
        break;

    default: // Unknown flag appears as is
        // 未定义flag当做普通字符串处理, 而aggregate_formatter用于处理普通字符串
        auto unknown_flag = details::make_unique<details::aggregate_formatter>();

        if (!padding.truncate_) // 未指定截断, 将%flag作为普通字符加入aggregate_formatter
        {
            unknown_flag->add_ch('%');
            unknown_flag->add_ch(flag);
            formatters_.push_back((std::move(unknown_flag)));
        }
        // fix issue #1617 (prev char was '!' and should have been treated as  funcname flag instead of truncating flag)
        // spdlog::set_pattern("[%10!] %v") => "[      main] some message"
        // spdlog::set_pattern("[%3!!] %v") => "[mai] some message"
        else // 指定了截断, 将截断标记'!'当函数名进行截断
        {
            padding.truncate_ = false;
            formatters_.push_back(details::make_unique<details::source_funcname_formatter<Padder>>(padding));
            unknown_flag->add_ch(flag);
            formatters_.push_back((std::move(unknown_flag)));
        }
        break;
    }
}
```

## format()格式化log消息

构造函数中，会编译pattern，根据pattern生成一系列formatter子类，添加进formatters_数组，用于后面将原始的log消息根据pattern转换成最终的log消息。转换工作是交给pattern_formatter::format，然后转交给formatters_的每个成员的format函数，也就是对每个pattern flag进行转换，最终的结果都是存放到二进制内存memory_buf_t中。

```cpp
SPDLOG_INLINE void pattern_formatter::format(const details::log_msg &msg,  memory_buf_t &dest)
{
    if (need_localtime_) // 使用局部时间, 这里并非值本地时间, 而是指调用format的时间
    {
        // 获取epoch时间, 单位: 秒
        const auto secs =  std::chrono::duration_cast<std::chrono::seconds>(msg.time.time_since_epoch());
        if (secs != last_log_secs_) // 如果不是同一秒, 就更新缓存时间cached_tm_
        {
            cached_tm_ = get_time_(msg); // 从msg获取log消息的时间
            last_log_secs_ = secs;
        }
    }
    // 对pattern flag逐个格式化, 得到的dest就是最终的log消息对应二进制缓存
    for (auto &f : formatters_)
    {
        f->format(msg, cached_tm_, dest);
    }
    // eol是附加到每条log消息末尾的字符串
    // write eol
    details::fmt_helper::append_string_view(eol_, dest);
}
```



## clone克隆对象

copy操作（编译器合成）通常是浅拷贝，而clone通常是深拷贝。例如，formatters_成员是std::vector<std::unique_ptrdetails::flag_formatter>，如果使用默认合成的copy操作，就会导致vector元素的copy，而元素类型是unique_ptr不允许copy，因此可能会造成错误；如果是进行move操作，则会导致原来vecotr成员所有权转移，从而指向nullptr。

```cpp
SPDLOG_INLINE std::unique_ptr<formatter> pattern_formatter::clone() const
{
    custom_flags cloned_custom_formatters;
    // 克隆自定义custom_handlers_ 自定义标记formatter的map
    for (auto &it : custom_handlers_)
    {
        cloned_custom_formatters[it.first] = it.second->clone();
    }
    // 构造一个新的pattern_formatter对象, 交给unique_ptr管理
    auto cloned = details::make_unique<pattern_formatter>(pattern_,  pattern_time_type_, eol_, std::move(cloned_custom_formatters));
    // 克隆need_localtime_属性
    cloned->need_localtime(need_localtime_); 
    // 返回克隆对象
#if defined(__GNUC__) && __GNUC__ < 5
    return std::move(cloned);
#else
    return cloned;
#endif
}
```

# formatter子类模板实参Padder

构造formatter子类对象时，有些子类需填充（用空格字符）格式转换后的二进制内存的多余空间。spdlog提供的方案，是使用模板参数Padder，而作为其实参有两套解决方案：scoped_padder类、null_scoped_padder类。前者是空格填充，后者是不进行实际填充。但为了保持代码的一致性，故而提供null_scoped_padder作为空对象模式。

## scoped_padder类

flag_formatter子类会在format()中利用scoped_padder，对固定宽度的字段填充空格。scoped_padder在构造函数中，填充该字段正文左侧空格，计算出右侧该填充空格数，待析构时填充右侧空格。这种方法十分精妙，利用对象的构造与析构，自动完成填充并且确保正文段与填充段的顺序

```cpp
class scoped_padder
{
public:
    // 通过构造函数填充左侧
    // wrapped_size: 包裹字符串的长度(实际宽度)
    // padinfo: 填充信息(限制宽度)
    // dest: 二进制缓存(结果)
    scoped_padder(size_t wrapped_size, const padding_info &padinfo, memory_buf_t  &dest)
        : padinfo_(padinfo)
        , dest_(dest)
    {
        // 计算待填充长度
        remaining_pad_ = static_cast<long>(padinfo.width_) -  static_cast<long>(wrapped_size);
        if (remaining_pad_ <= 0)
        {
            return;
        }

        // 左侧填充空格, 正文右对齐
        if (padinfo_.side_ == padding_info::pad_side::left)
        {
            pad_it(remaining_pad_);
            remaining_pad_ = 0;
        }
        // 两侧填充空格, 正文中心对齐
        else if (padinfo_.side_ == padding_info::pad_side::center)
        {
            auto half_pad = remaining_pad_ / 2;
            auto reminder = remaining_pad_ & 1;
            pad_it(half_pad); // 左侧填充空格
            // 计算余下空格宽度, 用于右侧, 因为正文尚未填充, 所以暂时只是计算
            remaining_pad_ = half_pad + reminder; // for the right side
        }
    }
    
    // 计算n有多少位数
    template<typename T>
    static unsigned int count_digits(T n)
    {
        return fmt_helper::count_digits(n);
    }
    
    // 通过析构函数填充右侧, 如果需要填充的话
    ~scoped_padder()
    {
        if (remaining_pad_ >= 0)
        {
            pad_it(remaining_pad_); // 填充剩余位置
        }
        else if (padinfo_.truncate_) // 没有需要填充的, 就看是否需要截断
        {
            // 重新计算缓存dest大小
            long new_size = static_cast<long>(dest_.size()) + remaining_pad_;
            dest_.resize(static_cast<size_t>(new_size)); 
        }
    }

private:
    // 填充count个空格字符
    void pad_it(long count)
    {
        fmt_helper::append_string_view(string_view_t(spaces_.data(),  static_cast<size_t>(count)), dest_);
    }

    const padding_info &padinfo_;  // 填充信息
    memory_buf_t &dest_;           // 结果缓存
    long remaining_pad_;           // 待填充pad长度
    string_view_t spaces_{"                                                                 ", 64}; // 64个空格字符的数组
};
```

## null_scoped_padder类

没有为pattern flag指定宽度，该字段不需要填充。为了不大幅改变代码，提高复用率，spdlog选择实现一个空的scoped_padder作为需要padder作为模板参数的场景，即null_scoped_padder。该类构造为空，析构函数也是默认的，这样就不进行任何填充。

null_scoped_padder/scoped_padder 都必须实现的public函数：构造函数、析构函数、count_digits。

```cpp
// 因为模板参数不能为空, spdlog选择实现空的scoped_padder
struct null_scoped_padder
{
    // 空的构造函数
    null_scoped_padder(size_t /*wrapped_size*/, const padding_info & /*padinfo*/,  memory_buf_t & /*dest*/) {}
    // 默认析构函数
    // 计算整型数的位数
    template<typename T>
    static unsigned int count_digits(T /* number */){return 0};
};
```

# 标志格式器flag_formatter类

这个类实际上只是一个接口类，利用不同子类将原始log消息进行格式化，按所需的内容、格式，转换为最终的log消息。通过format接口，格式化log消息。由于大多子类可能需要进行填充工作，为了便捷，flag_formatter直接包含padinfo_成员代表需要填充的信息。

```cpp
// 一种flag_formatter子类, 对应一种flag的格式化
class SPDLOG_API flag_formatter
{
public:
    explicit flag_formatter(padding_info padinfo)
        : padinfo_(padinfo)
    {}
    flag_formatter() = default;
    virtual ~flag_formatter() = default;
    // flag_formatter并不负责具体pattern flag格式化, 交由子类进行, 因此为pure virtual函数
    // 格式化log消息msg, 结果存放到dest
    virtual void format(const details::log_msg &msg, const std::tm &tm_time,  memory_buf_t &dest) = 0;

protected:
    padding_info padinfo_;  // 空位填充信息
};
```

## 其派生类

这些类在 /include/spdlog/pattern_formatter-inl.h 文件中。


padding_info类：padding指在一个固定总长度的内存区域中，正文（text）可以位于该区域的右侧、中间、左侧，分别对应正文右对齐、正文中对齐、正文左对齐，而剩余的部分可以用“0”填充（zero padding）。而padding_info类，就是用来携带这些信息的类。


aggregate_formatter子类：显示原始字符串或者未知flag，对应普通字符串或者未定义pattern flag：%flag。aggregate_formatter会逐字符添加到str_进行存储。适用于逐字符解析pattern场景。

full_formatter子类：显示完整信息，对应pattern: %+ 或者 [%Y-%m-%d %H:%M:%S.%e] [%n] [%l] [%s:%#] %v。查阅用户手册的模式标志章节，可知，"[%Y-%m-%d %H:%M:%S.%e]" 时间格式，"[%n]" logger name，"[%l]" log level，"[%s:%#]" 源文件基础名+源代码的行数，"%v" 要记录的实际文本。

name_formatter子类：显示logger name，对应pattern flag：%n。

level_formatter子类：显示log level，对应pattern flag：%l。

short_level_formatter子类：显示简短的log level，对应pattern flag：%L。

t_formatter子类：显示线程id，对应pattern flag：%t。

v_formatter子类：显示消息文本（用户希望记录的message text），对应pattern flag：%v。

a_formatter子类：示简写的星期几，对应pattern flag：%a。

A_formatter子类：显示星期几全称，对应pattern flag：%A。

z_formatter子类：显示IOS 8601 规格时区偏移，例如"+02:00"，对应pattern flag：%z。

source_location_formatter子类：显示源文件和行号，例如"/some/dir/my_file.cpp:123"，对应pattern flag：%@。源文件和行号来自于log消息msg。

ch_formatter子类：显示'%'，对应pattern flag：%%。如果想显示'%'本身，可以使用ch_formatter。


# 小结

类模板 + 空对象模式 实现两套解决方案。

>在flag_formatter子类中，通过模板参数Padder的构造函数、析构函数，来实现固定宽度的剩余空（左侧、右侧）的填充。但有时候，我们没有指定字段宽度，也就是不需要填充。那么，如何让Padder客户端代码不变的情况下，不进行真实填充呢？

spdlog的解决方案是使用scoped_padder类、null_socped_padder类，作为模板实参。前者是会实际填充的类，后者是什么也不做的类，其对象是空对象。

类似的解决方案还有null_mutex、null_atomic_int。

```cpp
// 客户端 只需要根据需要决定传入scoped_padder or null_scoped_padder
// 要填充, 模板实参为实际要填充的scoped_padder类
handle_flag_<details::scoped_padder>(*it, padding);

// 不填充, 模板实参为实际不填充的null_scoped_padder类
handle_flag_<details::null_scoped_padder>(*it, padding);
```


而scoped_padder/null_scoped_padder类型，最终会传递给flag_formatter子类的ScopedPadder，于是在flag_formatter子类的format实现中，利用ScopedPadder的构造与析构，完成空格填充。

```cpp
// scoped_padder/null_scoped_padder类型 最终会传递给flag_formatter子类的ScopedPadder
// 例如name_formatter
template<typename ScopedPadder>
class name_formatter final : public flag_formatter
{
public:
    explicit name_formatter(padding_info padinfo)
        : flag_formatter(padinfo)
    {}
    
    // 格式化log消息
    void format(const details::log_msg &msg, const std::tm &, memory_buf_t &dest)  override
    {
        // 构造一个ScopedPadder对象, 利用构造函数对logger name字段(左侧)进行填充
        ScopedPadder p(msg.logger_name.size(), padinfo_, dest);

        fmt_helper::append_string_view(msg.logger_name, dest);

        // ScopedPadder析构函数(编译期自动添加), 利用析构函数对logger name字段(右侧)进行填充
    }
};
```

这种方式的好处是，客户端的代码几乎不用做任何改变，只需要实现两种scoped_padder类，并且根据需要传递给不同的flag_formatter子类即可。复用性极强，而且增加一套方案的支持代码改动量很小：只需要判断何时使用哪种方案。