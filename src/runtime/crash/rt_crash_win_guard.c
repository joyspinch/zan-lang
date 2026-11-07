/* rt_crash_win_guard.c -- Windows VEH 向量化异常过滤、线程级崩溃恢复与保护调用 */

/* 线程级崩溃恢复状态槽（以线程 ID 为键） */
typedef struct zan__guard zan__guard_t;
typedef struct zan__fault zan__fault_t;

typedef struct zan__thread_slot {
    volatile LONG tid;
    zan__guard_t *top;
    int ready;
    int busy;
} zan__thread_slot;

#define ZAN_GUARD_SLOTS 64

typedef struct zan__shared {
    LONG magic;
    volatile LONG busy;
    volatile LONG recovered;
    volatile LONG logged;
    volatile LONG firstchance;
    volatile LONG installed;
    zan__thread_slot slots[ZAN_GUARD_SLOTS];
} zan__shared_t;

ZAN_CRASH_SHARED zan__shared_t zan__shared = { 0x5A414353, 0, 0, 0, 0, 0, { { 0, 0, 0, 0 } } };

#define zan__slots             zan__shared.slots
#define zan__crash_busy_shared zan__shared.busy
#define zan__guard_recovered   zan__shared.recovered
#define zan__guard_logged      zan__shared.logged
#define zan__guard_firstchance zan__shared.firstchance
#define zan__crash_installed   zan__shared.installed

/* 查找或分配当前线程的崩溃恢复状态槽 */
static zan__thread_slot *zan__slot(int create) {
    LONG tid = (LONG)GetCurrentThreadId();
    unsigned start = ((unsigned)tid * 2654435761u) % ZAN_GUARD_SLOTS;
    for (unsigned i = 0; i < ZAN_GUARD_SLOTS; i++) {
        zan__thread_slot *s = &zan__slots[(start + i) % ZAN_GUARD_SLOTS];
        if (s->tid == tid) return s;
    }
    if (!create) return NULL;
    for (unsigned i = 0; i < ZAN_GUARD_SLOTS; i++) {
        zan__thread_slot *s = &zan__slots[(start + i) % ZAN_GUARD_SLOTS];
        if (InterlockedCompareExchange(&s->tid, tid, 0) == 0) return s;
    }
    return NULL;
}

static int zan__crash_busy_get(zan__thread_slot *s) {
    return s ? s->busy : (int)zan__crash_busy_shared;
}
static void zan__crash_busy_set(zan__thread_slot *s, int v) {
    if (s) s->busy = v; else zan__crash_busy_shared = v;
}

static void zan__crash_write_record(EXCEPTION_POINTERS *ep,
                                   const char *reason,
                                   void **bt, unsigned btn);

