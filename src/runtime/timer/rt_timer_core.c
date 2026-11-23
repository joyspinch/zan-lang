/* rt_timer_core.c: 核心定时器最小堆、超时调度、微秒单调时钟与纪元时间支持 */

typedef enum zan_timer_kind {
    ZAN_TIMER_DELAY = 0,
    ZAN_TIMER_PUBLIC = 1
} zan_timer_kind;

typedef struct zan_timer_entry {
    long long due_us;
    long long id;
    long long interval;
    long long exec_msec;
    long long exec_count;
    long long round;
    unsigned long long sequence;
    zan_timer_kind kind;
    int removed;
    int waking;
    int dead;  /* 已取消、仍留在堆里待惰性回收（计入 g_heap_dead） */
    struct zan_timer_entry *dnext;  /* 同一帧的下一条 delay 条目（帧索引链） */
    zan_timer_callback_t callback;
    zan_timer_ctx_callback_t callback_ctx;
    void *frame;
    zan_timer_step_t step;
} zan_timer_entry;

static zan_timer_entry **g_heap;
static size_t g_heap_len;
static size_t g_heap_cap;
static long long g_next_id = 1;
static long long g_round;
static unsigned long long g_sequence;
static int g_initialized;
static void (*g_ready_hook)(void *frame, zan_timer_step_t step);
static zan_timer_entry *g_dispatching;
static unsigned long g_dispatch_tid;
static volatile long long g_live;
/* 帧 → 未决 delay 条目索引（线性探测开放寻址，值为同帧条目链表头）。
 * 编译器在**每个**异步帧完成/清理时调 zan_timer_cancel_delay；原实现持全局
 * 定时器锁全堆扫描 + 重建堆，O(堆大小)。服务端在连接池排队时每次门等都挂一个
 * 250 ms 看门狗，堆里常驻上千条，于是每次帧完成都在锁里扫上千条——多 worker
 * 在这把锁上排队，32 连接压测吞吐反比 8 连接低一半。索引让取消 O(1)；取消
 * 的条目惰性回收（到堆顶时丢弃，死条目过半再整体压缩，摊还 O(1)）。 */
typedef struct zan_delay_slot {
    void *frame;
    zan_timer_entry *head;
} zan_delay_slot;
static zan_delay_slot *g_dix;
static size_t g_dix_cap;
static size_t g_dix_len;
static size_t g_heap_dead;
/* 派发互斥：zan_timer_dispatch_due 不是可重入的（g_dispatching /
 * g_dispatch_tid 是普通静态量），两个线程同时派发会把状态搅乱。
 * worker 池里靠 rt_io.c 的 g_timer_owner 保护，但同步上下文里的
 * 定时等待（A470 的 zan_co_sync_sleep）也在派发——它可能跑在**另一个**
 * 线程上。所以保护下沉到这里，让所有派发路径都受约束。 */
static volatile int g_dispatch_busy;
/* 持锁线程的精确标记：g_dispatch_tid 只在派发 delay 定时器时更新，派发
 * public 定时器时是旧值——拿它判"是不是我自己持有的"会漏判，导致等自己
 * 持有的互斥而死锁。用线程局部变量精确记录。 */
static __thread int g_dispatch_owned;

#if defined(ZAN_BARE_METAL)
__attribute__((weak))
#endif
long long zan_timer_now_ms(void) {
#if defined(_WIN32)
    return (long long)GetTickCount64();
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#else
    return 0;
#endif
}

long long zan_co_quantum_ms(void) {
    static long long q = -1;
    if (q >= 0) return q;
    long long v = 2;
    const char *e = getenv("ZAN_CO_QUANTUM_MS");
    if (e && *e) v = strtoll(e, NULL, 10);
    if (v < 0) v = 0;
    q = v;
    return q;
}

long long zan_co_precise_us(void) {
#if defined(_WIN32)
    static long long freq;
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    if (!freq) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        freq = f.QuadPart;
    }
    return (c.QuadPart / freq) * 1000000 +
           (c.QuadPart % freq) * 1000000 / freq;
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
#else
    return zan_timer_now_ms() * 1000;
#endif
}

static int timer_less(const zan_timer_entry *a, const zan_timer_entry *b) {
    return a->due_us < b->due_us ||
           (a->due_us == b->due_us && a->sequence < b->sequence);
}

