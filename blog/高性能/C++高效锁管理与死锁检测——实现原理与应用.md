# 背景
在项目使用多进程、多线程过程中，因争夺资源而造成一种资源竞态，所以需加锁处理。如下图所示，线程 A 想获取线程 B 的锁，线程 B 想获取线程 C 的锁，线程 C 想获取线程 D 的锁， 线程 D 想获取线程 A 的锁，从而构建了一个资源获取环，当进程或者线程申请的锁处于相互交叉锁住的情况，就会出现死锁，它们将无法继续运行。

死锁的存在是因为有资源获取环的存在，所以只要能检测出资源获取环，就等同于检测出死锁的存在。

# 设计方案

本文实现的是一个锁管理器，提供加锁解锁功能，同时提供检测死锁功能，出现死锁后释放部分资源来解决死锁。死锁的检测是通过检测死锁图中有没有环来实现的，如果对于请求同一资源的两个锁L1和L2(其对应的进程为P1和P2)，L1已经获得资源而L2在等待，则死锁图中有一条边P2->P1。

有向图中环的检测，即找到图中所有的强连通分量，使用Tarjan算法来实现，可以在O(E+V)时间找到所有的环。死锁图一般是比较稀疏的图，存储使用邻接表。

锁的数据结构为：

```cpp
class Lock {
public:
    Lock(int p, int res, int stat) {
        pid = p;
        res_id = res;
        state = stat;
    }
    int pid;
    int res_id;
    int state; //0 == locked, 1 == waiting
};
```

锁的状态有两种，已持有，等待。对于同一个资源加的锁放在链表中，方便检索和随机位置的删除。如果一个锁L1是资源R1对应链表的头，则他是一个已经持有的锁，链表其他位置的锁Ln都在阻塞等待L1释放，因此在死锁图中新建 Ln.pid -> L1.pid 边。

锁管理器的类声明如下，实现了后台线程进行死锁检测。

```cpp
class LockManager {
public:
    static LockManager& getInstance();
    ~LockManager();
    // 获取锁，返回一个指向Lock对象的shared_ptr，ret表示结果
    shared_ptr<Lock> getLock(int pid, int res_id, int& ret);
    // 查找指定pid和res_id的锁，返回一个指向Lock对象的shared_ptr
    shared_ptr<Lock> findLock(int pid, int res_id);
    // 释放锁
    int releaseLock(shared_ptr<Lock> lock);
    // 检测是否有死锁，若有，设置tokill为需要解除锁的pid
    bool isDeadLock(int& pid);
    void print();
    // 启动死锁检测器，interval为检测间隔
    void startDetection(int interval);
    void stopDeadlockDetector();

private:
    LockManager();
    // 计算强连通分量(SCC)，存储在pid_to_SCCid中
    void calSCC(map<int, vector<int>>& pid_to_SCCid);
    // 释放指定pid的所有锁
    void releaseProcess(int pid); 
    // 内部获取锁的实现，返回一个指向Lock对象的shared_ptr，possible表示是否可能发生死锁
    shared_ptr<Lock> getLockInternal(int pid, int res_id, bool& possible);
    // 内部释放锁的实现，返回操作结果，possible表示是否可能发生死锁
    int releaseLockInternal(shared_ptr<Lock> lock, bool& possible);
    void detectDeadlock();

    map<int, list<shared_ptr<Lock>>> res_to_locklist;// 资源ID到锁列表的映射
    map<int, pair<int, list<shared_ptr<Lock>>>> pid_to_locks;// 进程ID到锁计数和锁列表的映射
    map<int, list<int>> lock_graph;// 锁图，表示进程之间的等待依赖关系
    set<int> pid_set;// 进程ID集合，用于跟踪所有活跃的进程ID
    // 用于后台线程检测死锁的参数
    thread* deadlock_checker;// 指向死锁检测器线程的指针
    bool stop;
    int check_interval;
    mutex mtx;
};
```

