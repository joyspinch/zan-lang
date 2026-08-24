# System.Threading

> 源码: `stdlib/System/Threading/AsyncGate.zan`, `stdlib/System/Threading/AsyncRwLock.zan`, `stdlib/System/Threading/BlockingQueue.zan`, `stdlib/System/Threading/Gate.zan`, `stdlib/System/Threading/SemaphoreSlim.zan`, `stdlib/System/Threading/Threading.zan`, `stdlib/System/Threading/Timer.zan`


## AsyncGate (class)

事件驱动的协程等待/通知原语（异步条件变量）。

由运行时 gate 实现（`Gate` / src/runtime/rt_io.c
中的 zan_gate_*）：等待方真正挂起在调度器上，
只有其他协程调用 `Signal` 时才被唤醒——零
轮询、无人为延迟、唤醒确定。等待前发出的信号
会被记账（计数语义），因此
`while (!ready) { await gate.Wait(); }` 式的重查惯用法不会丢失唤醒。

（取代了早期的回环 self-pipe 实现，后者需要
阻塞式套接字配置，且会在空闲连接上产生虚假唤醒。）

- long handle;

- AsyncGate()

- bool IsOpen()

- async bool Wait()

- void Signal()

- void Close()


## AsyncRwLock (class)

协程读写锁：多个并发读者或一个独占
写者。等待者真正挂起在调度器上（经由 AsyncGate）——
无轮询。写者优先：一旦写者排队，新读者
便需等待，持续的读流不会饿死写操作。

AsyncRwLock lock = new AsyncRwLock();
await lock.EnterRead();  ...  lock.ExitRead();
await lock.EnterWrite(); ...  lock.ExitWrite();

- int readers;

- bool writer;

- int writersWaiting;

- AsyncGate readGate;

- AsyncGate writeGate;

- AsyncRwLock()

- async void EnterRead()
  - 获取共享（读）访问；当写者持有锁
    或正在等待锁时挂起。

- void ExitRead()
  - 释放共享访问。

- async void EnterWrite()
  - 获取排他（写）访问；挂起直到所有读线程
    以及当前写线程都释放。

- void ExitWrite()
  - 释放排他访问，先唤醒排队的写线程，
    然后唤醒排队的读线程。

- int ReaderCount()
  - 当前读线程数（诊断用）。

- bool IsWriteLocked()
  - 当前是否有写线程持有锁（诊断用）。

- void Close()


## AtomicInt (class)

用于 worker 线程间共享的顺序一致 64 位整数。

- nint handle;

- [DllImport("crt", EntryPoint="zan_atomic_int_create")]static extern nint NativeCreate(long initialValue);

- [DllImport("crt", EntryPoint="zan_atomic_int_destroy")]static extern void NativeDestroy(nint handle);

- [DllImport("crt", EntryPoint="zan_atomic_int_load")]static extern long NativeLoad(nint handle);

- [DllImport("crt", EntryPoint="zan_atomic_int_store")]static extern void NativeStore(nint handle, long newValue);

- [DllImport("crt", EntryPoint="zan_atomic_int_exchange")]static extern long NativeExchange(nint handle, long newValue);

- [DllImport("crt", EntryPoint="zan_atomic_int_compare_exchange")]static extern long NativeCompareExchange(nint handle, long expected, long desired);

- [DllImport("crt", EntryPoint="zan_atomic_int_add")]static extern long NativeAdd(nint handle, long delta);

- AtomicInt(long initialValue)

- ~AtomicInt()

- bool IsValid()

- long Load()

- void Store(long newValue)

- long Exchange(long newValue)

- long CompareExchange(long expected, long desired)

- long Add(long delta)

- long Increment()

- long Decrement()


## BlockingQueue (class)

操作系统线程之间的交接队列——相当于 C# 的
<c>BlockingCollection</c> / <c>Channel</c>。


BlockingQueue<string> q = new BlockingQueue<string>(null, 0);
Thread.Start(() => { q.Enqueue("row"); q.Complete(); });
string row = q.Dequeue();          // 阻塞直到有数据或队列结束
while (row != null) { ...; row = q.Dequeue(); }