static void heap_swap(size_t a, size_t b) {
    zan_timer_entry *entry = g_heap[a];
    g_heap[a] = g_heap[b];
    g_heap[b] = entry;
}

static int heap_push(zan_timer_entry *entry) {
    if (g_heap_len == g_heap_cap) {
        size_t cap = g_heap_cap ? g_heap_cap * 2 : 64;
        zan_timer_entry **heap = (zan_timer_entry **)realloc(g_heap, cap * sizeof(*heap));
        if (!heap) {
            entry->removed = 1;
            return -1;
        }
        g_heap = heap;
        g_heap_cap = cap;
    }
    if (!entry->removed) __atomic_fetch_add(&g_live, 1, __ATOMIC_RELAXED);
    size_t index = g_heap_len++;
    g_heap[index] = entry;
    while (index > 0) {
        size_t parent = (index - 1) / 2;
        if (!timer_less(entry, g_heap[parent])) break;
        heap_swap(index, parent);
        index = parent;
    }
    return 0;
}

static zan_timer_entry *heap_pop(void) {
    if (g_heap_len == 0) return NULL;
    zan_timer_entry *root = g_heap[0];
    if (!root->removed) __atomic_fetch_sub(&g_live, 1, __ATOMIC_RELAXED);
    zan_timer_entry *last = g_heap[--g_heap_len];
    if (g_heap_len == 0) return root;
    g_heap[0] = last;
    size_t index = 0;
    for (;;) {
        size_t left = index * 2 + 1;
        if (left >= g_heap_len) break;
        size_t right = left + 1;
        size_t smallest = right < g_heap_len && timer_less(g_heap[right], g_heap[left])
            ? right : left;
        if (!timer_less(g_heap[smallest], g_heap[index])) break;
        heap_swap(index, smallest);
        index = smallest;
    }
    return root;
}

static size_t dix_hash(const void *frame) {
    unsigned long long x = (unsigned long long)(uintptr_t)frame;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    return (size_t)x;
}

/* 帧所在槽位；不存在时返回 g_dix_cap。调用方持 timer_lock。 */
static size_t dix_find(const void *frame) {
    if (g_dix_cap == 0) return 0;
    size_t mask = g_dix_cap - 1;
    for (size_t i = dix_hash(frame) & mask;; i = (i + 1) & mask) {
        if (!g_dix[i].frame) return g_dix_cap;
        if (g_dix[i].frame == frame) return i;
    }
}

static void dix_place(void *frame, zan_timer_entry *head) {
    size_t mask = g_dix_cap - 1;
    size_t i = dix_hash(frame) & mask;
    while (g_dix[i].frame) i = (i + 1) & mask;
    g_dix[i].frame = frame;
    g_dix[i].head = head;
}

/* 保证还能再插一个帧（装载率 ≤ 1/2）。失败返回 -1，调用方按 OOM 口径处理。 */
static int dix_reserve(void) {
    if ((g_dix_len + 1) * 2 <= g_dix_cap) return 0;
    size_t cap = g_dix_cap ? g_dix_cap * 2 : 64;
    zan_delay_slot *old = g_dix;
    size_t old_cap = g_dix_cap;
    zan_delay_slot *slots = (zan_delay_slot *)calloc(cap, sizeof(*slots));
    if (!slots) return -1;
    g_dix = slots;
    g_dix_cap = cap;
    for (size_t i = 0; i < old_cap; i++)
        if (old[i].frame) dix_place(old[i].frame, old[i].head);
    free(old);
    return 0;
}

/* 删除槽位 i，并把后续探测链回填（backward-shift），不留墓碑。 */
static void dix_erase_slot(size_t i) {
    size_t mask = g_dix_cap - 1;
    size_t j = i;
    for (;;) {
        g_dix[i].frame = NULL;
        g_dix[i].head = NULL;
        for (;;) {
            j = (j + 1) & mask;
            if (!g_dix[j].frame) { g_dix_len--; return; }
            size_t home = dix_hash(g_dix[j].frame) & mask;
            /* j 的理想位置不在 (i, j] 环形区间内时，可以挪到 i。 */
            if (i <= j ? (home <= i || home > j) : (home <= i && home > j)) break;
        }
        g_dix[i] = g_dix[j];
        i = j;
    }
}

