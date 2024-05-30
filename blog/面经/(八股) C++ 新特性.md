# 类型转换

static_cast：于非多态类型的转换（静态转换），编译器隐式执行的任何类型转换都可用 static_cast ，但它不能用于两个不相关的类型进行转换。

reinterpret_cast：操作符通常为操作数的位模式提供较低层次的重新解释，用于将一种类型转换 

dynamic_cast：用于将一个父类对象的指针转换为子类对象的指针或引用(动态转换)
- 向上转型：子类对象指针->父类指针/引用(不需要转换，赋值兼容规则) 
- 向下转型：父类对象指针->子类指针/引用(用dynamic_cast转型是安全的) 

dynamic_cast 只能用于父类含有虚函数的类，dynamic_cast 会先检查是否能转换成功，能成功则转换，不能则返回 0 。
const_cast：常量转换用于去掉表达式的const属性，使其变成非常量表达式。常量转换不能改变表达式的类型，只能改变const属性。

# RAII基于什么实现？

RAII技术主要是通过C++的构造函数和析构函数来实现。在C++的对象构造时，会自动调用构造函数，此时可以在构造函数中进行资源的申请和初始化；当对象被销毁时，会自动调用析构函数，此时可以在析构函数中进行资源的释放。RAII技术的核心思想就是，资源的生命周期应该与对象的生命周期相同，即资源申请和释放都应该在对象构造和析构中完成，以确保资源的正确管理。

# 手撕 unique_ptr

```cpp
template <typename T>
class Unique_ptr {
public:
    // 构造函数：初始化指针为 nullptr
    Unique_ptr() : ptr(nullptr) {}
    // 构造函数：接收一个原始指针作为参数
    explicit Unique_ptr(T* p) : ptr(p) {}
    // 移动构造函数：将右值引用转移赋值给左值引用
    Unique_ptr(Unique_ptr&& other) noexcept {
        this->ptr = other.ptr;
        other.ptr = nullptr;
    }
    // 析构函数：自动释放所管理的资源
    ~Unique_ptr() {
        delete ptr;
    }
    // 按值交换函数：使用 std::swap 实现快速交换两个对象
    void swap(Unique_ptr& other) noexcept {
        std::swap(this->ptr, other.ptr);
    }
    // 赋值运算符重载：支持控制权转移（move semantics）
    Unique_ptr& operator=(Unique_ptr&& other) noexcept {
        if (this != &other) {
            delete this->ptr;
            this->ptr = other.ptr;
            other.ptr = nullptr;
        }
        return *this;
    }
    // 禁止拷贝构造函数和拷贝赋值运算符
    Unique_ptr(const Unique_ptr&) = delete;
    Unique_ptr& operator=(const Unique_ptr&) = delete;
    // 获取原始指针的引用
    T* get() const noexcept {
        return ptr;
    }
private:
    T* ptr;  // 原始指针，不可共享所有权
};
```
## unique_ptr和shared_ptr区别

unique_ptr。它独占地管理所指向的对象，在同一时刻只有一个unique_ptr可以指向同一个对象，当unique_ptr离开作用域或被销毁时，它所指向的对象也会被销毁。

shared_ptr。它允许多个shared_ptr共享同一个对象，每个shared_ptr对象内部维护一个引用计数器，表示当前指向该对象的shared_ptr数量，当最后一个shared_ptr被销毁时，它所指向的对象才会被销毁。


# 右值引用

右值是临时产生的值，不能对右值取地址，因为它本身就没存在内存地址空间上。右值引用只能绑定到右值上。右值有了名字后，就成了一个普通变量，也就是左值。


## 函数传参

右值引用是C++11引入的一个特性，用于实现移动语义（Move Semantics）。右值引用允许我们对临时对象进行非常高效的操作，而不必进行深拷贝。std::move实际上是将左值转换为右值，此时就可用右值引用绑定，调用右值引用重载函数。

- 若传入参数是非const左值，调用非const左值引用重载函数
- 若传入参数是const左值，调用const左值引用重载函数
- 若传入参数是右值，调用右值引用重载函数(即使是有 const 左值引用重载的情况下)

# 完美转发

C++ 11中有万能引用（Universal Reference）的概念：使用​​T&&​​类型的形参既能绑定右值，又能绑定左值。

但是注意了：只有发生类型推导的时候，T&&才表示万能引用；否则，表示右值引用。

