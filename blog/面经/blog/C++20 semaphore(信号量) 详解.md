<semaphore>头文件在C++20中是并发库技术规范（Technical Specification, TS）的一部分。信号量是同步原语，帮助控制多线程程序中对共享资源的访问。<semaphore>头文件提供了标准C++方式来使用信号量。

# 使用环境

Windows：VS中打开项目属性，修改C++语言标准。

Linux：GCC，版本至少应为10.1，编译命令中使用-std=c++20标志。

# 简单定义

你可以像这样创建一个信号量对象：

```cpp
std::counting_semaphore<size_t> sem(1); // 用初始计数为1初始化一个信号量
```

std::counting_semaphore是一种允许指定数量的线程同时访问资源的信号量。在这个例子中，一次只有一个线程可以访问由sem保护的资源。

# 获取和释放

## 直接获取

要获取（锁定）信号量，你可以使用acquire方法：

```cpp
sem.acquire();
// 关键段代码
sem.release();
```
acquire方法将信号量计数减一，有效地锁定它。release方法增加计数，释放信号量。

## 尝试获取

你也可以使用try_acquire方法来尝试获取信号量而不阻塞：

```cpp
if (sem.try_acquire()) {
    // 成功获取了信号量
    // 关键段代码
    sem.release();
} else {
    // 信号量未被获取
}
```

## 带超时的等待

C++20还引入了try_acquire_for和try_acquire_until方法，以带超时的方式尝试获取信号量。

```cpp
if (sem.try_acquire_for(std::chrono::seconds(1))) {
    // 在1秒内成功获取了信号量
    // 关键段代码
    sem.release();
} else {
    // 在1秒内未能获取信号量
}
```

# 信号量的类型

## std::counting_semaphore

计数信号量是一种同步原语，允许多个线程在一定限制下访问共享资源。它是互斥锁或二进制信号量的泛化。

你可以用一个初始计数来初始化计数信号量，该计数代表可以同时无阻塞访问资源的线程数量。线程可以获取和释放计数，信号量的计数相应地增加或减少。如果线程尝试获取的计数超过了可用的数量，它将阻塞，直到计数变得可用。

```cpp
// 展示如何使用counting_semaphore
#include <iostream>
#include <semaphore>
#include <thread>
using namespace std;

// 用3个计数初始化信号量
counting_semaphore<10> semaphore(3);

void worker(int id)
{
    // 获取
    semaphore.acquire();

    // 执行一些工作
    cout << "Thread " << id << " acquired the semaphore."
         << endl;

    // 释放
    semaphore.release();
    cout << "Thread " << id << " released the semaphore."
         << endl;
}

int main()
{
    thread t1(worker, 1);
    thread t2(worker, 2);
    thread t3(worker, 3);
    t1.join();
    t2.join();
    t3.join();
    return 0;
}

```

输出结果

```cpp
Thread 2 acquired the semaphore.
Thread 2 released the semaphore.
Thread 1 acquired the semaphore.
Thread 1 released the semaphore.
Thread 3 acquired the semaphore.
Thread 3 released the semaphore.
```

## std::binary_semaphore

二进制信号量是一种更简单的信号量版本，它只能有两个值：0和1。

通常用于两个线程之间的基本互斥或信号传递。可以被视为具有更轻量级接口的互斥锁。类似于mutex。

```cpp
// 展示二进制信号量的用法
#include <iostream>
#include <semaphore>
#include <thread>
using namespace std;

// 用1个计数（二进制）初始化信号量
binary_semaphore semaphore(1);

void worker(int id)
{
    // 获取信号量
    semaphore.acquire();
    cout << "Thread " << id << " acquired the semaphore."
         << endl;

    // 执行一些工作
    semaphore.release();
    // 释放信号量
    cout << "Thread " << id << " released the semaphore."
         << endl;
}

int main()
{
    thread t1(worker, 1);
    thread t2(worker, 2);
    t1.join();
    t2.join();
    return 0;
}

```

输出结果

```cpp
Thread 1 acquired the semaphore.
Thread 1 released the semaphore.
Thread 2 acquired the semaphore.
Thread 2 released the semaphore.
```

# 优点

信号量的优势如下：

- 精细控制：信号量可以配置为允许特定数量的线程同时访问资源，实现资源的精细控制。
- 通用性：信号量更加灵活多样，可以用来实现其他同步原语。
- 多资源管理：计数信号量可以用来管理多个资源实例，适用于需要控制对资源池（如线程池或连接池）访问的场景。
- 阻塞等待：信号量中的阻塞和等待机制允许线程等待直到资源再次可用。
- 超时处理：在获取信号量时可以指定超时，使其更加实用。


# 例子：生产者消费者问题

```cpp
#include <iostream>
#include <semaphore>
#include <thread>
using namespace std;

const int buffer_size = 5;// 缓冲区大小
std::binary_semaphore b_mutex(1);
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

## 关键组件
b_mutex：一个二进制信号量，用作互斥锁，保证在任何时刻只有一个线程可以操作缓冲区。这是为了防止多个生产者或消费者同时访问缓冲区，导致数据不一致的问题。

b_full：一个计数信号量，表示缓冲区中已填充的项数。初始化为0，因为一开始缓冲区是空的。

b_empty：另一个计数信号量，表示缓冲区中空闲位置的数量。初始化为缓冲区的大小，因为起初整个缓冲区都是空的。

## 生产者（Producer）
等待一个空闲位置（b_empty.acquire();），这表示生产者在缓冲区中找到了一个可以放置新生产项的位置。

获取互斥锁（b_mutex.acquire();），进行生产操作（这里通过打印"Producer"模拟），然后释放互斥锁（b_mutex.release();），这样其他线程（生产者或消费者）就可以访问缓冲区了。

生产完成后，释放一个已满位置的信号（b_full.release();），告知消费者缓冲区中有项可被消费。模拟生产过程中的延迟（std::this_thread::sleep_for(std::chrono::seconds(2));）。

## 消费者（Consumer）

等待一个已满位置（b_full.acquire();），这表示消费者在缓冲区中找到了一个可以消费的项。

获取互斥锁（b_mutex.acquire();），进行消费操作（这里通过打印"Consumer"模拟），然后释放互斥锁（b_mutex.release();），这样其他线程（生产者或消费者）就可以访问缓冲区了。

消费完成后，释放一个空闲位置的信号（b_empty.release();），告知生产者缓冲区中有位置可用于生产新的项。模拟消费过程中的延迟（std::this_thread::sleep_for(std::chrono::seconds(2));）。