`Channel` 是协程原语：用于在任务之间传递值，
这些任务由单一线程的调度器驱动；跨真实线程时会自旋（或错过唤醒）
。本队列是跨线程版本：条目列表
受互斥锁保护，消费方无数据可取时挂起在操作系统
信号量上，直到生产方投递。队列完成并取空后，`Dequeue` 返回 null，
消费循环自行结束——`empty` 就是
队列当时交回的哨兵值，在 Create 时提供，因为
泛型值类型没有 null。

失败的生产方可以用
`CompleteWithFault` 代替普通的 `Complete`：
消费者先取走剩余数据，然后通过
`GetFault` 看到错误信息而不是干净的 EOF。`Cancel` 是
中止路径：丢弃所有仍排队的条目，唤醒所有挂起的
消费者和生产方，并标记队列，使 `IsCanceled`
返回 true。

- List<T> items;

- int head;
  - 下一个待取出条目的索引；列表惰性压缩，避免
    长生命周期队列每次取出都搬移所有元素。

- bool closed;
  - 由 Complete 设置：不再会有条目到达，挂起的消费者
    以 null 被唤醒，而不是永远等待。

- int waiters;

- int capacity;
  - 队列在让生产方等待前可容纳的条目数；0 表示
    无上限。

- int posters;
  - 因队列已满而挂起在 `slots` 上的生产方。

- T empty;
  - 在无条目可取时由 take 交回给调用方。

- string fault;
  - 来自 CompleteWithFault 的失败信息；"" 表示无故障。

- bool canceled;
  - 由 Cancel 设置：队列被中止，待处理条目已丢弃。

- nint lockHandle;

- nint sem;

- nint slots;

- string lockHandle;

- string sem;

- string slots;

- BlockingQueue()
  - 无界队列，哨兵值为默认值（引用类型为 null）。
    和双参构造器一样建立项列表与互斥锁/信号量：否则
    任何 Enqueue/Dequeue 都会在空句柄上段错。

- BlockingQueue(T empty, int capacity)
  - 最多容纳 `capacity` 个条目的队列：一旦满，
    生产方在 Enqueue 中挂起，直到消费者取走一个，因此快的
    生产方不会超过消费方，队列不会无限增长。
    容量 0 表示无上限。`empty` 是队列完成并取空后
    由 Dequeue 交回的哨兵值。

- bool Enqueue(T item)
  - 投递一个条目并唤醒挂起的消费者；有界队列满时
    挂起调用方。队列结束后返回 false。

- T TryDequeue()
  - 取出下一个条目；若队列当前
    没有数据，则返回 `empty` 哨兵。

- T DequeueFor(int timeoutMs)
  - 取出下一个条目，最多等待 `timeoutMs` 毫秒。
    等待超时或队列
    完成并取空时也返回 `empty` 哨兵；`IsCompleted` 可区分二者
    。负超时表示无限等待。

- T Dequeue()
  - 取出下一个条目，挂起调用线程直到有数据
    投递。队列完成并取空后返回 `empty` 哨兵
    。

- void Complete()
  - 结束队列：不再接受新条目，所有
    挂起的消费者都会被唤醒。

- void CompleteWithFault(string message)
  - 以失败结束队列。消费者先取走剩余条目，
    然后通过 `GetFault` 获取错误信息，而非干净的
    EOF；此后 Enqueue 拒绝新条目。

- void Cancel()
  - 中止队列：丢弃所有仍在等待的条目，唤醒所有
    挂起的消费者和生产方，并将队列标记为已取消，使
    `IsCanceled` 返回 true。消费者看到的是空哨兵
    （队列已关闭并取空）；Enqueue 返回 false。

- bool IsCanceled()
  - 在 `Cancel` 之后为 true：队列是被中止
    而非正常完成。

- string GetFault()
  - 来自 `CompleteWithFault` 的失败信息，或
    队列正常结束或被取消时为 ""。

- bool IsCompleted()

- int Count()
  - 等待被取出的条目数。

- void Dispose()

- T TakeLocked()
  - 弹出一个条目；调用方持有锁。当已消费前缀
    超过一半时压缩，保证两端均摊 O(1)。


