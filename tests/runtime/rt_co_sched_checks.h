/* Shared by the Linux reactor and Windows IOCP tests after rt_io.c. */
typedef struct co_check_case co_check_case;
typedef struct {
  zan_co_fhdr sched;
  co_check_case *test;
} co_check_frame;

struct co_check_case {
  co_check_frame driver, recv, timer, watchdog;
  int nested, steps, received, timed_out;
#if !defined(_WIN32)
  int recv_fd;
#endif
  volatile int done;
  int64_t bytes;
  char buf[8];
};

static void co_check_timer(void *frame) {
  co_check_case *test = ((co_check_frame *)frame)->test;
  __atomic_store_n(&test->done, 1, __ATOMIC_RELEASE);
}

static void co_check_watchdog(void *frame) {
  co_check_case *test = ((co_check_frame *)frame)->test;
  test->timed_out = 1;
  __atomic_store_n(&test->done, 1, __ATOMIC_RELEASE);
}

static void co_check_received(void *frame) {
  co_check_case *test = ((co_check_frame *)frame)->test;
#if defined(_WIN32)
  CHECK(test->bytes == 7);
#else
  CHECK(test->bytes == 1 && test->buf[0] == 'x');
#endif
  test->received++;
  if (test->nested)
    zan_co_delay(0, &test->timer, co_check_timer);
}

static void co_check_receive(void *frame) {
  co_check_case *test = ((co_check_frame *)frame)->test;
#if defined(_WIN32)
  zan_io_recv_to_co((intptr_t)INVALID_SOCKET, test->buf, sizeof(test->buf),
                    60000, frame, co_check_received, &test->bytes);
  completion = submitted;
  CHECK(completion != NULL);
#else
  int pair[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0)
    abort();
  zan_io_set_nonblocking(pair[0]);
  zan_io_recv_co(pair[0], test->buf, sizeof(test->buf), frame,
                 co_check_received, &test->bytes);
  if (write(pair[1], "x", 1) != 1)
    abort();
  close(pair[1]);
  test->recv_fd = pair[0];
#endif
  CHECK(zan_io_has_pending());
}

static void co_check_drive(void *frame) {
  co_check_case *test = ((co_check_frame *)frame)->test;
  if (test->steps++ == 0)
    zan_co_ready(&test->recv, co_check_receive);
  if (test->nested) {
    /* Let the old deadlock fail by assertion instead of hanging the test. */
    zan_co_delay(1500, &test->watchdog, co_check_watchdog);
    zan_co_worker_t *w = co_self();
    long long slice = w->slice_start_us;
    zan_co_sched_run_until(&test->done);
    CHECK(test->received == 1 && !test->timed_out);
    CHECK(w->cur == frame && w->slice_start_us == slice);
    zan_timer_cancel_delay(&test->watchdog);
  } else if (!test->received) {
    /* Keep the queue runnable until IO is delivered or the budget expires. */
    CHECK(test->steps < 2 * ZAN_GLOBAL_TICK);
    if (test->steps < 2 * ZAN_GLOBAL_TICK)
      zan_co_ready(frame, co_check_drive);
  }
  CHECK(co_load(&g_co_parked) == 0 && !co_self()->parked);
}

static void run_co_sched_checks(void) {
  zan_async_set_workers(1);
  for (int nested = 0; nested < 2; nested++) {
    co_check_case test = {0};
    test.nested = nested;
    test.driver.test = test.recv.test = test.timer.test = test.watchdog.test =
        &test;
    /* Publish initial work without starting a competing background pool. */
    InterlockedExchange(&g_co_pool_fg, 1);
    zan_co_ready(&test.driver, co_check_drive);
    InterlockedExchange(&g_co_pool_fg, 0);
    zan_co_sched_run();
    CHECK(test.received == 1 && !test.timed_out);
    CHECK(co_all_idle() && !co_self() && co_load(&g_co_parked) == 0);
    CHECK(!co_load(&g_co_searching) && !g_io_started);
#if !defined(_WIN32)
    close(test.recv_fd);
#endif
  }
}

/* Compiler-facing join routines do not have a public runtime header. */
long long zan_join_new(int npairs, int any);
int zan_join_bind(long long entry, void *frame, int idx);
int zan_join_wait2(long long entry, void *frame, zan_timer_step_t step);
void zan_join_cancel(long long entry);
void *zan_join_exc(long long entry, void **tid_out, int *owned_out);
void zan_join_complete(void *frame);

