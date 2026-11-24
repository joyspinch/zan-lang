/* Shared white-box barriers for the Windows and Linux native IO harnesses.
 * Include once before rt_io.c, then with ZAN_OWNER_CHECKS_IMPLEMENTATION. */
#ifndef ZAN_OWNER_CHECKS_IMPLEMENTATION
static void owner_unlocked(void *lock);
static void owner_woke(void);
static void *owner_watched;
static int owner_frees;
static void owner_free(void *ptr) {
  if (ptr && ptr == owner_watched && ++owner_frees != 1)
    abort();
  free(ptr);
}
#if defined(_WIN32)
static BOOL WINAPI owner_post_completion(HANDLE port, DWORD bytes,
                                         ULONG_PTR key, LPOVERLAPPED ov) {
  BOOL ok = PostQueuedCompletionStatus(port, bytes, key, ov);
  if (key == ((ULONG_PTR) - 3))
    owner_woke();
  return ok;
}
static void WINAPI owner_unlock(PSRWLOCK lock) {
  ReleaseSRWLockExclusive(lock);
  owner_unlocked(lock);
}
#define ReleaseSRWLockExclusive owner_unlock
#define PostQueuedCompletionStatus owner_post_completion
#else
static int owner_is_wake_fd(int fd);
static ssize_t owner_write(int fd, const void *buf, size_t n) {
  ssize_t r = write(fd, buf, n);
  if (owner_is_wake_fd(fd))
    owner_woke();
  return r;
}
static int owner_unlock(pthread_mutex_t *lock) {
  int result = pthread_mutex_unlock(lock);
  owner_unlocked(lock);
  return result;
}
#define pthread_mutex_unlock owner_unlock
#define write owner_write
#endif
#define free owner_free
#else
#undef free
#if defined(_WIN32)
#undef ReleaseSRWLockExclusive
#undef PostQueuedCompletionStatus
#else
#undef pthread_mutex_unlock
#undef write
#endif