/* 已 dix_reserve 过，不会失败。 */
static void dix_link(zan_timer_entry *entry) {
    size_t i = dix_find(entry->frame);
    if (i < g_dix_cap) {
        entry->dnext = g_dix[i].head;
        g_dix[i].head = entry;
        return;
    }
    entry->dnext = NULL;
    dix_place(entry->frame, entry);
    g_dix_len++;
}

static void dix_unlink(zan_timer_entry *entry) {
    size_t i = dix_find(entry->frame);
    if (i >= g_dix_cap) return;
    zan_timer_entry **pp = &g_dix[i].head;
    while (*pp && *pp != entry) pp = &(*pp)->dnext;
    if (*pp) *pp = entry->dnext;
    entry->dnext = NULL;
    if (!g_dix[i].head) dix_erase_slot(i);
}

/* 丢弃堆顶的已取消条目。 */
static void heap_drop_removed_top(void) {
    while (g_heap_len > 0 && g_heap[0]->removed) {
        zan_timer_entry *entry = heap_pop();
        if (entry->dead && g_heap_dead > 0) g_heap_dead--;
        free(entry);
    }
}

/* 死条目过半时整体压缩并重建堆：摊还到每次取消仍是 O(1)，且长延时被取消
 * 后不会让堆无限增长。 */
static void heap_compact_dead(void) {
    if (g_heap_dead < 64 || g_heap_dead * 2 < g_heap_len) return;
    size_t w = 0;
    for (size_t i = 0; i < g_heap_len; i++) {
        if (g_heap[i]->dead) free(g_heap[i]);
        else g_heap[w++] = g_heap[i];
    }
    g_heap_len = w;
    g_heap_dead = 0;
    for (size_t i = g_heap_len / 2; i-- > 0;) {
        size_t index = i;
        for (;;) {
            size_t left = index * 2 + 1;
            if (left >= g_heap_len) break;
            size_t right = left + 1;
            size_t smallest = right < g_heap_len &&
                              timer_less(g_heap[right], g_heap[left])
                ? right : left;
            if (!timer_less(g_heap[smallest], g_heap[index])) break;
            heap_swap(index, smallest);
            index = smallest;
        }
    }
}

void zan_timer_set_ready_hook(void (*ready)(void *frame, zan_timer_step_t step)) {
    timer_lock();
    g_ready_hook = ready;
    timer_unlock();
}

void zan_timer_runtime_reset(void) {
    timer_lock();
    for (size_t i = 0; i < g_heap_len; i++) free(g_heap[i]);
    g_heap_len = 0;
    g_heap_dead = 0;
    if (g_dix) memset(g_dix, 0, g_dix_cap * sizeof(*g_dix));
    g_dix_len = 0;
    __atomic_store_n(&g_live, 0, __ATOMIC_RELAXED);
    g_next_id = 1;
    g_round = 0;
    g_sequence = 0;
    g_initialized = 1;
    timer_unlock();
}

long long zan_timer_saturating_due(long long now_ms, long long delay_ms) {
    if (delay_ms < 0) return now_ms;
    if (delay_ms > LLONG_MAX - now_ms) return LLONG_MAX;
    return now_ms + delay_ms;
}

long long zan_timer_saturating_due_us(long long now_us, long long delay_us) {
    if (delay_us < 0) return now_us;
    if (delay_us > LLONG_MAX - now_us) return LLONG_MAX;
    return now_us + delay_us;
}

static long long delay_ms_to_us(long long ms) {
    if (ms < 0) return 0;
    if (ms > LLONG_MAX / 1000) return LLONG_MAX;
    return ms * 1000;
}

void zan_timer_delay(long long ms, void *frame, zan_timer_step_t step) {
    if (!step) return;
    zan_timer_entry *entry = (zan_timer_entry *)ZAN_OOM_CALLOC("timer.delay_entry", 1, sizeof(*entry));
    if (!entry) { step(frame); return; } /* B-ID98：口径同 heap_push 失败路径，立即恢复等待协程不悬挂 */
    entry->due_us = zan_timer_saturating_due_us(zan_co_precise_us(),
                                                               delay_ms_to_us(ms));
    entry->kind = ZAN_TIMER_DELAY;
    entry->frame = frame;
    entry->step = step;
    timer_lock();
    g_initialized = 1;
    entry->sequence = ++g_sequence;
    if (dix_reserve() != 0 || heap_push(entry) != 0) {
        timer_unlock();
        free(entry);
        step(frame);
        return;
    }
    dix_link(entry);
    timer_unlock();
}