/* Match the compiler frame prefix through the exception fields read by join. */
typedef struct {
  zan_co_fhdr sched;
  int state, done;
  void *awaiter;
  zan_timer_step_t awaiter_step;
  long long result;
  zan_timer_step_t cleanup;
  int hcount;
  zan_timer_step_t self_step;
  void *exc, *exc_tid;
  int exc_owned;
} join_check_source;

typedef struct {
  long long entry;
  join_check_source *source;
  int tracked, calls;
} join_check_waiter;

static void co_check_join_ready(void *frame, zan_timer_step_t step) {
  step(frame);
}

static void co_check_join_resume(void *frame) {
  join_check_waiter *waiter = frame;
  CHECK(waiter->entry != 0);
  CHECK(zan_co_live_has(waiter->source) == waiter->tracked);
  /* Inline resumption may immediately destroy the join and its pairs. */
  zan_join_cancel(waiter->entry);
  waiter->entry = 0;
  waiter->calls++;
}

static void run_join_checks(void) {
  enum { FANOUT = 80 };
  zan_timer_set_ready_hook(co_check_join_ready);
  for (int untrack = 0; untrack < 2; untrack++) {
    join_check_source shared = {0}, other = {0};
    join_check_waiter waiters[FANOUT] = {0};
    zan_co_live_add(&shared);
    zan_co_live_add(&other);
    for (int i = 0; i < FANOUT; i++) {
      int any = i % 2;
      join_check_waiter *waiter = &waiters[i];
      waiter->entry = zan_join_new(2, any);
      waiter->source = &shared;
      waiter->tracked = !untrack;
      CHECK(zan_join_bind(waiter->entry, &shared, 0) == 1);
      CHECK(zan_join_bind(waiter->entry, any ? &other : &shared, 1) == 1);
      CHECK(zan_join_wait2(waiter->entry, waiter, co_check_join_resume) == 0);
    }
    zan_join_cancel(waiters[0].entry);
    waiters[0].entry = 0;
    join_check_waiter both = {zan_join_new(2, 0), &other, 0, 0};
    CHECK(zan_join_bind(both.entry, &shared, 0) == 1);
    CHECK(zan_join_bind(both.entry, &other, 1) == 1);
    CHECK(zan_join_wait2(both.entry, &both, co_check_join_resume) == 0);
    if (untrack) {
      zan_co_live_del(&shared);
    } else {
      __atomic_store_n(&shared.done, 1, __ATOMIC_RELEASE);
      zan_join_complete(&shared);
    }
    for (int i = 0; i < FANOUT; i++)
      CHECK(waiters[i].calls == (i != 0));
    CHECK(both.calls == 0);
    zan_join_complete(&shared);
    zan_co_live_del(&shared);
    zan_co_live_del(&other);
    CHECK(both.calls == 1);
    zan_join_cancel(both.entry);
    for (int i = 0; i < FANOUT; i++) {
      CHECK(waiters[i].calls == (i != 0));
      zan_join_cancel(waiters[i].entry);
    }
    CHECK(zan_co_live_count() == 0);
  }
  join_check_source failed = {0}, pending = {0};
  int exception, type_id;
  failed.exc = &exception;
  failed.exc_tid = &type_id;
  failed.exc_owned = 1;
  zan_co_live_add(&failed);
  zan_co_live_add(&pending);
  long long entry = zan_join_new(2, 0);
  CHECK(zan_join_bind(entry, &failed, 0) == 1);
  CHECK(zan_join_bind(entry, &pending, 1) == 1);
  __atomic_store_n(&failed.done, 1, __ATOMIC_RELEASE);
  zan_join_complete(&failed);
  void *tid = NULL;
  int owned = 0;
  CHECK(zan_join_exc(entry, &tid, &owned) == &exception);
  CHECK(tid == &type_id && owned == 1);
  CHECK(failed.exc == NULL && failed.exc_owned == 0);
  /* Completion between extraction and detach keeps the later exception. */
  pending.exc = &exception;
  pending.exc_tid = &type_id;
  pending.exc_owned = 1;
  __atomic_store_n(&pending.done, 1, __ATOMIC_RELEASE);
  zan_join_complete(&pending);
  CHECK(pending.exc == &exception && pending.exc_owned == 1);
  zan_join_cancel(entry);
  zan_co_live_del(&failed);
  zan_co_live_del(&pending);
  CHECK(zan_co_live_count() == 0);
  zan_timer_set_ready_hook(zan_co_ready);
}
