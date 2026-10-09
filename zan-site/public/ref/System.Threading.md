# System.Threading

> 源码: `stdlib/System/Threading/AsyncGate.zan`, `stdlib/System/Threading/AsyncRuntime.zan`, `stdlib/System/Threading/AsyncRwLock.zan`, `stdlib/System/Threading/BlockingQueue.zan`, `stdlib/System/Threading/CancellationToken.zan`, `stdlib/System/Threading/Gate.zan`, `stdlib/System/Threading/SemaphoreSlim.zan`, `stdlib/System/Threading/TaskCompletionSource.zan`, `stdlib/System/Threading/Threading.zan`, `stdlib/System/Threading/Timer.zan`


## AsyncGate (class)

- long handle;

- AsyncGate()

- bool IsOpen()

- async bool Wait()

- void Signal()

- void Close()


## AsyncRuntime (class)

- [DllImport("crt", EntryPoint="zan_async_set_workers")]static extern void PlatSetWorkers(int workers);

- [DllImport("crt", EntryPoint="zan_async_set_io_shards")]static extern void PlatSetIoShards(int shards);

- [DllImport("crt", EntryPoint="zan_async_set_sync_fast")]static extern void PlatSetSyncFast(int on);

- [DllImport("crt", EntryPoint="zan_async_cfg_workers")]static extern int PlatWorkers();

- [DllImport("crt", EntryPoint="zan_async_cfg_io_shards")]static extern int PlatIoShards();

- [DllImport("crt", EntryPoint="zan_async_cfg_sync_fast")]static extern int PlatSyncFast();

- static void SetWorkers(int workers)

- static void SetIoShards(int shards)

- static void SetSyncFast(bool on)

- static int Workers()

- static int IoShards()

- static int SyncFast()


## AsyncRwLock (class)

- int readers;

- bool writer;

- int writersWaiting;

- AsyncGate readGate;

- AsyncGate writeGate;

- AsyncRwLock()

- async void EnterRead()

- void ExitRead()

- async void EnterWrite()

- void ExitWrite()

- int ReaderCount()

- bool IsWriteLocked()

- void Close()


## AtomicInt (class)

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

- List<T> items;

- int head;

- bool closed;

- int waiters;

- int capacity;

- int posters;

- T empty;

- string fault;

- bool canceled;

- nint lockHandle;

- nint sem;

- nint slots;

- BlockingQueue()

- BlockingQueue(T empty, int capacity)

- bool Enqueue(T item)

- T TryDequeue()

- T DequeueFor(int timeoutMs)

- T Dequeue()

- void Complete()

- void CompleteWithFault(string message)

- void Cancel()

- bool IsCanceled()

- string GetFault()

- bool IsCompleted()

- int Count()

- void Dispose()

- ~BlockingQueue()

- T TakeLocked()


## CancellationToken (class)

- bool isCancellationRequested;

- Gate waitGate;

- nint syncLock;

- CancellationToken()

- bool IsCancellationRequested { get }

- void ThrowIfCancellationRequested()

- async bool WaitHandle()

- void CancelInternal()

- void Close()

- ~CancellationToken()


## CancellationTokenSource (class)

- CancellationToken token;

- bool isDisposed;

- CancellationTokenSource()

- static CancellationTokenSource CreateAfter(int delayMs)

- static async void CancelDelayed(CancellationTokenSource cts, int delayMs)

- CancellationToken Token { get }

- void Cancel()

- void CancelAfter(int delayMs)

- void Dispose()


## Channel (class)

- List<string> buffer;

- int capacity;

- bool closed;

- int sendWaiters;

- int recvWaiters;

- Gate sendGate;

- Gate recvGate;

- nint lockHandle;

- Channel(int capacity)

- static Channel CreateUnbuffered()

- async void Send(string item)

- async string Receive()

- void Close()

- bool IsClosed()

- int Count()

- ~Channel()


## Gate (class)

- long handle;

- static Dict <long, byte> live=new Dict <long, byte>();

- static nint liveLock=Mutex.Create();

- Gate()

- bool IsValid()

- bool IsOpen()

- async bool Wait()

- async bool Wait(int timeoutMs)

- static async void Watchdog(long handle, int timeoutMs)

- static void LiveAdd(long handle)

- void Signal()

- void Close()

- ~Gate()

- long Handle()

- [DllImport("crt", EntryPoint="zan_gate_new")]static extern long zan_gate_new();

- [DllImport("crt", EntryPoint="zan_gate_signal")]static extern void zan_gate_signal(long h);

- [DllImport("crt", EntryPoint="zan_gate_free")]static extern void zan_gate_free(long h);


## Mutex (class)

- [DllImport("crt", EntryPoint="calloc")]static extern nint WinCalloc(long count, long size);

- [DllImport("crt", EntryPoint="free")]static extern void WinFree(nint ptr);

- [DllImport("kernel32", EntryPoint="InitializeSRWLock")]static extern void WinInitializeSRWLock(nint srwLock);

- [DllImport("kernel32", EntryPoint="AcquireSRWLockExclusive")]static extern void WinAcquireSRWLockExclusive(nint srwLock);

