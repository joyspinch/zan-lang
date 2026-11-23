/* rt_timer_defer.c: B-ID78 深树级联析构的延迟释放队列
 *
 * 随 rt_timer.o 进入所有 hosted 链接路径（rt_timer.c 末尾 #include）。曾在
 * rt_mem.c——该对象只在 --fast-alloc 且非 Apple 的链接里存在，macOS 拿到 IR
 * 的同步弱兜底（zan_rt_defer_push 直接调用释放函数），5000 层链表释放爆栈。
 * wasm/bare 不编入，仍走 IR 弱兜底。节点用普通 malloc/free：--wrap 下导向
 * 小对象分配器，分配器不回调释放路径，不会重入。 */

/* ---------- B-ID78 深树级联析构防护：延迟释放队列 ----------
 * 对象树深链（数千层链表/树）经 release 函数递归下钻会耗尽线程栈。
 * 编译器在 zan_rt_release_dyn 里对类释放深度计数，超过阈值把
 * (释放函数, 对象) 交到本队列，最外层出口或进程退出时迭代排空。
 * 对象此刻 rc 已为 0（死对象无人再持），仅延后析构体执行；drain
 * 中再触深的分支会重新入队，循环天然迭代化。队列全局共享 + 锁
 * 保护，释放函数在锁外执行（防 dtor 内再 push 自锁死）。多线程
 * 对 IR 侧深度计数的竞写最坏导致 drain 晚触发——atexit 安全网
 * 兜底排空，不丢释放。 */

typedef struct zan_defer_node {
    struct zan_defer_node *next;
    void (*fn)(void *);
    void *obj;
} zan_defer_node;

#if defined(_WIN32)
static SRWLOCK g_defer_lock = SRWLOCK_INIT;
#define ZAN_DEFER_LOCK_W()   AcquireSRWLockExclusive(&g_defer_lock)
#define ZAN_DEFER_UNLOCK()   ReleaseSRWLockExclusive(&g_defer_lock)
#else
static pthread_mutex_t g_defer_lock = PTHREAD_MUTEX_INITIALIZER;
#define ZAN_DEFER_LOCK_W()   pthread_mutex_lock(&g_defer_lock)
#define ZAN_DEFER_UNLOCK()   pthread_mutex_unlock(&g_defer_lock)
#endif

static zan_defer_node *g_defer_head;
static int g_defer_count;
static int g_defer_exit_hooked;

void zan_rt_defer_drain(void);

static void zan_rt_defer_at_exit(void) {
    zan_rt_defer_drain();
}

void zan_rt_defer_push(void *fn, void *obj) {
    zan_defer_node *n = (zan_defer_node *)malloc(sizeof(*n));
    if (!n) {
        /* 队列节点分配失败：退回同步释放（与旧行为一致，深树极端
         * 场景叠加 OOM 时宁可冒栈深风险也不丢释放） */
        ((void (*)(void *))fn)(obj);
        return;
    }
    n->next = NULL;
    n->fn = (void (*)(void *))fn;
    n->obj = obj;
    ZAN_DEFER_LOCK_W();
    if (!g_defer_exit_hooked) {
        g_defer_exit_hooked = 1;
        atexit(zan_rt_defer_at_exit);
    }
    n->next = g_defer_head;
    g_defer_head = n;
    g_defer_count++;
    ZAN_DEFER_UNLOCK();
}

int zan_rt_defer_pending(void) {
    ZAN_DEFER_LOCK_W();
    int pend = g_defer_count > 0;
    ZAN_DEFER_UNLOCK();
    return pend;
}

void zan_rt_defer_drain(void) {
    for (;;) {
        void (*fn)(void *);
        void *obj;
        ZAN_DEFER_LOCK_W();
        zan_defer_node *n = g_defer_head;
        if (!n) {
            ZAN_DEFER_UNLOCK();
            return;
        }
        g_defer_head = n->next;
        g_defer_count--;
        ZAN_DEFER_UNLOCK();
        fn = n->fn;
        obj = n->obj;
        free(n);
        /* 锁外执行：dtor 内再 push（深树再触阈值）不自锁死 */
        fn(obj);
    }
}