# 具体实现

## 创建锁节点

获取锁：当一个进程 (pid) 请求一个资源 (res_id) 的锁时，会调用 getLock 方法。该方法首先检查该进程是否已经拥有该资源的锁（通过 findLock 方法）。

```cpp
shared_ptr<Lock> LockManager::getLock(int pid, int res_id, int& ret) {
    bool deadlock_possible;
    ret = 0;
    if (findLock(pid, res_id) != nullptr) {
        ret = 1; 
        return nullptr;
    }
    mtx.lock();
    auto p = getLockInternal(pid, res_id, deadlock_possible);
    mtx.unlock();
    return p;
}
```

内部获取锁逻辑：
- 如果没有找到重复的锁，会调用内部方法 getLockInternal 来实际创建并获取锁。
- 在 getLockInternal 方法中，会根据资源是否已经被其他进程锁定来创建不同状态的锁对象：
- 如果资源没有被锁定，创建一个状态为 0 的锁对象（表示已锁定）。
- 如果资源已经被其他进程锁定，创建一个状态为 1 的锁对象（表示等待）。

```cpp
shared_ptr<Lock> LockManager::getLockInternal(int pid, int res_id, bool& deadlock_possible) {
    deadlock_possible = false; // 初始无死锁
    if (!res_to_locklist.count(res_id)) res_to_locklist[res_id] = list<shared_ptr<Lock>>{};
    if (!pid_to_locks.count(pid)) pid_to_locks[pid] = make_pair(0, list<shared_ptr<Lock>>{});
    pid_set.insert(pid); // 加入线程

    shared_ptr<Lock> newlock;
    if (res_to_locklist[res_id].size() == 0) {
        newlock = make_shared<Lock>(pid, res_id, 0);
        res_to_locklist[res_id].push_back(newlock);
        pid_to_locks[pid].first++;
        pid_to_locks[pid].second.push_back(newlock);
    }
    else {
        newlock = make_shared<Lock>(pid, res_id, 1);
        res_to_locklist[res_id].push_back(newlock);
        pid_to_locks[pid].second.push_back(newlock);
        if (pid_to_locks[pid].first > 0) {
            const auto& first_lock = res_to_locklist[res_id].front();
            int p0id = first_lock->pid;
            if (!lock_graph.count(pid)) lock_graph[pid] = list<int>{};
            lock_graph[pid].push_back(p0id);

            deadlock_possible = true; // 可能发生死锁需要检查
        }
    }
    return newlock;
}
```

## 释放锁

调用 releaseLock 方法释放锁对象。releaseLock 方法调用 releaseLockInternal 方法，实际进行锁释放操作。

内部释放锁的逻辑：
- 从 res_to_locklist 中移除该锁对象。
- 从 pid_to_locks 中移除该锁对象。
- 如果锁对象的状态是 0（已锁定）且资源上仍有其他锁，则将资源上的下一个等待锁（状态为 1）转换为已锁定状态（状态为 0），并更新相应进程的锁计数。
- 更新依赖关系图 (lock_graph)：
- 移除当前进程到所有依赖于它的进程的边。
- 添加新的依赖关系，即新的持有锁的进程到其他等待进程的边。