- [DllImport("kernel32", EntryPoint="ReleaseSRWLockExclusive")]static extern void WinReleaseSRWLockExclusive(nint srwLock);

- [DllImport("crt", EntryPoint="calloc")]static extern nint PthreadCalloc(long count, long size);

- [DllImport("crt", EntryPoint="free")]static extern void PthreadFree(nint ptr);

- [DllImport("crt", EntryPoint="pthread_mutex_init")]static extern int PthreadMutexInit(nint mutex, nint attr);

- [DllImport("crt", EntryPoint="pthread_mutex_lock")]static extern int PthreadMutexLock(nint mutex);

- [DllImport("crt", EntryPoint="pthread_mutex_unlock")]static extern int PthreadMutexUnlock(nint mutex);

- [DllImport("crt", EntryPoint="pthread_mutex_destroy")]static extern int PthreadMutexDestroy(nint mutex);

- static nint Create()

- static void Lock(nint handle)

- static void Unlock(nint handle)

- static void Destroy(nint handle)

- static nint Create()

- static void Lock(nint handle)

- static void Unlock(nint handle)

- static void Destroy(nint handle)


## Semaphore (class)

- [DllImport("kernel32", EntryPoint="CreateSemaphoreA")]static extern nint WinCreateSemaphore(nint secAttrs, int initialCount, int maxCount, nint name);

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

- [DllImport("crt", EntryPoint="__error")]static extern nint SemErrnoLoc();

- [DllImport("crt", EntryPoint="__errno_location")]static extern nint SemErrnoLoc();

- static int SemErrno()

- static int SemEintr()

- static nint Create(int initialCount, int maxCount)

- static void Wait(nint handle)

- static bool WaitFor(nint handle, int timeoutMs)

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

- int limit;

- int active;

- int waiters;

- int canceled;

- nint lockHandle;

- nint sem;

- SemaphoreSlim()

- SemaphoreSlim(int limit)

- void Wait()

- bool WaitFor(int timeoutMs)

- bool TryWait()

- void Release()

- int SetLimit(int limit)

- void Cancel()

- bool IsCanceled()

- int Limit()

- int Active()

- int Waiting()

- void Dispose()

- ~SemaphoreSlim()


## SharedTable (class)

- nint handle;

- string tableName;

- int tableCapacity;

- int tableKeySize;

- string tableSchema;

- string tableLabel;

- static List<SharedTableEntry> openEntries=null;

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

- long OsHandle()

- static SharedTable Attach(long osHandle)

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

- long ResidentBytes()

- int Capacity()

- int RowStride()

- int KeyBytes()

- int ColumnCount()

- static int OpenCount()

- static string OpenLabel(int index)

- static long OpenStat(int index, int what)

- static string StatPad(string s, int width)

- static string StatBytes(long bytes)

- static string StatusText()

- static long Hash(string text)

- bool SetIntAt(long keyHash, string column, long newValue)

- long GetIntAt(long keyHash, string column)

- long IncrementAt(long keyHash, string column, long delta)

- long MinAt(long keyHash, string column, long newValue)

- long MaxAt(long keyHash, string column, long newValue)

- bool SetStringAt(long keyHash, string column, string newValue)

- string GetStringAt(long keyHash, string column)

- bool MatchAt(long keyHash, string column, string text)

- bool ExistsAt(long keyHash)

- bool DeleteAt(long keyHash)


## SharedTableEntry (class)

- public nint handle;

- public string label;

- public SharedTableEntry(nint handle, string label)


## TaskCompletionSource (class)

- T result;

- Exception error;

- bool isCompleted;

- Gate gate;

- nint syncLock;

- TaskCompletionSource()

- bool IsCompleted { get }

- async Task<T> Task { get }

- bool TrySetResult(T value)

- void SetResult(T value)

- bool TrySetException(Exception exc)

- void SetException(Exception exc)

- bool TrySetCanceled()

- void SetCanceled()

- void Close()

- ~TaskCompletionSource()


## Thread (class)

- [DllImport("kernel32", EntryPoint="Sleep")]static extern void PlatSleep(int milliseconds);

- [DllImport("crt", EntryPoint="usleep")]static extern int PlatUsleep(int microseconds);

- [DllImport("crt", EntryPoint="zan_thread_start")]static extern int PlatThreadStart(ThreadStart body);

- [DllImport("crt", EntryPoint="zan_thread_current_id")]static extern long PlatCurrentThreadId();

- static bool Start(ThreadStart body)

- static void Sleep(int milliseconds)

- static long CurrentId()

- static async Task SleepAsync(int milliseconds)


## Timer (class)

- int intervalMs;

- ThreadStart callback;

- bool running;

- int tickCount;

- int pendingTicks;

- long nextDue;

- Timer(int intervalMs, ThreadStart callback)

- bool Start()

- void Stop()

- async void StopAsync()

- private bool IsDrained()

- int TickCount()

- void Reset()

- bool IsRunning()

- private static bool PumpBatch()

- static void Pump()

- static long Now()

- static List<Timer> timers=new List<Timer>();

- static bool pumpRunning=false;

- static nint lockHandle;

- static nint lockHandle;

- static Timer()


## void (delegate)

`delegate void ThreadStart();`