## Channel (class)

用于协程间通信的协程感知 channel。
平台无关（纯 Zan 实现）。容量为 0 表示无界；
正容量表示有界缓冲；`CreateUnbuffered`
创建容量为 1 的 channel（阻塞当且仅当缓冲已满）。

- List<string> buffer;

- int capacity;

- bool closed;

- int sendWaiters;

- int recvWaiters;

- Gate sendGate;

- Gate recvGate;

- nint lockHandle;

- string lockHandle;

- Channel(int capacity)

- static Channel CreateUnbuffered()
  - 创建无缓冲 channel。

- async void Send(string item)
  - 向 channel 发送一个值（满时阻塞）。
    已关闭的 channel 上发送会被忽略。

- async string Receive()
  - 从 channel 接收一个值（空时阻塞）。
    关闭并取空后返回空字符串。

- void Close()
  - 关闭 channel，唤醒所有阻塞的
    发送方和接收方。

- bool IsClosed()
  - channel 已关闭时返回 true。

- int Count()
  - 返回当前缓冲条目数。


## Gate (class)

事件驱动的协程等待/通知原语，直接由
运行时调度器支撑（见 src/runtime/rt_io.c 的 zan_gate_*）。等待方
真正挂起——无定时器、无 <c>Task.Delay</c> 轮询——
并且仅当另一个协程调用
`Signal` 时才经由就绪队列重新就绪。

计数 / 条件变量语义：无等待方挂起时发出的 `Signal`
会被暂存，因此下一次 `Wait`
无需阻塞即可返回。这使得标准的重新检查写法安全：
`while (!available) { await gate.Wait(); }`
既不会丢失唤醒，也不会忙轮询。

- long handle;

- Gate()

- bool IsValid()
  - 门是否还持有一个可用的运行时句柄（即尚未
    `Close`）。这与"是否已 Signal"无关：刚 new 出来的门
    就是 true，暂存的信号数不可查询。

- bool IsOpen()
  - `IsValid` 的旧名字。保留是为了兼容，
    它从来就不表示"已打开/已 Signal"。

- async bool Wait()

- void Signal()

- void Close()

- long Handle()

- [DllImport("crt", EntryPoint="zan_gate_new")]static extern long zan_gate_new();

- [DllImport("crt", EntryPoint="zan_gate_signal")]static extern void zan_gate_signal(long h);

- [DllImport("crt", EntryPoint="zan_gate_free")]static extern void zan_gate_free(long h);


## Mutex (class)

用于线程同步的跨平台互斥锁。

- [DllImport("kernel32", EntryPoint="CreateMutexA")]static extern nint WinCreateMutex(nint secAttrs, int initialOwner, string name);

- [DllImport("kernel32", EntryPoint="WaitForSingleObject")]static extern int WinWaitForSingleObject(nint handle, int milliseconds);

- [DllImport("kernel32", EntryPoint="ReleaseMutex")]static extern int WinReleaseMutex(nint handle);

- [DllImport("kernel32", EntryPoint="CloseHandle")]static extern int WinCloseHandle(nint handle);

- [DllImport("crt", EntryPoint="calloc")]static extern string PthreadCalloc(long count, long size);

- [DllImport("crt", EntryPoint="free")]static extern void PthreadFree(string ptr);

- [DllImport("crt", EntryPoint="pthread_mutex_init")]static extern int PthreadMutexInit(string mutex, string attr);

- [DllImport("crt", EntryPoint="pthread_mutex_lock")]static extern int PthreadMutexLock(string mutex);

- [DllImport("crt", EntryPoint="pthread_mutex_unlock")]static extern int PthreadMutexUnlock(string mutex);

- [DllImport("crt", EntryPoint="pthread_mutex_destroy")]static extern int PthreadMutexDestroy(string mutex);

- static nint Create()

- static void Lock(nint handle)

- static void Unlock(nint handle)

- static void Destroy(nint handle)

- static string Create()

- static void Lock(string handle)

- static void Unlock(string handle)

- static void Destroy(string handle)


## Semaphore (class)