static LONG WINAPI zan__crash_filter(EXCEPTION_POINTERS *ep) {
    if (!ep || !ep->ExceptionRecord) return EXCEPTION_EXECUTE_HANDLER;
    zan__thread_slot *s = zan__slot(0);
    if (zan__crash_busy_get(s)) return EXCEPTION_EXECUTE_HANDLER;
    zan__crash_busy_set(s, 1);
    zan__crash_write_record(ep, "unhandled", NULL, 0);
    zan__crash_busy_set(s, 0);
    if (ep->ExceptionRecord->ExceptionCode == 0xE0A2C010) {
        /* 编译器内部生成的非致命崩溃记录触发信号 */
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static void zan__crash_write_record(EXCEPTION_POINTERS *ep,
                                   const char *reason,
                                   void **bt, unsigned btn) {
    if (!ep || !ep->ExceptionRecord) return;

    char logpath[MAX_PATH];
    char exe_dir[MAX_PATH];
    zan__crash_logpath(logpath, sizeof logpath, exe_dir);

    char exe[MAX_PATH];
    if (!GetModuleFileNameA(NULL, exe, (DWORD)sizeof exe)) exe[0] = '\0';
    uintptr_t exe_base = (uintptr_t)GetModuleHandleW(NULL);
    void *frames[62];
    USHORT fn = 0;
    void *arc_freed_by = NULL;

    zan__cf_t f = zan__crash_open_log(logpath);
    if (!f.h) f = zan__crash_open_log("zan_crash.log");
    if (!f.h) {
        char tpath[MAX_PATH];
        zan__crash_temp_logpath(tpath, sizeof tpath);
        if (tpath[0]) f = zan__crash_open_log(tpath);
    }
    if (f.h) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        EXCEPTION_RECORD *er = ep->ExceptionRecord;

        zan__cf_printf(f, "==== ZAN CRASH %04d-%02d-%02d %02d:%02d:%02d.%03d (%s) ====\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                st.wSecond, st.wMilliseconds, reason ? reason : "unhandled");
        zan__cf_printf(f, "exe=%s pid=%lu tid=%lu\n", exe,
                (unsigned long)GetCurrentProcessId(),
                (unsigned long)GetCurrentThreadId());
        zan__cf_printf(f, "code=0x%08lX flags=0x%08lX\n",
                (unsigned long)er->ExceptionCode,
                (unsigned long)er->ExceptionFlags);
        {   /* ARC 完整性断言崩溃代码判定 */
            const char *arc = NULL;
            switch (er->ExceptionCode) {
            case 0xE0A2C001: arc = "retain of an already-freed object"; break;
            case 0xE0A2C002: arc = "release of an already-freed object"; break;
            case 0xE0A2C003: arc = "retain of an already-freed string"; break;
            case 0xE0A2C004: arc = "release of an already-freed string"; break;
            case 0xE0A2C005: arc = "use of a freed object through a stale "
                                   "reference (missing retain when stored)";
                             break;
            case 0xE0A2C006: arc = "use of a freed string through a stale "
                                   "reference (missing retain when stored)";
                             break;
            case 0xE0A2C007: arc = "retain of an already-freed array"; break;
            case 0xE0A2C008: arc = "release of an already-freed array"; break;
            case 0xE0A2C009: arc = "use of a freed array through a stale "
                                   "reference (missing retain when stored)";
                             break;
            default: break;
            }
            /* 运行时断言失败崩溃（越界、除零或 IO 门控断言） */
            if (er->ExceptionCode == 0xE0A2C010 &&
                er->NumberParameters >= 1 && er->ExceptionInformation[0]) {
                zan__cf_printf(f, "zan=%s", (const char *)er->ExceptionInformation[0]);
                if (er->NumberParameters >= 2)
                    zan__cf_printf(f, "exit=%lld\n",
                            (long long)er->ExceptionInformation[1]);
            }
            if (arc) {
                zan__cf_printf(f, "arc=%s\n", arc);
                if (er->NumberParameters >= 3) {
                    zan__cf_printf(f, "arc.object=0x%p arc.refcount=%lld\n",
                            (void *)er->ExceptionInformation[0],
                            (long long)er->ExceptionInformation[1]);
                    arc_freed_by = (void *)er->ExceptionInformation[2];
                    zan__crash_modline(f, arc_freed_by, "arc.freed_by=");
                }
            }
        }
        if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
            er->NumberParameters >= 2) {
            const char *kind = er->ExceptionInformation[0] == 1 ? "write"
                             : er->ExceptionInformation[0] == 8 ? "execute"
                                                                : "read";
            zan__cf_printf(f, "access=%s addr=0x%p\n", kind,
                    (void *)er->ExceptionInformation[1]);
        }
        zan__crash_modline(f, er->ExceptionAddress, "fault=");

#if defined(_M_X64) || defined(__x86_64__)
        if (ep->ContextRecord) {
            CONTEXT *c = ep->ContextRecord;
            zan__cf_printf(f, "rip=%p rsp=%p rbp=%p\n",
                    (void *)c->Rip, (void *)c->Rsp, (void *)c->Rbp);
            zan__cf_printf(f, "rax=%p rbx=%p rcx=%p rdx=%p rsi=%p rdi=%p\n",
                    (void *)c->Rax, (void *)c->Rbx, (void *)c->Rcx,
                    (void *)c->Rdx, (void *)c->Rsi, (void *)c->Rdi);
            {
                ULONG_PTR raw_rsp = ZAN_CTX_SP(c);
                MEMORY_BASIC_INFORMATION raw_mbi;
                if (raw_rsp && VirtualQuery((void *)raw_rsp, &raw_mbi,
                                            sizeof raw_mbi)
                    && raw_mbi.State == MEM_COMMIT
                    && !(raw_mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
                    zan__cf_printf(f, "raw stack (@rsp):\n");
                    for (unsigned w = 0; w < 16; w++) {
                        ULONG_PTR p = raw_rsp + w * sizeof(ULONG_PTR);
                        char tag[16];
                        zan__crash_snprintf(tag, sizeof tag, "  [%02u] ", w);
                        zan__crash_modline(f, (void *)(*(ULONG_PTR *)p), tag);
                    }
                }
            }
        }
#elif defined(_M_ARM64) || defined(__aarch64__)
        if (ep->ContextRecord) {
            CONTEXT *c = ep->ContextRecord;
            zan__cf_printf(f, "pc=%p sp=%p lr=%p fp=%p\n",
                    (void *)c->Pc, (void *)c->Sp, (void *)c->Lr,
                    (void *)c->Fp);
            zan__cf_printf(f, "x0=%p x1=%p x2=%p x3=%p x4=%p x5=%p\n",
                    (void *)c->X0, (void *)c->X1, (void *)c->X2,
                    (void *)c->X3, (void *)c->X4, (void *)c->X5);
        }
#endif

        if (bt) {
            /* 捕获故障发生点调用栈帧 */
            fn = (USHORT)(btn < 62 ? btn : 62);
            for (USHORT i = 0; i < fn; i++) frames[i] = bt[i];
        } else {
            fn = RtlCaptureStackBackTrace(0, 62, frames, NULL);
        }
        if (fn == 0 && ep->ContextRecord)
            zan__crash_stack_scan(f, ZAN_CTX_SP(ep->ContextRecord), exe_base);
        zan__cf_printf(f, "backtrace (%u frames):\n", (unsigned)fn);
        for (USHORT i = 0; i < fn; i++) {
            char tag[16];
            zan__crash_snprintf(tag, sizeof tag, "  #%02u ", (unsigned)i);
            zan__crash_modline(f, frames[i], tag);
        }
        zan__cf_printf(f, "\n");
        zan__cf_close(f);

        DWORD crash_code = er->ExceptionCode;
        if (crash_code != EXCEPTION_STACK_OVERFLOW &&
            crash_code != 0xC0000374  &&
            crash_code != 0xC0000409 /* fail-fast */) {
            zan__crash_symbolize(logpath, exe, exe_dir[0] ? exe_dir : ".\\",
                                 exe_base, ep->ExceptionRecord->ExceptionAddress,
                                 frames, fn, 0);
        }
        if (arc_freed_by)
            zan__crash_symbolize_one(logpath, exe, exe_dir[0] ? exe_dir : ".\\",
                                     exe_base, arc_freed_by);
    }
}

/* 拷贝故障帧异常信息以供恢复帧写入日志 */
struct zan__fault {
    EXCEPTION_RECORD er;
    CONTEXT ctx;
    void *bt[62];
    unsigned btn;
    int have;
};

struct zan__guard {
    void *jb[5];
    struct zan__guard *prev;
    ULONG_PTR sp;
    volatile LONG armed;
    zan__fault_t f;
};

#define ZAN_GUARD_LOG_FULL 20
#define ZAN_GUARD_LOG_EVERY 100

#if defined(_M_X64) || defined(__x86_64__)
static int zan__guard_recoverable(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case 0xE0A2C001: case 0xE0A2C002: case 0xE0A2C003: case 0xE0A2C004:
    case 0xE0A2C005: case 0xE0A2C006: case 0xE0A2C007: case 0xE0A2C008:
    case 0xE0A2C009: case 0xE0A2C010:
        return 1;
    default:
        return 0;
    }
}

/* 崩溃恢复放弃故障帧调用链，不触发析构展开 */
static int zan__guard_rip_recoverable(EXCEPTION_POINTERS *ep) {
    ULONG_PTR rip = ep->ContextRecord ? ZAN_CTX_PC(ep->ContextRecord) : 0;
    HMODULE m = NULL;
    if (!rip ||
        !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)(ULONG_PTR)rip, &m))
        return 0;
    return (uintptr_t)m == (uintptr_t)GetModuleHandleA(NULL);
}
#endif