static zan_blocking_job_t *owner_job;
static int owner_stage, owner_start_failed, owner_barriers, owner_releases;
static int owner_batch, owner_steps, owner_rearms;
static LONG owner_local_out;
static void owner_finish(void) {
  if (owner_start_failed)
    owner_job->status = ZAN_BLOCKING_THREAD_FAILED;
  blocking_spawn(blocking_finish_worker(owner_job));
}
#if defined(_WIN32)
static DWORD WINAPI owner_worker(void *unused) {
  (void)unused;
  owner_finish();
  return 0;
}
#else
static void *owner_worker(void *unused) {
  (void)unused;
  owner_finish();
  return NULL;
}
#endif
static void owner_finish_at_barrier(void) {
  owner_stage = 0;
#if defined(_WIN32)
  HANDLE thread = CreateThread(NULL, 0, owner_worker, NULL, 0, NULL);
  CHECK(thread != NULL);
  if (!thread)
    abort();
  if (WaitForSingleObject(thread, 5000) != WAIT_OBJECT_0)
    abort();
  CloseHandle(thread);
#else
  pthread_t thread;
  if (pthread_create(&thread, NULL, owner_worker, NULL) != 0)
    abort();
  if (pthread_join(thread, NULL) != 0)
    abort();
#endif
  CHECK(owner_frees == 0);
  if (owner_frees)
    abort(); /* Fail before the scanner dereferences freed storage. */
  CHECK(owner_job->started == 0 && g_blocking_active == 0);
  owner_barriers++;
}
static void owner_unlocked(void *lock) {
#if defined(_WIN32)
  void *dns_mutex = &g_dns_srw;
#else
  void *dns_mutex = &g_dns_mx;
#endif
  if (owner_stage == 1 && lock == dns_mutex)
    owner_finish_at_barrier();
}
static void owner_batch_step(void *frame) {
  owner_steps++;
  zan_co_fhdr *h = co_hdr(frame);
  if (!owner_rearms) {
    owner_rearms++;
    zan_co_ready(frame, owner_batch_step);
  } else {
    __atomic_fetch_or(&h->sched, CO_DEAD, __ATOMIC_RELEASE);
  }
}
#if !defined(_WIN32)
static int owner_is_wake_fd(int fd) {
  int n = (int)g_shards;
  if (n < 1)
    n = 1;
  for (int i = 0; i < n; i++) {
    if (fd >= 0 && fd == g_ioshard[i].wake_wfd)
      return 1;
  }
  return 0;
}
#endif
static void owner_woke(void) {
  if (owner_stage == 2)
    owner_finish_at_barrier();
  if (!owner_batch)
    return;
  owner_batch = 0; /* co_run may rearm and notify recursively. */
  CHECK(co_self() == &g_wk[0]);
  CHECK(co_load(&g_wk[0].cnt_out) - owner_local_out == 4);
  CHECK(co_cnt_outstanding() == 4 && !co_all_idle());
  for (int i = 0; i < 5; i++) {
    zan_co_task task;
    CHECK(co_next_task(&g_wk[0], &task));
    co_run(&g_wk[0], &task);
  }
  CHECK(co_cnt_outstanding() == 0 && owner_frees == 1);
}
static void owner_count_release(volatile LONG *counter) {
  if (owner_watched && owner_job && counter == &g_blocking_inflight)
    owner_releases++;
}
static void owner_resume(void *frame) {
  (void)frame;
  owner_steps++;
}
static void run_dns_owner_case(int stage, int start_failed, int started) {
  zan_co_fhdr frame = {0};
  int32_t out = 99;
  int initial_steps = owner_steps, initial_barriers = owner_barriers;
  owner_job = calloc(1, sizeof(*owner_job));
  CHECK(owner_job != NULL);
  owner_watched = owner_job;
  owner_frees = 0;
  owner_releases = 0;
  owner_job->frame = &frame;
  owner_job->step = owner_resume;
  owner_job->out = &out;
  owner_job->started = started;
  owner_job->deadline_ms = 1;
  g_blocking_active = started;
  g_blocking_inflight = 1;
  g_blocking_pending = owner_job;
  if (!started) {
    g_blocking_queued = owner_job;
    g_blocking_queued_n = 1;
  }
  owner_stage = stage;
  owner_start_failed = start_failed;
  g_co_parked = g_wk[0].parked = 1;
  CHECK(dns_timeout_scan() == 1);
  CHECK(owner_barriers == initial_barriers + (stage != 0));
  CHECK(out == 0 && owner_releases == 1);
  CHECK(g_blocking_inflight == 0 && co_cnt_outstanding() == 1);
  CHECK(g_blocking_pending == NULL && g_blocking_queued == NULL);
  CHECK(g_blocking_queued_n == 0);
  if (started && stage == 0) {
    CHECK(owner_frees == 0);
    owner_finish(); /* scanner releases first; worker owns destruction */
  }
  CHECK(owner_frees == 1 && g_blocking_active == 0);
  zan_co_task task;
  CHECK(co_next_task(&g_wk[0], &task));
  CHECK(task.frame == &frame && task.step == owner_resume);
  co_run(&g_wk[0], &task);
  CHECK(owner_steps == initial_steps + 1 && co_all_idle());
  CHECK(co_cnt_outstanding() == 0 && owner_releases == 1);
  owner_watched = NULL;
  owner_job = NULL;
  g_co_parked = 0;
  g_wk[0].parked = 0;
}
static void run_owner_checks(void) {
  run_dns_owner_case(0, 0, 0);
  for (int failed = 0; failed < 2; failed++) {
    run_dns_owner_case(0, failed, 1);
    run_dns_owner_case(1, failed, 1);
    run_dns_owner_case(2, failed, 1);
  }
  CHECK(owner_barriers == 4);
  zan_co_task_t tasks[4];
  for (int i = 0; i < 4; i++) {
    tasks[i].frame = calloc(1, sizeof(zan_co_fhdr));
    CHECK(tasks[i].frame != NULL);
    tasks[i].step = owner_batch_step;
  }
  owner_watched = tasks[0].frame;
  owner_frees = 0;
  owner_steps = 0;
  owner_rearms = 0;
  owner_local_out = co_load(&g_wk[0].cnt_out);
  LONG activity = co_load(&g_wk[0].cnt_act);
  LONG external = co_load(&g_co_cnt_ext.act);
  t_co_self = &g_wk[0];
  CHECK(co_self() == &g_wk[0]);
  g_co_parked = g_wk[0].parked = 1;
  owner_batch = 1;
  zan_co_ready_batch(tasks, 4);
  CHECK(owner_batch == 0 && owner_steps == 5 && owner_rearms == 1);
  CHECK(co_load(&g_wk[0].cnt_act) - activity == 10);
  CHECK(co_load(&g_co_cnt_ext.act) == external);
  CHECK(co_load(&g_wk[0].cnt_out) == owner_local_out && co_all_idle());
  t_co_self = NULL;
  owner_watched = NULL;
  g_co_parked = 0;
  g_wk[0].parked = 0;
  puts("PASS DNS ownership: 7 cases, 4 worker barriers, one free/wake/release "
       "each; TLS local batch: delta 4, immediate consume/rearm/free");
}
#endif