跨平台信号量。

- [DllImport("kernel32", EntryPoint="CreateSemaphoreA")]static extern nint WinCreateSemaphore(nint secAttrs, int initialCount, int maxCount, string name);

- [DllImport("kernel32", EntryPoint="WaitForSingleObject")]static extern int WinWaitForSingleObject(nint handle, int milliseconds);

- [DllImport("kernel32", EntryPoint="ReleaseSemaphore")]static extern int WinReleaseSemaphore(nint handle, int releaseCount, nint prevCount);

- [DllImport("kernel32", EntryPoint="CloseHandle")]static extern int WinCloseHandle(nint handle);

- [DllImport("crt", EntryPoint="dispatch_semaphore_create")]static extern string MacSemCreate(int initValue);

- [DllImport("crt", EntryPoint="dispatch_semaphore_wait")]static extern int MacSemWait(string sem, long timeout);

- [DllImport("crt", EntryPoint="dispatch_semaphore_signal")]static extern int MacSemSignal(string sem);

- [DllImport("crt", EntryPoint="dispatch_release")]static extern void MacSemRelease(string sem);

- [DllImport("crt", EntryPoint="dispatch_semaphore_wait")]static extern int MacSemWaitUntil(string sem, long deadline);

- [DllImport("crt", EntryPoint="dispatch_time")]static extern long MacDispatchTime(long baseTime, long deltaNs);

- [DllImport("crt", EntryPoint="calloc")]static extern string SemCalloc(long count, long size);

- [DllImport("crt", EntryPoint="free")]static extern void SemFree(string ptr);

- [DllImport("crt", EntryPoint="sem_init")]static extern int SemInit(string sem, int pshared, int initValue);

- [DllImport("crt", EntryPoint="sem_wait")]static extern int SemWait(string sem);

- [DllImport("crt", EntryPoint="sem_post")]static extern int SemPost(string sem);

- [DllImport("crt", EntryPoint="sem_destroy")]static extern int SemDestroy(string sem);

- [DllImport("crt", EntryPoint="sem_timedwait")]static extern int SemTimedWait(string sem, nint absTime);

- [DllImport("crt", EntryPoint="clock_gettime")]static extern int SemClockGettime(int clkId, nint tp);

- static nint Create(int initialCount, int maxCount)

- static void Wait(nint handle)

- static bool WaitFor(nint handle, int timeoutMs)
  - 最多等待 `timeoutMs` 毫秒获取一个许可。
    获取到返回 true；超时返回 false（此时不消耗许可）。
    负超时无限等待，0 则轮询。

- static void Release(nint handle)

- static void Destroy(nint handle)

- static string Create(int initialCount, int maxCount)

- static void Wait(string handle)

- static bool WaitFor(string handle, int timeoutMs)

- static void Release(string handle)

- static void Destroy(string handle)

- static string Create(int initialCount, int maxCount)

- static void Wait(string handle)

- static bool WaitFor(string handle, int timeoutMs)

- static void Release(string handle)

- static void Destroy(string handle)


## SemaphoreSlim (class)

计数节流器，其上限可在任务运行中调整——
相当于 C# 的 <c>SemaphoreSlim</c> 加上一个
可动态调整的 <c>maxConcurrency</c> 旋钮。


SemaphoreSlim gate = new SemaphoreSlim(4);   // 同时最多 4 个 worker
gate.Wait();
try { ... } finally { gate.Release(); }
gate.SetLimit(8);                               // 提高上限，唤醒等待者


准入判定在互斥锁下进行；无法准入的调用方
挂起在操作系统信号量上，由释放槽位者唤醒——无自旋、
无定时器。提高上限时恰好放行与新增额度
数量相等的挂起等待者。降低上限从不打断运行中的任务：
多余的许可只会随调用方归还而停止发放，
并发度会逐渐降到新上限。

`Cancel` 是关闭路径：唤醒所有挂起的等待者，并使
之后的所有准入尝试失败，这样正在销毁的 worker 池
能够干净地停止，而不是挂在一个无人释放的门上。将其视为
一次性操作——Cancel 之后门必须被 Dispose，调用方不得
与它并发归还槽位（Release / SetLimit）。