int zan_timer_cancel_delay(void *frame) {
    int found = 0;
    if (!frame) return 0;
    timer_lock();
    size_t slot = dix_find(frame);
    if (slot < g_dix_cap) {
        zan_timer_entry *entry = g_dix[slot].head;
        while (entry) {
            zan_timer_entry *next = entry->dnext;
            entry->dnext = NULL;
            entry->removed = 1;
            entry->dead = 1;
            g_heap_dead++;
            __atomic_fetch_sub(&g_live, 1, __ATOMIC_RELAXED);
            found++;
            entry = next;
        }
        dix_erase_slot(slot);
    }
    if (g_dispatching && g_dispatching->kind == ZAN_TIMER_DELAY &&
        !g_dispatching->removed && g_dispatching->frame == frame) {
        g_dispatching->removed = 1;
        found++;
    }
    while (g_dispatching && g_dispatching->kind == ZAN_TIMER_DELAY &&
           g_dispatching->waking && g_dispatching->frame == frame &&
           g_dispatch_tid != timer_self_tid()) {
        timer_unlock();
        timer_yield();
        timer_lock();
    }
    heap_compact_dead();
    timer_unlock();
    return found;
}

static long long timer_add(long long ms, zan_timer_callback_t callback, int repeat) {
    if (ms < 1 || !callback) return 0;
    zan_timer_entry *entry = (zan_timer_entry *)calloc(1, sizeof(*entry));
    if (!entry) return 0;
    entry->callback = callback;
    timer_lock();
    g_initialized = 1;
    entry->id = g_next_id++;
    if (entry->id <= 0) entry->id = g_next_id = 1;
    entry->interval = repeat ? ms : 0;
    entry->due_us = zan_timer_saturating_due_us(zan_co_precise_us(),
                                                               delay_ms_to_us(ms));
    entry->sequence = ++g_sequence;
    entry->kind = ZAN_TIMER_PUBLIC;
    if (heap_push(entry) != 0) {
        timer_unlock();
        free(entry);
        return 0;
    }
    timer_unlock();
    return entry->id;
}

long long zan_timer_tick(long long interval, zan_timer_callback_t callback) {
    return timer_add(interval, callback, 1);
}

long long zan_timer_after(long long delay, zan_timer_callback_t callback) {
    return timer_add(delay, callback, 0);
}

long long zan_timer_after_ctx(long long delay, zan_timer_ctx_callback_t callback, void *arg) {
    if (delay < 1 || !callback) return 0;
    zan_timer_entry *entry = (zan_timer_entry *)calloc(1, sizeof(*entry));
    if (!entry) return 0;
    entry->callback_ctx = callback;
    entry->frame = arg;
    entry->kind = ZAN_TIMER_PUBLIC;
    timer_lock();
    g_initialized = 1;
    entry->id = g_next_id++;
    if (entry->id <= 0) entry->id = g_next_id = 1;
    entry->interval = 0;
    entry->due_us = zan_timer_saturating_due_us(zan_co_precise_us(), delay_ms_to_us(delay));
    entry->sequence = ++g_sequence;
    if (heap_push(entry) != 0) {
        timer_unlock();
        free(entry);
        return 0;
    }
    timer_unlock();
    return entry->id;
}

long long zan_timer_next_timeout(void) {
    long long timeout = -1;
    timer_lock();
    heap_drop_removed_top();
    if (g_heap_len > 0) {
        long long rem_us = g_heap[0]->due_us - zan_co_precise_us();
        timeout = (rem_us <= 0) ? 0 : (rem_us + 999) / 1000;
    }
    timer_unlock();
    return timeout;
}

/* 派发主循环。调用方必须已持有 g_dispatch_busy（CAS 拿到），
 * 返回时互斥由本函数释放。 */
