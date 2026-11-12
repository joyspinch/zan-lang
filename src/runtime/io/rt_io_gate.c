/* Coroutine counting gate. Each prepared wait owns its registration and timer. */
#if defined(ZAN_CO_DRIVER) && !defined(_WIN32)
#include <pthread.h>
#endif
#include <limits.h>

typedef struct zan_gate_waiter {
    struct zan_gate_waiter *next;
    struct zan_gate_waiter *record_next;
    void *frame;
    zan_co_step_t step;
    long long ticket;
    int parked;
    int completed;
    int timer_done;
    int dispatching;
} zan_gate_waiter;

typedef struct zan_gate {
    zan_gate_waiter *head;
    zan_gate_waiter *tail;
    zan_gate_waiter *records;
    long long surplus;
    long long next_ticket;
    int ref_count;
    int closed;
#if defined(ZAN_CO_DRIVER) && defined(_WIN32)
    CRITICAL_SECTION lock;
#elif defined(ZAN_CO_DRIVER)
    pthread_mutex_t lock;
#endif
} zan_gate;

#if defined(ZAN_CO_DRIVER) && defined(_WIN32)
static void zan_gate_lock(zan_gate *g) { EnterCriticalSection(&g->lock); }
static void zan_gate_unlock(zan_gate *g) { LeaveCriticalSection(&g->lock); }
static void zan_gate_lock_init(zan_gate *g) { InitializeCriticalSection(&g->lock); }
static void zan_gate_lock_free(zan_gate *g) { DeleteCriticalSection(&g->lock); }
#elif defined(ZAN_CO_DRIVER)
static void zan_gate_lock(zan_gate *g) { pthread_mutex_lock(&g->lock); }
static void zan_gate_unlock(zan_gate *g) { pthread_mutex_unlock(&g->lock); }
static void zan_gate_lock_init(zan_gate *g) { pthread_mutex_init(&g->lock, NULL); }
static void zan_gate_lock_free(zan_gate *g) { pthread_mutex_destroy(&g->lock); }
#else
static void zan_gate_lock(zan_gate *g) { (void)g; }
static void zan_gate_unlock(zan_gate *g) { (void)g; }
static void zan_gate_lock_init(zan_gate *g) { (void)g; }
static void zan_gate_lock_free(zan_gate *g) { (void)g; }
#endif

static void gate_destroy(zan_gate *g) {
    zan_gate_lock_free(g);
    free(g);
}

static void gate_release(zan_gate *g) {
    zan_gate_lock(g);
    int destroy = --g->ref_count == 0;
    zan_gate_unlock(g);
    if (destroy) gate_destroy(g);
}

static void gate_append(zan_gate *g, zan_gate_waiter *w) {
    w->next = NULL;
    if (g->tail) g->tail->next = w; else g->head = w;
    g->tail = w;
}

static void gate_remove(zan_gate *g, zan_gate_waiter *w) {
    zan_gate_waiter *prev = NULL;
    for (zan_gate_waiter *p = g->head; p; p = p->next) {
        if (p == w) {
            if (prev) prev->next = p->next; else g->head = p->next;
            if (g->tail == p) g->tail = prev;
            return;
        }
        prev = p;
    }
}

static zan_gate_waiter *gate_record(zan_gate *g, long long ticket) {
    for (zan_gate_waiter *w = g->records; w; w = w->record_next)
        if (w->ticket == ticket) return w;
    return NULL;
}

/* A record pins the gate until park has consumed it AND its watchdog is done.
 * A detached wake list additionally pins each record until dispatch completes. */
static void gate_retire(zan_gate *g, zan_gate_waiter *w) {
    if (!w->completed || !w->parked || !w->timer_done || w->dispatching) return;
    if (w->ticket) {
        zan_gate_waiter **p = &g->records;
        while (*p != w) p = &(*p)->record_next;
        *p = w->record_next;
        g->ref_count--;
    }
    free(w);
}

static void gate_complete(zan_gate *g, zan_gate_waiter *w, zan_gate_waiter **ready) {
    w->completed = 1;
    if (w->parked) {
        w->dispatching = 1;
        w->next = *ready;
        *ready = w;
    } else {
        gate_retire(g, w);
    }
}

/* Caller holds a gate reference; enqueue resumes outside the gate mutex. */
static void gate_dispatch(zan_gate *g, zan_gate_waiter *ready) {
    while (ready) {
        zan_gate_waiter *w = ready;
        ready = w->next;
        void *frame = w->frame;
        zan_co_step_t step = w->step;
        zan_gate_lock(g);
        w->dispatching = 0;
        gate_retire(g, w);
        zan_gate_unlock(g);
        zan_co_ready(frame, step);
    }
}

long long zan_gate_new(void) {
    zan_gate *g = (zan_gate *)calloc(1, sizeof(*g));
    if (!g) return 0;
    g->ref_count = 1;
    zan_gate_lock_init(g);
    return (long long)(intptr_t)g;
}