```cpp
int LockManager::releaseLock(shared_ptr<Lock> lock) {
    bool deadlock_possible;
    mtx.lock();
    releaseLockInternal(lock, deadlock_possible);
    mtx.unlock();
    return 0;
}
int LockManager::releaseLockInternal(shared_ptr<Lock> lock, bool& deadlock_possible) {
    deadlock_possible = false; // 初始化为没有死锁的可能性
    int pid = lock->pid; // 获取锁的进程ID
    int res_id = lock->res_id; // 获取锁的资源ID

    auto& locklist = res_to_locklist[res_id]; // 获取资源对应的锁列表
    locklist.remove(lock); // 从资源的锁列表中移除该锁
    pid_to_locks[pid].second.remove(lock); // 从进程的锁列表中移除该锁

    printf("release lock(pid=%d, res_id=%d, state=%d)\n", pid, res_id, lock->state);
    if (lock->state == 0 && locklist.size() > 0) { // 如果释放的是已锁定状态的锁，且资源上还有其他等待的锁
        pid_to_locks[pid].first--; // 减少该进程的锁计数
        int p0id = locklist.front()->pid; // 获取新获得锁的进程ID
        locklist.front()->state = 0; // 将等待的锁状态改为已锁定
        pid_to_locks[p0id].first++; // 增加新获得锁的进程的锁计数

        for (auto it = locklist.begin(); it != locklist.end(); it++) { // 更新依赖关系图
            int p1id = (*it)->pid;
            lock_graph[p1id].remove(pid); // 移除指向释放锁的进程的依赖关系
            printf("remove edge(%d->%d)\n", p1id, pid);
            if (p1id != p0id) {
                lock_graph[p1id].push_back(p0id); // 添加新的依赖关系，指向新获得锁的进程
                printf("add edge(%d->%d)\n", p1id, p0id);
            }
        }
        // 可能导致死锁，需要检查
        deadlock_possible = true;
    }

    return 0;
}
```

## Kosaraju算法

对反向图进行拓扑排序，并按照拓扑排序的逆序进行深度优先搜索 (DFS)，是为了高效地找到原始图中的强连通分量 (SCC)。这种方法称为 Kosaraju算法，其主要思想是：

拓扑排序 确定访问顺序：
- 对反向图进行拓扑排序，可以得到一个访问顺序，使得在原图中从某个节点出发的所有可能路径都被访问到。
- 拓扑排序保证了在原图中，某个节点的所有后继节点在排序中都在它之前。这有助于后续步骤中的 SCC 检测。

逆序DFS 高效找到 SCC：
- 按照拓扑排序的逆序进行 DFS 确保每次从尚未访问的节点出发时，能够遍历一个完整的强连通分量。
- 由于拓扑排序的逆序保证了我们从图的“后面”开始访问（即从没有后继节点的节点开始），所以每次 DFS 都会完全包含一个 SCC。

这种方法的效率很高，因为每个节点和每条边都只被访问两次（一次在拓扑排序时，一次在逆序 DFS 时），所以 Kosaraju 算法的时间复杂度是 O(V + E)，其中 V 是节点数，E 是边数。


死锁检测是通过计算锁图的强连通分量 (SCC) 来实现的。首先通过 reverseGraph 方法构建锁图的反向图。

```cpp
void reverseGraph(map<int, list<int>>& origin, map<int, list<int>>& dest) {
    for (const auto& p : origin) {
        int e = p.first;
        const auto& vec = p.second;
        for (auto v : vec) {
            if (!dest.count(v)) dest[v] = list<int>{};
            dest[v].push_back(e);
        }
    }
}
```

通过 topoSort 方法对反向图进行拓扑排序，按照拓扑排序的逆序进行DFS。

```cpp
void dfs(map<int, list<int>>& graph, int cur, set<int>& visited, vector<int>& topo_order) {
    if (visited.count(cur)) return;
    visited.insert(cur);
    for (auto x : graph[cur]) {
        dfs(graph, x, visited, topo_order);
    }
    topo_order.push_back(cur);
}

void topoSort(map<int, list<int>>& graph, set<int>& pid_set, vector<int>& topo_order) {
    set<int> visited;
    for (auto x : pid_set) {
        dfs(graph, x, visited, topo_order);
    }
}
```

通过深度优先搜索 (DFS) 来计算锁图中的强连通分量。检查是否存在强连通分量。如果存在，说明有死锁，并选择一个进程进行终止以打破死锁。