/* 过滤并记录致命硬件异常 (AV/非法指令等) */
static int zan__guard_hard_fault(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_STACK_OVERFLOW:
    case 0xC0000374:
    case 0xE0A2C001: case 0xE0A2C002: case 0xE0A2C003: case 0xE0A2C004:
    case 0xE0A2C005: case 0xE0A2C006: case 0xE0A2C007: case 0xE0A2C008:
    case 0xE0A2C009:
        return 1;
    default:
        return 0;
    }
}

#if defined(_M_X64) || defined(__x86_64__)
static void zan__guard_resume(void) {
    zan__thread_slot *s = zan__slot(0);
    zan__guard_t *g = s ? s->top : NULL;
    if (!g) ExitProcess(0xE0A2C0FFu);
    __builtin_longjmp(g->jb, 1);
}
#endif

void zan__crash_install(void);

#if defined(_M_X64) || defined(__x86_64__)
/* 恢复栈上写入崩溃记录日志 */
static void zan__guard_log_deferred(zan__thread_slot *s, zan__fault_t *flt) {
    if (!flt->have) return;
    flt->have = 0;
    if (zan__crash_busy_get(s)) return;

    LONG seen = InterlockedIncrement((LONG volatile *)&zan__guard_recovered);
    /* 线程崩溃恢复保护机制 */
    if (seen > ZAN_GUARD_LOG_FULL && seen % ZAN_GUARD_LOG_EVERY != 0) return;

    EXCEPTION_POINTERS p;
    p.ExceptionRecord = &flt->er;
    p.ContextRecord = &flt->ctx;
    zan__crash_busy_set(s, 1);
    zan__crash_write_record(&p, "recovered", flt->bt, flt->btn);
    zan__crash_busy_set(s, 0);
    InterlockedIncrement((LONG volatile *)&zan__guard_logged);
}
#endif /* x64 recovery */