- int limit;
  - 可同时持有的槽位数。可由 SetLimit 随时修改。

- int active;
  - 已从 Wait 返回的调用方当前持有的槽位数。

- int waiters;
  - 等待槽位而挂起在 `sem` 上的调用方数。

- int canceled;
  - 由 Cancel 设置：门已失效，之后任何准入都不会成功。

- nint lockHandle;

- nint sem;

- string lockHandle;

- string sem;

- SemaphoreSlim()

- SemaphoreSlim(int limit)
  - 一次放行 `limit` 个调用方的节流器。

- void Wait()
  - 获取一个槽位，阻塞调用线程直到空位
    释放。`Cancel` 之后则立即返回，不占槽位。

- bool WaitFor(int timeoutMs)
  - 获取一个槽位，最多等待 `timeoutMs` 毫秒。
    获取到槽位返回 true（调用方必须 Release）；等待超时
    或门被取消返回 false。负超时表示无限等待。

- bool TryWait()
  - 仅当当前有可用槽位时才获取。
    获取到返回 true——调用方必须 Release。Cancel 之后恒为 false。

- void Release()
  - 归还一个槽位。若上限允许，唤醒一个等待者。
    `Cancel` 之后对准入为无操作（持有者仍可
    归还槽位，但不会唤醒任何人）。

- int SetLimit(int limit)
  - 随时调整可同时持有槽位的调用方数量。
    返回新上限放行了几名挂起的等待者。

- void Cancel()
  - 一次性关闭：唤醒所有挂起的等待者，并使所有
    后续 Wait / WaitFor / TryWait 立即失败。已持有槽位的
    任务保留其槽位（其 Release 对准入仍为无操作）。此后
    必须 `Dispose` 该门，调用方不得
    与其并发归还槽位。

- bool IsCanceled()
  - 在 `Cancel` 之后为 true：门不再放行任何人。

- int Limit()
  - 当前上限。

- int Active()
  - 当前持有的槽位数。

- int Waiting()
  - 等待槽位而挂起的调用方数。

- void Dispose()
  - 释放操作系统句柄。不允许有挂起的调用方。


## SharedTable (class)

由命名共享内存支持的固定容量、固定模式的哈希表。
每个操作在线程与进程间都是原子的。

- nint handle;

- string tableName;

- int tableCapacity;

- int tableKeySize;

- string tableSchema;

- string tableLabel;

- static List<string> openLabels=null;

- static List<nint> openHandles=null;

- [DllImport("crt", EntryPoint="zan_shared_table_create")]static extern nint NativeCreate(string name, int capacity, int keySize, string schema);

- [DllImport("crt", EntryPoint="zan_shared_table_open")]static extern nint NativeOpen(string name);

- [DllImport("crt", EntryPoint="zan_shared_table_create_anon")]static extern nint NativeCreateAnon(int capacity, int keySize, string schema);

- [DllImport("crt", EntryPoint="zan_shared_table_handle")]static extern long NativeOsHandle(nint handle);

- [DllImport("crt", EntryPoint="zan_shared_table_attach")]static extern nint NativeAttach(long osHandle);

- [DllImport("crt", EntryPoint="zan_shared_table_close")]static extern void NativeClose(nint handle);

- [DllImport("crt", EntryPoint="zan_shared_table_destroy")]static extern int NativeDestroy(nint handle);

- [DllImport("crt", EntryPoint="zan_shared_table_set_int")]static extern int NativeSetInt(nint handle, string key, string column, long newValue);

- [DllImport("crt", EntryPoint="zan_shared_table_get_int")]static extern long NativeGetInt(nint handle, string key, string column);

- [DllImport("crt", EntryPoint="zan_shared_table_set_float")]static extern int NativeSetFloat(nint handle, string key, string column, double newValue);

- [DllImport("crt", EntryPoint="zan_shared_table_get_float")]static extern double NativeGetFloat(nint handle, string key, string column);

- [DllImport("crt", EntryPoint="zan_shared_table_set_string")]static extern int NativeSetString(nint handle, string key, string column, string newValue);