```cpp
void LockManager::calSCC(map<int, vector<int>>& pid_to_SCCid) {
    map<int, list<int>> reverse_graph;
    reverseGraph(lock_graph, reverse_graph); // 构建反向图
    vector<int> topo_order;
    topoSort(reverse_graph, pid_set, topo_order); // 对反向图进行拓扑排序

    set<int> visited;
    for (int i = topo_order.size() - 1; i >= 0; i--) { // 按照拓扑排序的逆序进行DFS
        auto vec = vector<int>{};
        dfs(lock_graph, topo_order[i], visited, vec);
        if (vec.size() > 1) pid_to_SCCid[i] = vec; // 找到强连通分量
    }
}
```

## 后台检测死锁

isDeadLock是实际进行检测死锁的函数：

计算强连通分量 (SCC)：调用 calSCC 方法，计算图中的强连通分量并将结果存储。每个强连通分量表示一个可能的死锁环。

遍历每个强连通分量：对于每个强连通分量中的进程，打印其进程ID，并找到锁数量最少的进程。

选择要终止的进程：如果找到了锁数量最少的进程，将其标记为 tokill。通过终止该进程来打破死锁。

如果存在强连通分量，返回 true 表示存在死锁


```cpp
bool LockManager::isDeadLock(int& tokill) {
    map<int, vector<int>> SCCid_to_pids;
    calSCC(SCCid_to_pids); // 计算强连通分量
    for (const auto& cyc : SCCid_to_pids) { // 遍历每个强连通分量
        const auto& vec = cyc.second;
        printf("detected deadlock: ");
        int minlocks = 1e8;
        int minpid = -1;
        for (auto it = vec.begin(); it != vec.end(); it++) { // 打印并找到最少锁的进程
            printf("%d->", *it);
            int nlock = pid_to_locks[*it].first;
            if (nlock < minlocks) {
                minlocks = nlock;
                minpid = *it;
            }
        }
        printf("%d\n", vec.front());
        if (minpid != -1) {
            printf("will release pid=%d(%d) to break deadlock\n", minpid, minlocks);
            tokill = minpid; // 选择需要终止的进程
        }
    }
    return SCCid_to_pids.size() > 0; // 如果存在强连通分量，说明存在死锁
}
```

detectDeadlock方法是一个后台线程，用于定期检测死锁并处理死锁。

```cpp
LockManager::LockManager() {
    stop = false;
    check_interval = 1;
    deadlock_checker = new thread([this] { this->detectDeadlock(); });
}
void LockManager::detectDeadlock() {
    using std::chrono::system_clock;
    while (!stop) {
        int tokill;
        mtx.lock();
        while (isDeadLock(tokill)) {
            releaseProcess(tokill);
        }
        mtx.unlock();
        // 检查死锁
        std::time_t tt = system_clock::to_time_t(system_clock::now());
        struct std::tm* ptm = std::localtime(&tt);
        ptm->tm_sec += check_interval;
        std::this_thread::sleep_until(system_clock::from_time_t(mktime(ptm)));
    }
}
```


# 完整代码

