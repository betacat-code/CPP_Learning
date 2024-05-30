# kfifo 简介

kfifo是Linux内核的一个FIFO数据结构，采用环形循环队列的数据结构来实现，提供一个无边界的字节流服务，并且使用并行无锁编程技术，即单生产者单消费者场景下两个线程可以并发操作，不需要任何加锁行为就可以保证kfifo线程安全。

其数据结构如下：

```cpp
struct kfifo
{
    unsigned char *buffer;
    unsigned int size;
    unsigned int in;
    unsigned int out;
    spinlock_t *lock;
};
```
# 源码阅读

## 设计要点

**保证buffer size为2的幂**：kfifo->size值在调用者传递参数size的基础上向2的幂扩展，目的是使kfifo->size取模运算可以转化为位与运算（提高运行效率）。kfifo->in % kfifo->size转化为 kfifo->in & (kfifo->size – 1)保证size是2的幂可以通过位运算的方式求余，在频繁操作队列的情况下可以大大提高效率。


**使用自旋锁实现同步**：确保在多线程环境下对缓冲区的操作是线程安全的。spin_lock_irqsave 和 spin_unlock_irqrestore 确保在临界区内的操作不会被中断打断。


**线性代码结构**：代码中没有任何if-else分支来判断是否有足够的空间存放数据，kfifo每次入队或出队只是简单的 +len 判断剩余空间，并没有对kfifo->size 进行取模运算，所以kfifo->in和kfifo->out总是一直增大，直到unsigned in超过最大值时绕回到0这一起始端，但始终满足：kfifo->in - kfifo->out <= kfifo->size。

**使用内存屏障**：内存屏障确保编译器和CPU在内存访问上的有序性，避免乱序访问带来的逻辑错误。适用于多处理器和单处理器环境下的不同类型内存屏障，确保同步原语和无锁数据结构的正确性。


kfifo的巧妙之处在于in和out定义为无符号类型，在put和get时，in和out都是增加，当达到最大值时，产生溢出，使得从0开始，进行循环使用。


## 创建队列

```cpp
struct kfifo *kfifo_init(unsigned char *buffer, unsigned int size, gfp_t gfp_mask, spinlock_t *lock)
{
    struct kfifo *fifo;
    // 判断是否为2的幂
    BUG_ON(!is_power_of_2(size));
    fifo = kmalloc(sizeof(struct kfifo), gfp_mask);
    if (!fifo)
        return ERR_PTR(-ENOMEM);
    fifo->buffer = buffer;
    fifo->size = size;
    fifo->in = fifo->out = 0;
    fifo->lock = lock;

    return fifo;
}
```

其传参如下，这些是用来初始化kfifo：
- unsigned char *buffer：缓冲区的数据存储区。
- unsigned int size：缓冲区的大小。
- gfp_t gfp_mask：内存分配标志，用于内存分配函数kmalloc。
- spinlock_t *lock：自旋锁，用于多线程同步。

其会检查size是否是2的幂。如果不是2的幂，BUG_ON宏会导致程序崩溃并打印错误信息。环形缓冲区的大小必须是2的幂，以便使用高效的位运算（如按位与操作）代替取模运算。

对于一个索引 index，取模运算是：index%N，这是因为环形缓冲区是循环的，当索引超过缓冲区大小时，需要返回到缓冲区的起点。如果N是2的幂，那么可以N表示为2^k。此时，取模运算可以通过位运算替代：

**index % N = index & (N - 1)**

这种方式极大地提高了计算效率，因为按位与运算比取模运算要快得多。

## 分配空间

```cpp
struct kfifo *kfifo_alloc(unsigned int size, gfp_t gfp_mask, spinlock_t *lock)
{
    unsigned char *buffer;
    struct kfifo *ret;
    // 判断是否为2的幂
    if (!is_power_of_2(size))
    {
        BUG_ON(size > 0x80000000);
        // 向上扩展成2的幂
        size = roundup_pow_of_two(size);
    }
    buffer = kmalloc(size, gfp_mask);
    if (!buffer)
        return ERR_PTR(-ENOMEM);
    ret = kfifo_init(buffer, size, gfp_mask, lock);

    if (IS_ERR(ret))
        kfree(buffer);
    return ret;
}
```

这一步主要就是检查缓冲区大小是否为2的幂。如果不是，则将其调整为不小于原大小的最接近的2的幂，
为缓冲区分配内存。

## 入队函数

```cpp
unsigned int __kfifo_put(struct kfifo *fifo, const unsigned char *buffer, unsigned int len)
{
    unsigned int l; //buffer中空的长度
    len = min(len, fifo->size - fifo->in + fifo->out);
    // 内存屏障：smp_mb()，smp_rmb(), smp_wmb()来保证对方观察到的内存操作顺序
    smp_mb();
    // 将数据追加到队列尾部
    l = min(len, fifo->size - (fifo->in & (fifo->size - 1)));
    memcpy(fifo->buffer + (fifo->in & (fifo->size - 1)), buffer, l);
    memcpy(fifo->buffer, buffer + l, len - l);

    smp_wmb();
    //每次累加，到达最大值后溢出，自动转为0
    fifo->in += len;
    return len;
}
static inline unsigned int kfifo_put(struct kfifo *fifo, const unsigned char *buffer, unsigned int len)
{
    unsigned long flags;
    unsigned int ret;
    spin_lock_irqsave(fifo->lock, flags);
    ret = __kfifo_put(fifo, buffer, len);
    spin_unlock_irqrestore(fifo->lock, flags);
    return ret;
}
```

