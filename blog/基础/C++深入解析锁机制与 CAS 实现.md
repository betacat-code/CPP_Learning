
# 锁机制


在锁机制的应用中，乐观锁和悲观锁是两种常见的并发控制策略，它们主要在处理数据的一致性和并发操作时表现出不同的假设和实现方式。

## 乐观锁

乐观锁基于这样一个假设：**冲突发生的概率很低**，因此在数据操作过程中不会主动去锁定资源，而是在数据提交更新时才进行检查。如果发现冲突（通常是通过版本号或时间戳来检测），则操作被拒绝，通常伴随着重试机制。乐观锁适用于读操作多但写操作少的场景，因为它可以减少锁的开销，提高系统的吞吐量。

常见的乐观锁机制有CAS（Compare-And-Swap），这是一种硬件支持的乐观锁机制，用于多线程编程。它涉及三个操作数：内存位置、预期原值和新值。如果位置的当前值与预期值相匹配，就将这个位置的数据更新为新值。这通常用于实现无锁编程构建。

## 悲观锁

悲观锁则是假设冲突总是会发生，因此在整个数据处理过程中会主动加锁。这种锁可以通过数据库的行锁或表锁实现，或者在编程中使用互斥锁等。悲观锁通过锁定资源来防止其他操作影响到当前事务的执行，适用于写操作多的场景，但可能会引起锁竞争和降低并发性能。

**悲观锁会导致其它所有需要锁的线程挂起，等待持有锁的线程释放锁。**


常见的悲观锁机制如下：
- 互斥锁（Mutex）：属于悲观锁的一种，因为它主动阻止多个线程同时执行共享资源的访问。
- 读写锁（Read-Write Lock）：也是悲观锁的一种，因为它通过区分读和写操作来减少锁的竞争，但仍然在资源访问前加锁。
- 自旋锁（Spinlock）：属于悲观锁的一种，用于防止线程在执行短期任务时被系统挂起。
- 递归锁（Recursive Lock）：属于悲观锁的一种，因为它允许同一线程多次获得同一个锁。
- 条件变量（Condition Variable）：通常与互斥锁一起使用，用于线程间的协调，虽然本身不是锁，但配合锁使用时属于悲观锁策略。
- 信号量（Semaphore）：可以视为悲观锁的一种广义形式，因为它通过控制资源的数量来限制线程并发访问。

悲观锁锁机制存在的问题：
- 在多线程竞争下，加锁、释放锁会导致比较多的上下文切换和调度延时，引起性能问题。
- 一个线程持有锁会导致其它所有需要此锁的线程挂起。
- 如果一个优先级高的线程等待一个优先级低的线程释放锁会导致优先级倒置，引起性能风险。

# CAS机制

CAS，是Compare and Swap的简称，在这个机制中有三个核心的参数：

- 主内存中存放的共享变量的值：V（一般情况下这个V是内存的地址值，通过这个地址可以获得内存中的值）
- 工作内存中共享变量的副本值，也叫预期值：A
- 需要将共享变量更新到的最新值：B

## CAS算法原理描述

1.在对变量进行计算之前(如 ++ 操作)，首先读取原变量值，称为 旧的预期值 A

2.然后在更新之前再获取当前内存中的值，称为 当前内存值 V

3.如果 A==V 则说明变量从未被其他线程修改过，此时将会写入新值 B

4.如果 A!=V 则说明变量已经被其他线程修改过，当前线程应当什么也不做。

用C语言来描述该操作

```cpp
int compare_and_swap (int* reg, int oldval, int newval) 
{   
      int old_reg_val = *reg;   
      if (old_reg_val == oldval)      
               *reg = newval;   
      return old_reg_val; 
} 
```

变种为返回bool值形式的操作：返回 bool值的好处在于，调用者可以知道有没有更新成功

```cpp
bool compare_and_swap (int *accum, int *dest, int newval)
{   
      if ( *accum == *dest ) 
      {       
           *dest = newval;       
           return true;   
      }   
      return false; 
} 
```

## 其他操作

除了CAS还有以下原子操作： 

Fetch And Add，一般用来对变量做 +1 的原子操作。

```cpp
<< atomic >>
function FetchAndAdd(address location, int inc) {
    int value := *location
    *location := value + inc
    return value
}
```

Test-and-set，写值到某个内存位置并传回其旧值。汇编指令BST。

```cpp
#define LOCKED 1
 
int TestAndSet(int* lockPtr) {
    int oldValue;
 
    // Start of atomic segment
    // The following statements should be interpreted as pseudocode for
    // illustrative purposes only.
    // Traditional compilation of this code will not guarantee atomicity, the
    // use of shared memory (i.e. not-cached values), protection from compiler
    // optimization, or other required properties.
    oldValue = *lockPtr;
    *lockPtr = LOCKED;
    // End of atomic segment
 
    return oldValue;
}
```

Test and Test-and-set，用来实现多核环境下互斥锁，

```cpp
boolean locked := false // shared lock variable
procedure EnterCritical() {
  do {
    while (locked == true) skip // spin until lock seems free
  } while TestAndSet(locked) // actual atomic locking
}
```



## C/C++程序中CAS的实现


在 GCC 4.1 及以上版本中，提供了内置的 CAS 支持，主要通过以下两个函数实现：

```cpp
bool __sync_bool_compare_and_swap (type *ptr, type oldval, type newval, ...) 
type __sync_val_compare_and_swap (type *ptr, type oldval, type newval, ...)
```