/* 在崩溃恢复点保护下执行函数 (正常返回 1，捕获故障返回 0) */
int zan__guard_call(void (*fn)(void *), void *arg) {
    if (!fn) return 1;
    zan__thread_slot *s = zan__slot(1);
    if (!s) { fn(arg); return 1; } /* out of slots: unguarded, as before */
    if (!s->ready) {
        s->ready = 1;
        /* 重新确立全局未处理异常过滤器所有权 */
        SetUnhandledExceptionFilter(zan__crash_filter);
        zan__crash_install();
        /* 保护页后备栈空间，保证栈溢出时有足够栈深度写入日志 */
        ULONG guarantee = 64 * 1024;
        SetThreadStackGuarantee(&guarantee);
    }

#if defined(_M_X64) || defined(__x86_64__)
    zan__guard_t g;
    volatile char frame_probe = 0;
    g.prev = s->top;
    g.sp = (ULONG_PTR)&frame_probe;
    g.armed = 1;
    g.f.have = 0;
    int ok = 1;
    if (__builtin_setjmp(g.jb) == 0) {
        s->top = &g;
        fn(arg);
    } else {
        ok = 0;
        s->top = g.prev;
        zan__guard_log_deferred(s, &g.f);
    }
    s->top = g.prev;
    (void)frame_probe;
    return ok;
#else
    fn(arg);
    return 1;
#endif
}

static LONG CALLBACK zan__crash_veh(EXCEPTION_POINTERS *ep) {
    if (!ep || !ep->ExceptionRecord || !ep->ContextRecord)
        return EXCEPTION_CONTINUE_SEARCH;
    /* 未受保护线程故障处理 */
    zan__thread_slot *s = zan__slot(0);
    /* 递归二次故障防御，防止死循环 */
    if (zan__crash_busy_get(s)) return EXCEPTION_CONTINUE_SEARCH;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    zan__guard_t *g = s ? s->top : NULL;

#if defined(_M_X64) || defined(__x86_64__)
    if (g && g->armed && zan__guard_recoverable(code) &&
        zan__guard_rip_recoverable(ep)) {
        g->armed = 0;
        g->f.er = *ep->ExceptionRecord;
        g->f.ctx = *ep->ContextRecord;
        g->f.btn = (unsigned)RtlCaptureStackBackTrace(0, 62, g->f.bt, NULL);
        g->f.have = 1;
        ULONG_PTR sp = ((g->sp - 256) & ~(ULONG_PTR)15) - 8;
        ep->ContextRecord->Rsp = (DWORD64)sp;
        ep->ContextRecord->Rip = (DWORD64)(ULONG_PTR)&zan__guard_resume;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
#endif

    if (zan__guard_hard_fault(code)) {
        LONG seen = InterlockedIncrement((LONG volatile *)&zan__guard_firstchance);
        if (seen <= ZAN_GUARD_LOG_FULL || seen % ZAN_GUARD_LOG_EVERY == 0) {
            zan__crash_busy_set(s, 1);
            zan__crash_write_record(ep, "first-chance", NULL, 0);
            zan__crash_busy_set(s, 0);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

long zan__guard_recovered_count(void) {
    return zan__guard_recovered;
}

static void zan__crash_atexit(void) {
    char detail[96];
    zan__crash_snprintf(detail, sizeof detail, "recovered=%ld logged=%ld first-chance=%ld",
             (long)zan__guard_recovered, (long)zan__guard_logged,
             (long)zan__guard_firstchance);
    zan__crash_note("exit", detail);
}

/* 主程序实例标记（区分 DLL 宿主副本） */
static int zan__crash_in_main_image(void) {
    HMODULE m = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)(ULONG_PTR)&zan__crash_in_main_image, &m))
        return 1;
    return (uintptr_t)m == (uintptr_t)GetModuleHandleA(NULL);
}

void zan__crash_install(void) {
#if defined(__SANITIZE_ADDRESS__)
    return;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
    return;
#endif
#endif
    /* 单进程内共享的向量化异常处理程序 */
    if (InterlockedCompareExchange((LONG volatile *)&zan__crash_installed,
                                   1, 0) != 0) return;
    SetUnhandledExceptionFilter(zan__crash_filter);
    /* 向量化异常处理优先于帧异常拦截 */
    AddVectoredExceptionHandler(1, zan__crash_veh);
    zan__crash_note("run", "");
    if (zan__crash_in_main_image()) atexit(zan__crash_atexit);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor)) static void zan__crash_ctor(void) {
    zan__crash_install();
}
#endif