通过在 kfifo_put 中使用自旋锁（spin_lock_irqsave 和 spin_unlock_irqrestore），确保了在多线程环境下对缓冲区的操作是线程安全的。

进入临界区前,获取自旋锁，并禁用中断。这确保在临界区内不会发生上下文切换或中断。调用实际的写入函数 __kfifo_put，执行数据写入操作。释放自旋锁，并恢复中断状态。


## 出队操作

```cpp
unsigned int __kfifo_get(struct kfifo *fifo, unsigned char *buffer, unsigned int len)
{
    unsigned int l;
    //有数据的缓冲区的长度
    len = min(len, fifo->in - fifo->out);
    smp_rmb();
    l = min(len, fifo->size - (fifo->out & (fifo->size - 1)));
    memcpy(buffer, fifo->buffer + (fifo->out & (fifo->size - 1)), l);
    memcpy(buffer + l, fifo->buffer, len - l);
    smp_mb();
    fifo->out += len; //每次累加，到达最大值后溢出，自动转为0
    return len;
}
static inline unsigned int kfifo_get(struct kfifo *fifo, unsigned char *buffer, unsigned int len)
{
    unsigned long flags;
    unsigned int ret;
    spin_lock_irqsave(fifo->lock, flags);
    ret = __kfifo_get(fifo, buffer, len);
    //当fifo->in == fifo->out时，buufer为空
    if (fifo->in == fifo->out)
        fifo->in = fifo->out = 0;
    spin_unlock_irqrestore(fifo->lock, flags);
    return ret;
}
```

在数据出队时，有两个复制操作，与入队时很类似，下

```cpp
memcpy(buffer, fifo->buffer + (fifo->out & (fifo->size - 1)), l);
memcpy(buffer + l, fifo->buffer, len - l);
```


假设：缓冲区大小 N = 8。当前读指针 out = 6。需要读取的数据长度 len = 5。

第一步：计算可以连续读取的长度 l = min(5, 8 - (6 & 7)) = min(5, 2) = 2。执行 memcpy(buffer, fifo->buffer + 6, 2)：从位置 6 开始读取 2 字节数据到目标缓冲区 buffer。

第二步：计算剩余需要读取的数据长度 len - l = 5 - 2 = 3。执行 memcpy(buffer + 2, fifo->buffer, 3)：从环形缓冲区起始位置读取 3 字节数据到目标缓冲区 buffer + 2。


## 计算长度

```cpp
static inline unsigned int __kfifo_len(struct kfifo *fifo)
{
    return fifo->in - fifo->out;
}

static inline unsigned int kfifo_len(struct kfifo *fifo)
{
    unsigned long flags;
    unsigned int ret;
    spin_lock_irqsave(fifo->lock, flags);
    ret = __kfifo_len(fifo);
    spin_unlock_irqrestore(fifo->lock, flags);
    return ret;
}
```


## 重置
```cpp
static inline void __kfifo_reset(struct kfifo *fifo)
{
    fifo->in = fifo->out = 0;
}

static inline void kfifo_reset(struct kfifo *fifo)
{
    unsigned long flags;
    spin_lock_irqsave(fifo->lock, flags);
    __kfifo_reset(fifo);
    spin_unlock_irqrestore(fifo->lock, flags);
}
```

直接重置环形缓冲区的读写指针，使其重新变为空状态。


# C++仿写

使用C++的无锁编程实现kfifo，100w次入队出队只需要0.7s。

```cpp
#include <iostream>
#include <vector>
#include <atomic>
#include <thread>
#include <cassert>
#include <chrono>

template<typename T>
class LockFreeQueue {
public:
    explicit LockFreeQueue(size_t size)
        : buffer_(size), size_(size), head_(0), tail_(0) {
        assert((size & (size - 1)) == 0 && "Size must be a power of 2");
    }

    // Enqueue an element into the queue. Returns false if the queue is full.
    bool enqueue(const T& item) {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t next_head = (head + 1) & (size_ - 1);
        if (next_head == tail_.load(std::memory_order_acquire)) {
            // Queue is full
            return false;
        }
        buffer_[head] = item;
        head_.store(next_head, std::memory_order_release);
        return true;
    }

    // Dequeue an element from the queue. Returns false if the queue is empty.
    bool dequeue(T& item) {
        size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            // Queue is empty
            return false;
        }
        item = buffer_[tail];
        tail_.store((tail + 1) & (size_ - 1), std::memory_order_release);
        return true;
    }

private:
    std::vector<T> buffer_;
    const size_t size_;
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
};

void producer(LockFreeQueue<int>& queue, int num_items) {
    for (int i = 0; i < num_items; ++i) {
        while (!queue.enqueue(i)) {
            // Busy-wait until the item is enqueued
        }
    }
}

void consumer(LockFreeQueue<int>& queue, int num_items) {
    int item;
    for (int i = 0; i < num_items; ++i) {
        while (!queue.dequeue(item)) {
            // Busy-wait until the item is dequeued
        }
    }
}

int main() {
    const int queue_size = 1024;  // Size must be a power of 2
    const int num_items = 1000000;  // Number of items the producer will produce

    LockFreeQueue<int> queue(queue_size);

    auto start_time = std::chrono::high_resolution_clock::now();

    // Create producer and consumer threads
    std::thread producer_thread(producer, std::ref(queue), num_items);
    std::thread consumer_thread(consumer, std::ref(queue), num_items);

    // Join producer and consumer threads
    producer_thread.join();
    consumer_thread.join();

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end_time - start_time;

    std::cout << "Test completed in " << duration.count() << " seconds." << std::endl;

    return 0;
}
```