static long long gate_prepare(long long handle, int timed) {
    zan_gate *g = (zan_gate *)(intptr_t)handle;
    if (!g) return 0;
    zan_gate_waiter *w = (zan_gate_waiter *)calloc(1, sizeof(*w));
    if (!w) return 0;
    zan_gate_lock(g);
    if (g->closed) {
        zan_gate_unlock(g);
        free(w);
        return 0;
    }
    do {
        g->next_ticket = g->next_ticket == LLONG_MAX ? 1 : g->next_ticket + 1;
    } while (gate_record(g, g->next_ticket));
    w->ticket = g->next_ticket;
    w->timer_done = !timed;
    w->record_next = g->records;
    g->records = w;
    g->ref_count++;
    if (g->surplus > 0) {
        g->surplus--;
        w->completed = 1;
    } else {
        gate_append(g, w);
    }
    long long ticket = w->ticket;
    zan_gate_unlock(g);
    return ticket;
}

/* Called under the managed Gate lock. Each successful arm MUST be paired with
 * park_ticket(handle,ticket,...) and ticket_signal_and_release(handle,ticket).
 * prepare is the untimed variant and needs only park_ticket. No thread affinity. */
long long zan_gate_arm(long long handle) { return gate_prepare(handle, 1); }
long long zan_gate_prepare(long long handle) { return gate_prepare(handle, 0); }

void zan_gate_park_ticket(long long handle, long long ticket,
                          void *frame, zan_co_step_t step) {
    zan_gate *g = (zan_gate *)(intptr_t)handle;
    if (!g || !step) { if (step) zan_co_ready(frame, step); return; }
    zan_gate_lock(g);
    zan_gate_waiter *w = gate_record(g, ticket);
    if (!w || w->parked) {
        zan_gate_unlock(g);
        zan_co_ready(frame, step);
        return;
    }
    w->frame = frame;
    w->step = step;
    w->parked = 1;
    int completed = w->completed;
    if (completed) gate_retire(g, w);
    int destroy = g->ref_count == 0;
    zan_gate_unlock(g);
    if (destroy) gate_destroy(g);
    if (completed) zan_co_ready(frame, step);
}

/* Original one-argument Park ABI, also used by AsyncGate and native callers. */
void zan_gate_park(long long handle, void *frame, zan_co_step_t step) {
    zan_gate *g = (zan_gate *)(intptr_t)handle;
    if (!g || !step) { if (step) zan_co_ready(frame, step); return; }
    zan_gate_lock(g);
    if (g->closed || g->surplus > 0) {
        if (!g->closed) g->surplus--;
        zan_gate_unlock(g);
        zan_co_ready(frame, step);
        return;
    }
    zan_gate_waiter *w = (zan_gate_waiter *)calloc(1, sizeof(*w));
    if (!w) {
        zan_gate_unlock(g);
        zan_co_ready(frame, step);
        return;
    }
    w->frame = frame;
    w->step = step;
    w->parked = w->timer_done = 1;
    gate_append(g, w);
    zan_gate_unlock(g);
}

static void gate_signal(long long handle, int all) {
    zan_gate *g = (zan_gate *)(intptr_t)handle;
    if (!g) return;
    zan_gate_waiter *ready = NULL;
    zan_gate_lock(g);
    if (g->closed) { zan_gate_unlock(g); return; }
    g->ref_count++; /* protects dispatch against a concurrent Close */
    if (!g->head) {
        g->surplus++;
    } else {
        do {
            zan_gate_waiter *w = g->head;
            g->head = w->next;
            gate_complete(g, w, &ready);
        } while (all && g->head);
        if (!g->head) g->tail = NULL;
    }
    zan_gate_unlock(g);
    gate_dispatch(g, ready);
    gate_release(g);
}

void zan_gate_signal(long long handle) { gate_signal(handle, 0); }
void zan_gate_signal_all(long long handle) { gate_signal(handle, 1); }

void zan_gate_free(long long handle) {
    zan_gate *g = (zan_gate *)(intptr_t)handle;
    if (!g) return;
    zan_gate_waiter *ready = NULL;
    zan_gate_lock(g);
    if (g->closed) { zan_gate_unlock(g); return; }
    g->closed = 1;
    while (g->head) {
        zan_gate_waiter *w = g->head;
        g->head = w->next;
        gate_complete(g, w, &ready);
    }
    g->tail = NULL;
    g->surplus = 0;
    zan_gate_unlock(g);
    gate_dispatch(g, ready);
    gate_release(g); /* owner reference; pending registrations/timers still pin */
}

void zan_gate_ticket_signal_and_release(long long handle, long long ticket) {
    zan_gate *g = (zan_gate *)(intptr_t)handle;
    if (!g) return;
    zan_gate_waiter *ready = NULL;
    zan_gate_lock(g);
    zan_gate_waiter *w = gate_record(g, ticket);
    if (w && !w->timer_done) {
        w->timer_done = 1;
        if (!w->completed) {
            gate_remove(g, w);
            gate_complete(g, w, &ready);
        } else {
            gate_retire(g, w);
        }
    }
    g->ref_count++; /* keep gate alive while dispatching, even after Close */
    zan_gate_unlock(g);
    gate_dispatch(g, ready);
    gate_release(g);
}
