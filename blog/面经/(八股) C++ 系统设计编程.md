# 异步通信的方法

使用线程和回调函数： 可以创建一个新的线程来执行异步操作，然后在操作完成后通过回调函数来处理结果。

使用异步任务（Future 和 Promise）：std::future 用于获取异步操作的结果，std::promise 用于设置异步操作的结果。td::packaged_task 包装一个可调用的对象，并且允许异步获取该可调用对象产生的结果。

```cpp
#include <iostream>     // std::cout
#include <future>       // std::packaged_task, std::future
#include <chrono>       // std::chrono::seconds
#include <thread>       // std::thread, std::this_thread::sleep_for

int countdown(int from, int to) {
    for (int i = from; i != to; --i) {
        std::cout << i << '\n';
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "Finished!\n";
    return from - to;
}

int main()
{
    std::packaged_task<int(int, int)> task(countdown); // 设置 packaged_task
    std::future<int> ret = task.get_future(); // 获得与 packaged_task 共享状态相关联的 future 对象.
    std::thread th(std::move(task), 10, 0);   //创建一个新线程完成计数任务.
    int value = ret.get();                    // 等待任务完成并获取结果.
    std::cout << "The countdown lasted for " << value << " seconds.\n";
    th.join();
    return 0;
}

```


# 为什么要用多线程不用多进程

多进程下，进程的上下文不仅包括了虚拟内存、栈、全局变量等用户空间的资源，还包括了内核堆栈、寄存器等内核空间的状态。

多线程属于同一个进程。此时，虚拟内存是共享的，所以在切换时，虚拟内存这些资源就保持不动，只需要切换线程的私有数据、寄存器等不共享的数据。

但同进程内的线程切换，要比多进程间的切换消耗更少的资源。

# 线程池

```cpp
#pragma once
 
#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
 
class ThreadPool
{
private:
    std::vector<std::thread> workers_;          // 工作线程
    std::queue<std::function<void()>>tasks_;    // 任务队列，存放匿名函数
    std::mutex queue_mutex_;                    // 任务队列的互斥锁
    std::condition_variable condition_;         // 条件变量，用来唤醒工作线程
    bool stop_;                                 // 线程池是否正在工作
public:
    ThreadPool(size_t size)
        :stop_(false)
    {
        for(size_t i=0;i<size;++i)
        {
            workers_.emplace_back([this]{
                for(;;)
                {
                    std::function<void()> task;
 
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex_);
                        //当括号内的代码结果为真时才会继续执行后面的代码，否则会一直阻塞在这个函数中。
                        this->condition_.wait(lock,[this]{
                            return (this->stop_)||!this->tasks_.empty();
                        });
                        if(this->stop_&&this->tasks_.empty())
                            return;
                        task=std::move(this->tasks_.front());
                        this->tasks_.pop();
                    }
                    task();
                }
            });
        }
    }
 
    template <class F,class... Args>
    auto enqueue(F&& f,Args&&... args)
        ->std::future<typename std::result_of<F(Args...)>::type>
    {
        //返回一个异步对象
        using return_type=typename std::result_of<F(Args...)>::type;
 
        auto task=std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f),std::forward<Args>(args)...)
        );
 
        std::future<return_type> res=task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if(stop_)
                throw std::runtime_error("enqueue on stopped ThreadPool.");
 
            tasks_.emplace([task](){
                (*task)();
            });
            //把std::packaged_task 包装的可调用对象函数放入队列
        }
        condition_.notify_one();
        return res;
    }
 
    ~ThreadPool()
    {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_=true;
        }
 
        condition_.notify_all();
        for(std::thread& worker: workers_)
        {
            if(worker.joinable())
                worker.join();
        }
    }
};

```

构造函数：接受一个整型变量，这个整型代表线程池中存放线程的数目，并在构造函数中将线程以匿名函数的方式存放到vector容器内部。线程会不断检测任务队列中是否有需要执行的任务，如果没有就一直被阻塞，直到任务队列中有数据，被条件变量唤醒，取出任务并执行。每个线程的任务就是不断监听任务队列，任务队列不为空就尝试获得锁，并取出任务执行。

向任务队列中添加任务：接受一个可调用的函数F和一系列参数，并返回一个std::future对象，用于异步获取可调用函数的执行结果或状态。创建一个智能指针，确保对象不再被需要的时候被销毁。通过packaged_task对象封装可调用对象，然后用bind函数将可调用对象和和可变参数绑定，并通过forward完美转发。放入队列中。

析构函数：拥有锁后将线程池停止运行的标志设置为真，函数体的大括号也是为了在出了作用域后释放锁。之后唤醒所有的线程。然后检查线程是否满足joinable的条件，满足就调用join函数，使得主函数等待线程执行完毕。这是为了将任务队列中的所有任务全部执行完毕。避免漏掉任务队列中的任务。


# 生产者消费者

## 信号量的使用

生产者和消费者之间通过一个有限大小的缓冲区进行通信。当缓冲区满时，生产者必须等待，直到有消费者取走数据，释放出空间。而当缓冲区为空时，消费者必须等待，直到生产者生成数据并放入缓冲区。

使用两个信号量来表示空闲空间数量和可用数据数量。
- 生产者在生产数据时，首先尝试获取空闲空间信号量，然后将数据放入缓冲区，并释放可用数据信号量。
- 消费者在消费数据时，首先尝试获取可用数据信号量，然后从缓冲区取出数据，并释放空闲空间信号量。
这种方法可以通过信号量的值来控制生产者和消费者的行为，保证生产者和消费者之间的同步和互斥。