- [DllImport("crt", EntryPoint="zan_shared_table_get_string")]static extern string NativeGetString(nint handle, string key, string column);

- [DllImport("crt", EntryPoint="zan_shared_table_increment")]static extern long NativeIncrement(nint handle, string key, string column, long delta);

- [DllImport("crt", EntryPoint="zan_shared_table_expire")]static extern int NativeExpire(nint handle, string key, long ttlMs);

- [DllImport("crt", EntryPoint="zan_shared_table_expire_at")]static extern int NativeExpireAt(nint handle, string key, long unixMs);

- [DllImport("crt", EntryPoint="zan_shared_table_expires_at")]static extern long NativeExpiresAt(nint handle, string key);

- [DllImport("crt", EntryPoint="zan_shared_table_purge_expired")]static extern long NativePurgeExpired(nint handle, long nowMs);

- [DllImport("crt", EntryPoint="zan_shared_table_rate_allow")]static extern int NativeRateAllow(nint handle, string key, long nowMs, long windowMs, long limit);

- [DllImport("crt", EntryPoint="zan_shared_table_lock_acquire")]static extern int NativeLockAcquire(nint handle, string key, long owner, long nowMs, long leaseMs);

- [DllImport("crt", EntryPoint="zan_shared_table_lock_release")]static extern int NativeLockRelease(nint handle, string key, long owner);

- [DllImport("crt", EntryPoint="zan_shared_table_delete")]static extern int NativeDelete(nint handle, string key);

- [DllImport("crt", EntryPoint="zan_shared_table_exists")]static extern int NativeExists(nint handle, string key);

- [DllImport("crt", EntryPoint="zan_shared_table_count")]static extern long NativeCount(nint handle);

- [DllImport("crt", EntryPoint="zan_shared_table_clear")]static extern void NativeClear(nint handle);

- [DllImport("crt", EntryPoint="zan_shared_table_hash")]static extern long NativeHash(string text);

- [DllImport("crt", EntryPoint="zan_shared_table_stat")]static extern long NativeStat(nint handle, int what);

- [DllImport("crt", EntryPoint="zan_shared_table_set_int_at")]static extern int NativeSetIntAt(nint handle, long keyHash, string column, long newValue);

- [DllImport("crt", EntryPoint="zan_shared_table_get_int_at")]static extern long NativeGetIntAt(nint handle, long keyHash, string column);

- [DllImport("crt", EntryPoint="zan_shared_table_increment_at")]static extern long NativeIncrementAt(nint handle, long keyHash, string column, long delta);

- [DllImport("crt", EntryPoint="zan_shared_table_extreme_at")]static extern long NativeExtremeAt(nint handle, long keyHash, string column, long newValue, long keepLarger);

- [DllImport("crt", EntryPoint="zan_shared_table_set_string_at")]static extern int NativeSetStringAt(nint handle, long keyHash, string column, string newValue);

- [DllImport("crt", EntryPoint="zan_shared_table_get_string_at")]static extern string NativeGetStringAt(nint handle, long keyHash, string column);

- [DllImport("crt", EntryPoint="zan_shared_table_match_at")]static extern int NativeMatchAt(nint handle, long keyHash, string column, string text);

- [DllImport("crt", EntryPoint="zan_shared_table_exists_at")]static extern int NativeExistsAt(nint handle, long keyHash);

- [DllImport("crt", EntryPoint="zan_shared_table_delete_at")]static extern int NativeDeleteAt(nint handle, long keyHash);

- SharedTable(string name, int capacity)

- void Label(string text)
  - 给这张表一个便于识别的名字，用于状态输出。匿名表没有
    名字，不设置就显示为 "(anonymous)"。

- static void Track(SharedTable table)

- static void Untrack(nint handle)

- ~SharedTable()

- void KeySize(int size)

- void ColumnInt(string name)

- void ColumnFloat(string name)

- void ColumnString(string name, int size)

- bool Create()

- static SharedTable Open(string name)