```cpp
#include <iostream>
#include <memory>
#include <map>
#include <list>
#include <vector>
#include <set>
#include <string>
#include <thread>
#include <chrono>
#include <mutex>
using namespace std;

class Lock {
public:
    Lock(int p, int res, int stat) {
        pid = p;
        res_id = res;
        state = stat;
    }
    int pid;
    int res_id;
    int state; //0 == locked, 1 == waiting
};

typedef pair<int, list<shared_ptr<Lock> > > pivec;


class LockManager {
public:
    static LockManager& getInstance();
    ~LockManager();
    // 获取锁，返回一个指向Lock对象的shared_ptr，ret表示结果
    shared_ptr<Lock> getLock(int pid, int res_id, int& ret);
    // 查找指定pid和res_id的锁，返回一个指向Lock对象的shared_ptr
    shared_ptr<Lock> findLock(int pid, int res_id);
    // 释放锁
    int releaseLock(shared_ptr<Lock> lock);
    // 检测是否有死锁，若有，设置tokill为需要解除锁的pid
    bool isDeadLock(int& pid);
    void print();
    // 启动死锁检测器，interval为检测间隔
    void startDetection(int interval);
    void stopDeadlockDetector();

private:
    LockManager();
    // 计算强连通分量(SCC)，存储在pid_to_SCCid中
    void calSCC(map<int, vector<int>>& pid_to_SCCid);
    // 释放指定pid的所有锁
    void releaseProcess(int pid); 
    // 内部获取锁的实现，返回一个指向Lock对象的shared_ptr，possible表示是否可能发生死锁
    shared_ptr<Lock> getLockInternal(int pid, int res_id, bool& possible);
    // 内部释放锁的实现，返回操作结果，possible表示是否可能发生死锁
    int releaseLockInternal(shared_ptr<Lock> lock, bool& possible);
    void detectDeadlock();

    map<int, list<shared_ptr<Lock>>> res_to_locklist;// 资源ID到锁列表的映射
    map<int, pair<int, list<shared_ptr<Lock>>>> pid_to_locks;// 进程ID到锁计数和锁列表的映射
    map<int, list<int>> lock_graph;// 锁图，表示进程之间的等待依赖关系
    set<int> pid_set;// 进程ID集合，用于跟踪所有活跃的进程ID
    // 用于后台线程检测死锁的参数
    thread* deadlock_checker;// 指向死锁检测器线程的指针
    bool stop;
    int check_interval;
    mutex mtx;
};

LockManager::LockManager() {
    stop = false;
    check_interval = 1;
    deadlock_checker = new thread([this] { this->detectDeadlock(); });
}

LockManager::~LockManager() {
    stopDeadlockDetector();
}

LockManager& LockManager::getInstance() {
    static LockManager inst;
    return inst;
}

shared_ptr<Lock> LockManager::getLock(int pid, int res_id, int& ret) {
    bool deadlock_possible;
    ret = 0;
    if (findLock(pid, res_id) != nullptr) {
        ret = 1; 
        return nullptr;
    }
    mtx.lock();
    auto p = getLockInternal(pid, res_id, deadlock_possible);
    mtx.unlock();
    return p;
}

shared_ptr<Lock> LockManager::getLockInternal(int pid, int res_id, bool& deadlock_possible) {
    deadlock_possible = false; 
    if (!res_to_locklist.count(res_id)) res_to_locklist[res_id] = list<shared_ptr<Lock>>{};
    if (!pid_to_locks.count(pid)) pid_to_locks[pid] = make_pair(0, list<shared_ptr<Lock>>{});
    pid_set.insert(pid);

    shared_ptr<Lock> newlock;
    if (res_to_locklist[res_id].size() == 0) {
        newlock = make_shared<Lock>(pid, res_id, 0);
        res_to_locklist[res_id].push_back(newlock);
        pid_to_locks[pid].first++;
        pid_to_locks[pid].second.push_back(newlock);
    }
    else {
        newlock = make_shared<Lock>(pid, res_id, 1);
        res_to_locklist[res_id].push_back(newlock);
        pid_to_locks[pid].second.push_back(newlock);
        if (pid_to_locks[pid].first > 0) {
            const auto& first_lock = res_to_locklist[res_id].front();
            int p0id = first_lock->pid;
            if (!lock_graph.count(pid)) lock_graph[pid] = list<int>{};
            lock_graph[pid].push_back(p0id);

            deadlock_possible = true;
        }
    }
    return newlock;
}

shared_ptr<Lock> LockManager::findLock(int pid, int res_id) {
    shared_ptr<Lock> result = nullptr;
    mtx.lock();
    for (const auto& p : pid_to_locks[pid].second) {
        if (p->res_id == res_id) result = p;
    }
    mtx.unlock();
    return result;
}

int LockManager::releaseLock(shared_ptr<Lock> lock) {
    bool deadlock_possible;
    mtx.lock();
    releaseLockInternal(lock, deadlock_possible);
    mtx.unlock();
    return 0;
}

int LockManager::releaseLockInternal(shared_ptr<Lock> lock, bool& deadlock_possible) {
    deadlock_possible = false;
    int pid = lock->pid;
    int res_id = lock->res_id;

    auto& locklist = res_to_locklist[res_id];
    locklist.remove(lock);
    pid_to_locks[pid].second.remove(lock);

    printf("release lock(pid=%d, res_id=%d, state=%d)\n", pid, res_id, lock->state);
    if (lock->state == 0 && locklist.size() > 0) {
        pid_to_locks[pid].first--;
        int p0id = locklist.front()->pid;
        locklist.front()->state = 0;
        pid_to_locks[p0id].first++;

        for (auto it = locklist.begin(); it != locklist.end(); it++) {
            int p1id = (*it)->pid;
            lock_graph[p1id].remove(pid); 
            printf("remove edge(%d->%d)\n", p1id, pid);
            if (p1id != p0id) {
                lock_graph[p1id].push_back(p0id);
                printf("add edge(%d->%d)\n", p1id, p0id);
            }
        }
        deadlock_possible = true;
    }

    return 0;
}

bool LockManager::isDeadLock(int& tokill) {
    map<int, vector<int>> SCCid_to_pids;
    calSCC(SCCid_to_pids);
    for (const auto& cyc : SCCid_to_pids) {
        const auto& vec = cyc.second;
        printf("detected deadlock: ");
        int minlocks = 1e8;
        int minpid = -1;
        for (auto it = vec.begin(); it != vec.end(); it++) {
            printf("%d->", *it);
            int nlock = pid_to_locks[*it].first;
            if (nlock < minlocks) {
                minlocks = nlock;
                minpid = *it;
            }
        }
        printf("%d\n", vec.front());
        if (minpid != -1) {
            printf("will release pid=%d(%d) to break deadlock\n", minpid, minlocks);
            tokill = minpid;
        }
    }
    return SCCid_to_pids.size() > 0;
}

void LockManager::releaseProcess(int pid) {
    if (pid_to_locks.count(pid)) {
        list<shared_ptr<Lock>> tmplist = pid_to_locks[pid].second;
        for (const auto& p_lock : tmplist) {
            bool possible; 
            releaseLockInternal(p_lock, possible);
        }
        pid_to_locks.erase(pid);
    };
    if (lock_graph.count(pid)) lock_graph.erase(pid);
    pid_set.erase(pid);
    printf("erase pid %d\n", pid);
}

//死锁检测

void LockManager::startDetection(int interval) {
    if (deadlock_checker != nullptr) {
        check_interval = interval;
        deadlock_checker = new thread([this] {
            this->detectDeadlock();
            });
    }
}

void LockManager::stopDeadlockDetector() {
    stop = true;
    if (deadlock_checker && deadlock_checker->joinable()) {
        printf("deadlock detector is stoped\n");
        deadlock_checker->join();
        deadlock_checker = nullptr;
    }
}

void LockManager::detectDeadlock() {
    using std::chrono::system_clock;
    while (!stop) {
        int tokill;
        mtx.lock();
        while (isDeadLock(tokill)) {
            releaseProcess(tokill);
        }
        mtx.unlock();

        // 检查死锁
        std::time_t tt = system_clock::to_time_t(system_clock::now());
        struct std::tm* ptm = std::localtime(&tt);
        ptm->tm_sec += check_interval;
        std::this_thread::sleep_until(system_clock::from_time_t(mktime(ptm)));
    }
}


// 计算SCC的辅助函数

void reverseGraph(map<int, list<int>>& origin, map<int, list<int>>& dest) {
    for (const auto& p : origin) {
        int e = p.first;
        const auto& vec = p.second;
        for (auto v : vec) {
            if (!dest.count(v)) dest[v] = list<int>{};
            dest[v].push_back(e);
        }
    }
}

void dfs(map<int, list<int>>& graph, int cur, set<int>& visited, vector<int>& topo_order) {
    if (visited.count(cur)) return;
    visited.insert(cur);
    for (auto x : graph[cur]) {
        dfs(graph, x, visited, topo_order);
    }
    topo_order.push_back(cur);
}

void topoSort(map<int, list<int>>& graph, set<int>& pid_set, vector<int>& topo_order) {
    set<int> visited;
    for (auto x : pid_set) {
        dfs(graph, x, visited, topo_order);
    }
}

void printVec(vector<int>& vec) {
    printf("vector[%d", vec.front());
    for (auto it = vec.begin() + 1; it != vec.end(); it++) {
        printf(",%d", *it);
    }
    printf("]\n");
}

void LockManager::calSCC(map<int, vector<int>>& pid_to_SCCid) {
    map<int, list<int>> reverse_graph;
    reverseGraph(lock_graph, reverse_graph);
    vector<int> topo_order;
    topoSort(reverse_graph, pid_set, topo_order);
    set<int> visited;
    for (int i = topo_order.size() - 1; i >= 0; i--) {
        auto vec = vector<int>{};
        dfs(lock_graph, topo_order[i], visited, vec);
        if (vec.size() > 1) pid_to_SCCid[i] = vec;
    }
}

void LockManager::print() {
    cout << "pid to locks:[\n";
    for (const auto& p : pid_to_locks) {

        printf("(%d, %d, [", p.first, p.second.first);
        for (const auto& q : p.second.second) {
            printf("(pid=%d, res_id=%d, state=%d),", q->pid, q->res_id, q->state);
        }
        printf("])\n");
    }
    cout << ']' << endl << "res to locks:[\n";
    for (const auto& p : res_to_locklist) {
        printf("(%d, [", p.first);
        for (const auto& q : p.second) {
            printf("(pid=%d, res_id=%d, state=%d),", q->pid, q->res_id, q->state);
        }
        printf("])\n");
    }
    cout << ']' << endl << "graph:[\n";
    for (const auto& p : lock_graph) {
        int e = p.first;
        printf("%d:[", e);
        for (const auto& q : p.second) {
            printf("%d,", q);
        }
        printf("]\n");
    }
    cout << ']' << endl << "pid_set:[";
    for (auto x : pid_set) printf("%d, ", x);
    cout << ']' << endl;
}

int doGetLock(int pid, int rid) {
    static auto& lock_manager = LockManager::getInstance();
    int ret;
    printf("set lock(pid=%d, res_id=%d)\n", pid, rid);
    lock_manager.getLock(pid, rid, ret);
    if (ret == 1) {
        printf("lock(pid=%d, res_id=%d) is duplicated, lock failed\n", pid, rid);
    }
    return ret;
}

int doReleaseLock(int pid, int rid) {
    static auto& lock_manager = LockManager::getInstance();
    auto lock = lock_manager.findLock(pid, rid);
    if (lock != nullptr) lock_manager.releaseLock(lock);
    else printf("no such lock(pid=%d, res_id=%d)\n", pid, rid);
    return 0;
}

int main() {
    doGetLock(1, 2);
    doGetLock(1, 3);
    doGetLock(2, 2);
    doGetLock(3, 3);
    doGetLock(2, 3);
    doGetLock(3, 2);
    doReleaseLock(1, 2);
    doReleaseLock(1, 3);

    std::this_thread::sleep_for(std::chrono::seconds(2));
    doGetLock(5, 5);
    doGetLock(6, 6);
    doGetLock(5, 6);
    doGetLock(6, 5);

    int pid, resid;
    char tmp[40];
    while (scanf("%s %d %d", tmp, &pid, &resid) != EOF) {
        if (string(tmp) == "lock") {
            doGetLock(pid, resid);
        }
        else {
            doReleaseLock(pid, resid);
        }
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    LockManager::getInstance().stopDeadlockDetector();

    return 0;
}
```