```cpp
#include <iostream>
#include <semaphore>
#include <thread>
#include <queue>
using namespace std;

const int buffer_size = 5;// 缓冲区大小
std::counting_semaphore<1> b_mutex(1);
//容量为5 赋初值
std::counting_semaphore<buffer_size> b_full(0);
std::counting_semaphore<buffer_size> b_empty(5);
void Producer()
{
    while (true)
    {
        b_empty.acquire();
        b_mutex.acquire();
        std::cout << "Producer\n";
        b_mutex.release();
        b_full.release();
        //模拟生成过程
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}
void Consumer()
{
    while (true)
    {
        b_full.acquire();
        b_mutex.acquire();
        std::cout << "Consumer\n";
        b_mutex.release();
        b_empty.release();
        //模拟消耗过程
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}
int main()
{
    thread t0(Producer);
    thread t1(Producer);
    thread t2(Consumer);
    thread t3(Consumer);

    t0.join();
    t1.join();
    t2.join();
    t3.join();
}
```

## 队列空时情况

队列为空时，消费者线程被阻塞，等待生产者释放可用数据数量的信号量。



# C++多线程并发问题（场景千万级数量级怎么处理）

使用线程池：创建和销毁线程有很大的开销。对于千万级的任务，不可能为每个任务都创建一个新线程。线程池允许固定数量的线程来执行这些任务。这不仅可以减少线程创建和销毁的开销，还能有效控制并发数量，避免过多的线程竞争导致的性能问题。（内存池，对象池同理）

减少锁的使用：使用更细粒度的锁来保护数据，而不是一个大锁保护所有数据，可以减少锁的竞争。比如使用原子操作和自旋锁。

限流和：限制到达系统或服务的请求量，确保系统在负载可承受的范围内运行。使用令牌桶算法，以固定的速率向令牌桶中添加令牌，处理请求时需要消耗令牌，能够允许一定程度的突发流量。

# 怎样分配线程数量？

- CPU 密集型的程序 ：核心数 + 1
- I/O 密集型的程序 ： 核心数 * 2

一个 CPU 核心，某一时刻只能执行一个线程的指令

一个极端的线程（不停执行 “计算” 型操作时），就可以把单个核心的利用率跑满，多核心 CPU 最多只能同时执行等于核心数的 “极端” 线程数

如果每个线程都这么 “极端”，且同时执行的线程数超过核心数，会导致不必要的切换，造成负载过高，只会让执行更慢

I/O 等暂停类操作时，CPU 处于空闲状态，操作系统调度 CPU 执行其他线程，可以提高 CPU 利用率，同时执行更多的线程

I/O 事件的频率频率越高，或者等待 / 暂停时间越长，CPU 的空闲时间也就更长，利用率越低，操作系统可以调度 CPU 执行更多的线程

# 什么情况下会使用静态变量

全局变量替代： 静态变量可以替代全局变量的使用，避免了全局变量可能引发的命名冲突和作用域混乱的问题。在命名空间内部或者在类的静态成员中定义静态变量，可以将其限制在特定的作用域内。

计数器和缓存： 静态变量可以用作计数器或者缓存。例如，可以使用静态变量来跟踪函数调用的次数或者保存函数的计算结果，以避免重复计算。

单例模式： 静态变量常常用于实现单例模式，确保只有一个实例被创建并且可以在整个程序中访问。

常量数据存储： 在函数内部或者类的成员函数中定义的静态常量可以用于存储常量数据，避免了每次调用函数都要重新创建和初始化的开销。

跨文件共享数据： 在不同的源文件中，可以使用静态变量来实现跨文件共享数据，通过 extern 关键字将其声明并在其他文件中使用。

# 定时器的设计


## 为什么用小根堆实现定时器

小根堆支持以O(log n)时间复杂度插入新的定时任务和以O(1)时间复杂度查看最近的定时任务（堆顶元素），弹出下一个最近的定时任务也仅需O(log n)时间复杂度。


# 心跳检测如何实现

心跳检测步骤：
- 客户端每隔一个时间间隔发生一个探测包给服务器。
- 客户端发包时启动一个超时定时器。
- 服务器端接收到检测包，应该回应一个包。
- 如果客户机收到服务器的应答包，则说明服务器正常，删除超时定时器。
- 如果客户端的超时定时器超时，依然没有收到应答包，则说明服务器挂了。


**方案：**每个连接保存最后收到数据的时间，每当该连接收到数据则刷新该时间。然后用一个系统级的定时器，每秒钟遍历一遍所有连接，判断每个连接的最后收到数据的时间。一旦该时间超时，则判定该连接超时。 

使用超时队列，创建了一个先入先出队列（入队在队头，出队在队尾）用来管理超时连接。为了保证该队列的线程安全，每次对该队列进行添加和删除操作均需在加锁情况下进行。在每个队列节点中，都保存了对应连接的上次接收消息时间。每当有新连接建立时，尝试获取超时队列锁，然后创建一个新的队列节点，并在该节点中保存该连接信息和当前时间。最后我们将这个新连接对应的节点添加到超时队列的头部(超时时间早的在后面，因为来得早)。


系统建立一个定时器，每隔一段时间检查超时队列中接收时间已经过期的连接。首先会检查超时队列最尾部的节点，如果对该节点对应连接的接收时间进行判断，显示未超时，则整个超时队列中的其他节点对应连接也均未超时，此次定时事件直接结束处理。如果判定该队尾节点对应连接已经超时，则记录该连接信息，并将该节点从队尾移除。然后再判断新的队尾节点是否已超时，如超时则同样记录并且移除，直到新的队尾节点未超时为止。最后系统将会向此次检测超时的所有连接发送超时通知，并最终强制断开这些超时连接。

使用list和unordered_map，前者作为队列，后者为索引。更新，插入，删除均为O(1)