__sync_bool_compare_and_swap：此函数检查指针 ptr 指向的值是否等于 oldval，如果是，则将 ptr 指向的值设置为 newval。返回值为 bool 类型，表示是否成功替换。

__sync_val_compare_and_swap：此函数的功能类似于 __sync_bool_compare_and_swap，但返回的是操作前的原始值，而不是操作的成功与否。

C++11 标准引入了更为标准化的原子操作，通过atomic头文件中定义的 std::atomic 类来实现。该类提供了多个成员函数，其中两个用于 CAS 操作的是：

```cpp
template< class T > bool atomic_compare_exchange_weak( std::atomic* obj,T* expected, T desired ); 
template< class T > bool atomic_compare_exchange_strong( volatile std::atomic* obj,T* expected, T desired );
```
atomic_compare_exchange_weak：尝试将 std::atomic 类型对象 obj 中的值与 expected 比较，如果相同，则将其替换为 desired。这个操作可能失败，即使在没有数据竞争的情况下也可能因为硬件优化而失败，因此它是弱比较交换。如果需要保证严格的原子性，则应该使用compare_exchange_strong函数。

atomic_compare_exchange_strong：与 atomic_compare_exchange_weak 类似，但其提供更强的保证，即不会因为假共享等硬件优化原因而失败。


>注意，compare_exchange_strong函数保证原子性，因此它的效率可能比compare_exchange_weak低。
## ABA问题及解决方案


CAS在操作的时候会检查变量的值是否被更改过，如果没有则更新值，但是带来一个问题，最开始的值是A，接着变成B，最后又变成了A。经过检查这个值确实没有修改过，因为最后的值还是A，但是实际上这个值确实已经被修改过了。

为了解决这个问题，在每次进行操作的时候加上一个版本号，每次操作的就是两个值，一个版本号和某个值，A——>B——>A问题就变成了1A——>2B——>3A。

真正要做到严谨的CAS机制，**我们在compare阶段不仅要比较期望值A和地址V中的实际值，还要比较变量的版本号是否一致**。

带版本号的 CAS：这种方法通过将版本号或标签与数据值配对来解决ABA问题。每次更新数据时，除了改变数据本身，还会更新版本号。这样，即使数据值返回到原始值，版本号的改变也会阻止CAS操作错误地认为没有其他线程修改过数据。

一种可行的方法为：在指针后追加一个计数器，每次操作时增加计数。因此，即使一个元素（比如A）被出队并后来又有一个相同内存地址的新元素（如C）被入队，计数器的变化将防止CAS操作错误地认为头节点没有被改变。这是因为计数器的值将不匹配，CAS操作会失败，有效防止了ABA问题。

```cpp
struct alignas(16) AtomicWord
{
    intptr_t p, num;
};
```

p存储节点指针，num存储应用计数。




对于Windows平台下，_InterlockedCompareExchange128 用于执行 128 位的原子比较和交换操作。这个函数尝试将两个 64 位值原子地比较与目标地址中的两个连续的 64 位值，如果它们匹配，则用新的两个 64 位值替换它们。

```cpp
static inline bool AtomicCompareExchangeStrongExplicit(volatile AtomicWord* p, 
AtomicWord* oldval, AtomicWord newval)
{
    return _InterlockedCompareExchange128((volatile long long*)p, (long long)newval.p, 
    (long long)newval.num, (long long*)oldval) != 0;
}
```

 函数尝试将 AtomicWord2 结构体中的 p 和 num 值原子地与给定的 oldval 进行比较。如果当前值（指针 p 指向的值）与 oldval 相匹配，则将这两个值替换为 newval 中的 lo 和 hi。如果成功，函数返回非零值，否则返回零。


> 对应于 Windows 的 _InterlockedCompareExchange128 的功能可以通过 GCC 提供的__atomic_compare_exchange 和 __atomic_compare_exchange_n 内置函数来实现。这些函数支持执行宽度达到 128 位的原子比较和交换操作。



## 效率测试

在Linux上测试，以下为无锁和有锁，模拟高并发情况。

```cpp
int mutex = 0;
int lock = 0;
int unlock = 1;
//无锁
static volatile int count = 0;
void *test_func(void *arg)
{
    int i = 0;
    for(i = 0; i < 2000000; i++)
    {
        while (!(__sync_bool_compare_and_swap (&mutex,lock, 1) ))usleep(100000);
        count++;
        __sync_bool_compare_and_swap (&mutex, unlock, 0);
    }
    return NULL;
}
```


```cpp
// 有锁
pthread_mutex_t mutex_lock;
static volatile int count = 0;
void *test_func(void *arg)
{
    int i = 0;
    for(i = 0; i < 2000000; i++)
    {
        pthread_mutex_lock(&mutex_lock);
        count++;
        pthread_mutex_unlock(&mutex_lock);
    }
    return NULL;
}

```

自行测试：无锁操作在性能上优于加锁操作，消耗时间仅为加锁操作的1/3左右，无锁编程方式确实能够比传统加锁方式效率高

## 缺点

看起来CAS比锁的效率高，从阻塞机制变成了非阻塞机制，减少了线程之间等待的时间。每个方法不能绝对的比另一个好，在线程之间竞争程度大的时候，如果使用CAS，每次都有很多的线程在竞争，也就是说CAS机制不能更新成功。这种情况下CAS机制会一直重试，这样就会比较耗费CPU。因此可以看出，如果线程之间竞争程度小，使用CAS是一个很好的选择；但是如果竞争很大，使用锁可能是个更好的选择。