static long long timer_dispatch_due_locked(void) {
    size_t dispatched = 0;
    for (;;) {
        long long now = zan_co_precise_us();
        timer_lock();
        heap_drop_removed_top();
        if (g_heap_len == 0 || g_heap[0]->due_us > now) {
            timer_unlock();
            __atomic_store_n(&g_dispatch_busy, 0, __ATOMIC_RELEASE);
            g_dispatch_owned = 0;
            return dispatched;
        }
        zan_timer_entry *entry = heap_pop();
        if (entry->kind == ZAN_TIMER_DELAY) dix_unlink(entry);
        if (entry->kind == ZAN_TIMER_PUBLIC) {
            entry->exec_count++;
            entry->round = ++g_round;
        }
        zan_timer_entry *prev = g_dispatching;
        g_dispatching = entry;
        timer_unlock();

        long long started_us = zan_co_precise_us();
        if (entry->kind == ZAN_TIMER_DELAY) {
            void (*ready)(void *, zan_timer_step_t);
            timer_lock();
            ready = g_ready_hook;
            int cancelled = entry->removed;
            if (!cancelled) {
                entry->waking = 1;
                g_dispatch_tid = timer_self_tid();
            }
            timer_unlock();
            if (!cancelled) {
                if (ready) ready(entry->frame, entry->step);
                else entry->step(entry->frame);
            }
        } else if (entry->callback_ctx) {
            entry->callback_ctx(entry->frame);
        } else if (entry->callback) {
            entry->callback();
        }
        long long elapsed = zan_co_precise_us() - started_us;
        dispatched++;

        timer_lock();
        entry->waking = 0;
        g_dispatching = prev;
        if (entry->kind == ZAN_TIMER_PUBLIC) entry->exec_msec = elapsed / 1000;
        if (entry->kind == ZAN_TIMER_PUBLIC && entry->interval > 0 && !entry->removed) {
            long long cur_now = zan_co_precise_us();
            long long interval_us = delay_ms_to_us(entry->interval);
            entry->due_us = zan_timer_saturating_due_us(entry->due_us, interval_us);
            if (entry->due_us < cur_now) {
                if (cur_now - entry->due_us > interval_us) {
                    long long lag = cur_now - entry->due_us;
                    long long skips = lag / interval_us;
                    entry->due_us = zan_timer_saturating_due_us(entry->due_us, skips * interval_us);
                    if (entry->due_us <= cur_now) {
                        entry->due_us = zan_timer_saturating_due_us(entry->due_us, interval_us);
                    }
                }
            }
            entry->sequence = ++g_sequence;
            if (heap_push(entry) != 0) {
                timer_unlock();
                free(entry);
                continue;
            }
            timer_unlock();
        } else {
            timer_unlock();
            free(entry);
        }
    }
}

long long zan_timer_dispatch_due(void) {
    /* 派发互斥：已有线程在派发就跳过（那一轮会把到期的都派完）。
     * 用 CAS 而不是锁：派发里会回调用户代码（协程就绪钩子、public
     * 定时器回调），持锁回调有死锁风险。 */
    if (!__sync_bool_compare_and_swap(&g_dispatch_busy, 0, 1)) {
        return 0;
    }
    g_dispatch_owned = 1;
    return timer_dispatch_due_locked();
}

/* 等到能派发为止再派发一轮。
 *
 * 与 zan_timer_dispatch_due 的区别：后者在别的线程正派发时**直接跳过**
 * （worker 池里那是对的——对方会把到期的都派完）。但同步上下文里的定时
 * 等待（A470 的 zan_co_sync_sleep）必须保证"这段时间里到期的定时器确实
 * 被派发"，跳过就等于没修。所以这里**等到互斥空出来**再派。
 */
long long zan_timer_dispatch_due_wait(void) {
    /* 重入保护：派发里会恢复协程（entry->step），那个协程完全可能又
     * 走到同步等待里再来一次派发。此时互斥被**当前线程**持有，等它
     * 释放就是等自己——死锁。识别出"我就是持有者"就直接返回：这一轮
     * 派发（外层）会把到期的都处理掉，不差这一次。 */
    if (g_dispatch_owned) {
        return 0;
    }
    for (;;) {
        /* 抢到互斥才走；抢不到说明别的线程正在派发，让出 CPU 再试。 */
        if (__sync_bool_compare_and_swap(&g_dispatch_busy, 0, 1)) {
            break;
        }
        timer_yield();
    }
    g_dispatch_owned = 1;
    return timer_dispatch_due_locked();
}

long long zan_timer_pending(void) {
    return __atomic_load_n(&g_live, __ATOMIC_RELAXED);
}