- 假设入参是一个 string 左值: 此时 T&& 应该等同于 string &, 根据引用折叠的规则，T 应该是一个左值引用，于是得到 T 为 string &，即非const左值引用
- 假设入参是一个 const string 左值： 此时 T&& 等同于 const string&，得到 T 为 const string &，即const 左值引用。
- 假设入参是右值，如 move（string）： 此时 T&& 等同于 string&&， 于是得到 T 为 string&&，即右值引用。

当需要在函数中调用其他函数，并且转发参数的时候，例如调用 push_back 函数：

传进来的入参有可能是个左值，有可能是一个右值。然而形参 x 一定是一个左值，因为他是个具名的对象。直接 push_back(x) 的话，就相当于入参传递的一定是左值了。也就是说，不论我们实际入参是左值还是右值，最后都会被当做左值来转发。即我们丢失了它本身的值类型。

std::forward被称为完美转发，它的作用是保持原来的值属性不变。保留了参数的值类型，左值就按照左值转发，右值按照右值转发。

# unique_lock和lock_guard区别

std::unique_lock和std::lock_guard都是C++标准库提供的用于互斥量（mutex）的RAII封装类，用于简化线程同步操作。

std::unique_lock比std::lock_guard更加灵活。它可以在构造时选择是否锁住互斥量，并且可以在生命周期内多次锁住和解锁互斥量。而std::lock_guard在构造时会自动锁住互斥量，在析构时自动释放锁，没有其他操作选项。

std::unique_lock支持所有权转移（move ownership）。这意味着一个线程可以创建一个独占的 std::unique_lock 对象，并将其所有权传递给另一个线程。

std::unique_lock 会带来一些额外的性能开销。而 std::lock_guard 是更轻量级的封装，因此通常情况下效率较高。

unique_lock 可以与条件变量（std::condition_variable）一起使用，以实现线程间的同步和通信。

# lambda表达式的理解

匿名函数

lambda表达式的具体形式如下：

```cpp
[capture](parameters)->return-type{body}
```

其中, capture是需要用到的外部变量， parameters是函数参数，return-type是返回的类型（可省略），body是函数体。

lambda只有在其捕获列表中捕获一个它所在函数中的局部变量，才能在函数体中使用该变量。捕获列表只用于非静态局部变量，lambda可以直接使用静态局部变量和在它所在函数之外声明的名字。

以下为捕获规则
- [] 什么也没有捕获
- [a, &b] 按值捕获a，按引用捕获b
- [&] 按引用捕获任何用到的外部变量
- [=] 按值捕获任何用到的外部变量
- [a, &] 按值捕获a, 其它的变量按引用捕获
- [&b, =] 按引用捕获b,其它的变量按值捕获
- [this] 按值捕获this指针

# move函数

C++11 标准中借助右值引用可以为指定类添加移动构造函数，这样当使用该类的右值对象（可以理解为临时对象）初始化同类对象时，编译器会优先选择移动构造函数。

移动构造函数的调用时机是：用同类的右值对象初始化新对象。那么，用当前类的左值对象（有名称，能获取其存储地址的实例对象）初始化同类对象时，是否就无法调用移动构造函数了呢？

当然不是，调用 move() 函数，将某个左值强制转化为右值。，调用右值重载函数。

# Coroutine（C++20）

C++的协程是从C++20标准开始引入的一个重要特性，它们为异步编程提供了一种新的编程范式。协程是可以暂停和恢复执行的函数。

协程的工作原理
- 启动：当调用一个协程时，它并不会立即执行，而是首先创建一个承诺对象。协程的执行将根据这个承诺对象的行为来进行。
- 暂停和恢复：通过co_await和co_yield，协程可以在执行过程中暂停，并在条件满足时被恢复。
- 结束：协程通过执行co_return来结束，此时会清理资源，并将控制权返回给协程的调用者。


# memory_order

memory_order指定了原子操作的内存顺序约束。

- std::memory_order_relaxed：最宽松的内存顺序。不保证操作的顺序，只保证原子操作的原子性。
- std::memory_order_consume：（在C++17中被弃用，行为与std::memory_order_acquire相同）针对特定的数据依赖，提供较轻量级的顺序保证。
- std::memory_order_acquire：确保当前线程中，所有后续的读操作必须在这个操作之后执行。
- std::memory_order_release：确保当前线程中，所有之前的写操作完成后，才能执行这个操作。
- std::memory_order_acq_rel：同时具有acquire和release的语义，适用于读-修改-写操作。
- std::memory_order_seq_cst：顺序一致性内存顺序。这是最严格的内存顺序，它保证了所有线程看到相同顺序的操作。