- bool CreateAnonymous()
  - 建一张匿名表：不占用任何全局命名空间，机器上的其它程序
    既打不开也读不到，也不会在文件系统里留下东西——最后一个引用消失
    （包括进程被强杀）内核就回收。子进程要用它，只能由建表的进程把
    OsHandle() 传下去（子进程继承该句柄/描述符），再用 Attach() 映射
    同一块内存，就像 fork 出来的那样。
    
    因此必须在派生子进程之前建好：之后才建的表，已有的子进程拿不到。

- long OsHandle()
  - 匿名表要交给子进程的句柄（POSIX 是文件描述符，Windows 是
    可继承 HANDLE）。按名字建的表返回 0：它本来就按名字打开。

- static SharedTable Attach(long osHandle)
  - 子进程侧：映射父进程建好并继承下来的匿名表。表的结构写在
    内存自身的头部，所以不用重复声明列；句柄不是一张本版本的表时返回
    未打开的表。

- bool IsOpen()

- void Close()

- bool Destroy()

- bool SetInt(string key, string column, long newValue)

- long GetInt(string key, string column)

- bool SetFloat(string key, string column, double newValue)

- double GetFloat(string key, string column)

- bool SetString(string key, string column, string newValue)

- string GetString(string key, string column)

- long Increment(string key, string column, long delta)

- long Decrement(string key, string column, long delta)

- bool Expire(string key, long ttlMs)

- bool ExpireAt(string key, long unixMs)

- bool Persist(string key)

- long ExpiresAt(string key)

- long PurgeExpired(long nowMs)

- bool RateAllow(string key, long nowMs, long windowMs, long limit)

- bool TryAcquireLease(string key, long owner, long nowMs, long leaseMs)

- bool ReleaseLease(string key, long owner)

- bool Delete(string key)

- bool Exists(string key)

- int Count()

- void Clear()

- static int STAT_RESERVED=0;

- static int STAT_RESIDENT=1;

- static int STAT_CAPACITY=2;

- static int STAT_COUNT=3;

- static int STAT_ROW_STRIDE=4;

- static int STAT_KEY_SIZE=5;

- static int STAT_COLUMNS=6;

- long ReservedBytes()
  - 整块映射的大小（预留的虚拟地址空间，不是内存占用）。

- long ResidentBytes()
  - 本进程真正驻留在物理内存中的部分；平台不支持时为 -1。

- int Capacity()
  - 表能容纳的行数（向上取整到 2 的幂）。

- int RowStride()
  - 每行占用的字节数（键 + 各列 + 行头）。

- int KeyBytes()
  - 键的最大字节数。

- int ColumnCount()

- static int OpenCount()
  - 本进程当前映射着的共享表数量。

- static string OpenLabel(int index)

- static long OpenStat(int index, int what)
  - 第 `index` 张表的某项统计，取值见 STAT_* 常量。

- static string StatPad(string s, int width)

- static string StatBytes(long bytes)

- static string StatusText()
  - 本进程所有共享表的一张文本表格：行数、每行大小、预留的
    映射大小，以及真正驻留在内存中的部分。

- static long Hash(string text)
  - 用与表相同的方式哈希键（FNV-1a，64 位）。

- bool SetIntAt(long keyHash, string column, long newValue)

- long GetIntAt(long keyHash, string column)

- long IncrementAt(long keyHash, string column, long delta)

- long MinAt(long keyHash, string column, long newValue)
  - 若 `newValue` 小于当前值则存入（0 视为未设置），
    一步原子完成。用于记录“目前最快响应”。

- long MaxAt(long keyHash, string column, long newValue)
  - 若 `newValue` 大于当前值则存入，
    一步原子完成。用于记录“目前最慢响应”。

- bool SetStringAt(long keyHash, string column, string newValue)

- string GetStringAt(long keyHash, string column)

- bool MatchAt(long keyHash, string column, string text)
  - 判断某字符串列是否等于 `text`，不把
    存储的字节复制出来。否则每次查找校验键的原始文本都会
    为每次调用分配一个字符串。

- bool ExistsAt(long keyHash)

- bool DeleteAt(long keyHash)


## Stopwatch (class)

跨平台高分辨率秒表。

- [DllImport("kernel32", EntryPoint="GetTickCount64")]static extern long WinGetTickCount64();