int zan_timer_clear(long long id) {
    int found = 0;
    timer_lock();
    for (size_t i = 0; i < g_heap_len; i++) {
        zan_timer_entry *entry = g_heap[i];
        if (entry->kind == ZAN_TIMER_PUBLIC && entry->id == id && !entry->removed) {
            entry->removed = 1;
            __atomic_fetch_sub(&g_live, 1, __ATOMIC_RELAXED);
            found = 1;
            break;
        }
    }
    if (!found && g_dispatching && g_dispatching->kind == ZAN_TIMER_PUBLIC &&
        g_dispatching->id == id && !g_dispatching->removed) {
        g_dispatching->removed = 1;
        found = 1;
    }
    timer_unlock();
    return found;
}

long long zan_timer_clear_all(void) {
    long long count = 0;
    timer_lock();
    for (size_t i = 0; i < g_heap_len; i++) {
        zan_timer_entry *entry = g_heap[i];
        if (entry->kind == ZAN_TIMER_PUBLIC && !entry->removed) {
            entry->removed = 1;
            __atomic_fetch_sub(&g_live, 1, __ATOMIC_RELAXED);
            count++;
        }
    }
    if (g_dispatching && g_dispatching->kind == ZAN_TIMER_PUBLIC &&
        !g_dispatching->removed) {
        g_dispatching->removed = 1;
        count++;
    }
    timer_unlock();
    return count;
}

int zan_timer_info(long long id, long long *exec_msec, long long *exec_count,
                   long long *interval, long long *round, int *removed) {
    int found = 0;
    timer_lock();
    for (size_t i = 0; i < g_heap_len; i++) {
        zan_timer_entry *entry = g_heap[i];
        if (entry->kind == ZAN_TIMER_PUBLIC && entry->id == id) {
            if (exec_msec) *exec_msec = entry->exec_msec;
            if (exec_count) *exec_count = entry->exec_count;
            if (interval) *interval = entry->interval;
            if (round) *round = entry->round;
            if (removed) *removed = entry->removed;
            found = 1;
            break;
        }
    }
    timer_unlock();
    return found;
}

long long zan_timer_list_count(void) {
    long long count = 0;
    timer_lock();
    for (size_t i = 0; i < g_heap_len; i++)
        if (g_heap[i]->kind == ZAN_TIMER_PUBLIC && !g_heap[i]->removed) count++;
    timer_unlock();
    return count;
}

long long zan_timer_list_at(long long index) {
    long long current = 0;
    long long id = 0;
    timer_lock();
    for (size_t i = 0; i < g_heap_len; i++) {
        zan_timer_entry *entry = g_heap[i];
        if (entry->kind != ZAN_TIMER_PUBLIC || entry->removed) continue;
        if (current++ == index) { id = entry->id; break; }
    }
    timer_unlock();
    return id;
}

void zan_timer_stats(long long *initialized, long long *num, long long *round) {
    timer_lock();
    if (initialized) *initialized = g_initialized;
    if (num) {
        size_t count = 0;
        for (size_t i = 0; i < g_heap_len; i++)
            if (g_heap[i]->kind == ZAN_TIMER_PUBLIC && !g_heap[i]->removed) count++;
        *num = (long long)count;
    }
    if (round) *round = g_round;
    timer_unlock();
}

int64_t zan_time_epoch_ms(void) {
#if defined(_WIN32)
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER ul;
    ul.LowPart = ft.dwLowDateTime;
    ul.HighPart = ft.dwHighDateTime;
    return (int64_t)((ul.QuadPart / 10000ULL) - 11644473600000ULL);
#elif defined(CLOCK_REALTIME)
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    }
    return (int64_t)time(NULL) * 1000;
#else
    return (int64_t)time(NULL) * 1000;
#endif
}

int32_t zan_time_local_offset_minutes(int64_t unix_seconds) {
    time_t t = (time_t)unix_seconds;
    struct tm tm_buf;
#if defined(_WIN32)
    if (_localtime64_s(&tm_buf, &t) != 0) return 0;
    time_t wall = _mkgmtime(&tm_buf);
#else
    if (localtime_r(&t, &tm_buf) == NULL) return 0;
    time_t wall = timegm(&tm_buf);
#endif
    if (wall == (time_t)-1) return 0;
    return (int32_t)((wall - t) / 60);
}