- [DllImport("crt", EntryPoint="clock_gettime")]static extern int ClockGettime(int clk_id, string tp);

- [DllImport("crt", EntryPoint="calloc")]static extern string StopwatchCalloc(long count, long size);

- [DllImport("crt", EntryPoint="free")]static extern void StopwatchFree(string ptr);

- static int MonotonicClockId()

- static int MonotonicClockId()

- [DllImport("crt", EntryPoint="zan_monotonic_us")]static extern long NativeMonotonicUs();

- static long GetMicroseconds()
  - 单调微秒。与 GetMilliseconds 不同，它能分辨
    单个请求（Windows 上 GetTickCount64 步长约 15ms），而且
    不分配内存，因此可用于请求处理路径。

- static long GetMilliseconds()
  - 返回当前毫秒级的 tick 计数。


## Thread (class)

跨平台线程与同步原语。
Windows 上使用 Win32 API，POSIX（Linux/macOS）上使用 libc 中的 pthreads。

- [DllImport("kernel32", EntryPoint="Sleep")]static extern void PlatSleep(int milliseconds);

- [DllImport("crt", EntryPoint="usleep")]static extern int PlatUsleep(int microseconds);

- [DllImport("crt", EntryPoint="zan_thread_start")]static extern int PlatThreadStart(ThreadStart body);

- [DllImport("crt", EntryPoint="zan_thread_current_id")]static extern long PlatCurrentThreadId();

- static bool Start(ThreadStart body)
  - 在新的分离后台线程上运行 <paramref name="body"/> 并
    立即返回。委托必须是非捕获的（只操作
    静态/共享状态）；用原子操作或 App.Post 把结果
    安全地交回另一线程。线程成功启动返回 true。

- static void Sleep(int milliseconds)
  - 让当前线程休眠指定的毫秒数。

- static long CurrentId()
  - 获取当前线程的 ID——同一进程内所有线程互不相同
    （Windows 的 GetCurrentThreadId、Linux 的 TID、
    macOS 的 pthread_threadid_np，由运行时 shim
    zan_thread_current_id 统一提供）。此前 POSIX 分支
    误用 getpid()，让每个线程都拿到进程 ID，
    导致 Gui.Reactive.Dispatcher 把后台线程误判为
    UI 线程而直接执行 GUI 操作。

- static void SleepAsync(int milliseconds)
  - 异步休眠（Sleep 的包装）。


## Timer (class)

简单的周期定时器。`Start` 将定时器注册到
共享的定时器线程，该线程约每 10 毫秒唤醒一次，触发每个到期的
回调；`Stop` 注销定时器。慢回调会延迟共享线程上的
所有其他定时器，因此回调应保持简短，或把工作
转交给另一个线程。

Timer t = new Timer(1000, () => { counter = counter + 1; });
t.Start();
...
t.Stop();

- int intervalMs;

- ThreadStart callback;

- bool running;

- int tickCount;

- long nextDue;

- Timer(int intervalMs, ThreadStart callback)
  - 创建一个每隔 `intervalMs` 毫秒触发一次 `callback` 的定时器。
    在调用 `Start` 之前不会启动。

- bool Start()
  - 启动定时器。已在运行时为无操作。
    定时器处于运行状态时返回 true。

- void Stop()
  - 停止定时器并注销它。正在进行的 tick（如有）
    会执行完；后续 tick 不再触发。未运行时为无操作。

- int TickCount()
  - 自 Start（或上次 Reset）以来已完成的 tick 数。

- void Reset()
  - 将 tick 计数器重置为零。

- bool IsRunning()
  - 定时器运行期间（Start 与 Stop 之间）为 true。

- static void Pump()

- static long Now()

- static List<Timer> timers=new List<Timer>();

- static bool pumpRunning=false;

- static nint lockHandle;

- static string lockHandle;

- static Timer()


## void (delegate)

无参数的线程入口点。可赋值 lambda 或静态方法
组：<c>Thread.Start(() => { ... })</c> 或 <c>Thread.Start(Worker)</c>。

`delegate void ThreadStart();`
