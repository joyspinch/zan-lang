/* POSIX process contracts. Standalone, like rt_gate_timeout_test.c:
 * cc -std=c11 -O2 -pthread tests/runtime/rt_proc_contract_test.c -o
 * build/cases/rt_proc_contract timeout --kill-after=2s 20s
 * build/cases/rt_proc_contract > build/test-out/rt_proc_contract.log 2>&1
 * Define ZAN_PROC_TEST_LEGACY to reproduce against the old six-argument
 * capture. Every child is finite; alarm is a second watchdog, independent of
 * the API.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../../src/runtime/rt_sync.h"

int64_t zan_monotonic_us(void) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    abort();
  return (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}
#if defined(__linux__) && !defined(ZAN_PROC_TEST_LEGACY)
/* Inject at the real termination boundary, while capture is not reading.
 * This proves the timeout snapshot drain independently of scheduler timing. */
static int snapshot_enabled, snapshot_written, snapshot_writer = -1;
static const char snapshot_marker[] = "timeout-snapshot-only\n";
static int test_pipe2(int fds[2], int flags) {
  int rc = pipe2(fds, flags);
  if (rc == 0 && snapshot_enabled) {
    snapshot_writer = fcntl(fds[1], F_DUPFD_CLOEXEC, 3);
  }
  return rc;
}
static int test_kill(pid_t pid, int sig) {
  if (snapshot_enabled && sig == SIGKILL && snapshot_writer >= 0) {
    /* --silent never writes, so this small marker fits in the empty pipe. */
    snapshot_written =
        write(snapshot_writer, snapshot_marker, sizeof(snapshot_marker) - 1) ==
        (ssize_t)(sizeof(snapshot_marker) - 1);
    close(snapshot_writer);
    snapshot_writer = -1;
  }
  return kill(pid, sig);
}
#define pipe2 test_pipe2
#define kill test_kill
#endif
#include "../../src/runtime/sync/rt_sync_proc.c"
#if defined(__linux__) && !defined(ZAN_PROC_TEST_LEGACY)
#undef pipe2
#undef kill
#endif

static int failures;
static void check(int ok, const char *what) {
  printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  fflush(stdout);
  if (!ok)
    failures++;
}
static int capture(const char *self, const char *mode, int timeout, char **buf,
                   int32_t *len, int32_t *code) {
  const char *args[] = {mode};
#ifdef ZAN_PROC_TEST_LEGACY
  (void)timeout;
  return zan_proc_capture_safe(self, args, 1, buf, len, code);
#else
  return zan_proc_capture_safe_timeout(self, args, 1, buf, len, code, timeout);
#endif
}
static int child(const char *mode) {
  if (!strcmp(mode, "--short")) {
    usleep(80000);
    puts("done");
    return 127;
  }
  if (!strcmp(mode, "--normal")) {
    for (int i = 0; i < 4096; i++)
      puts("0123456789abcdefghijklmnopqrstuvwxyz");
    return 37;
  }
  if (!strcmp(mode, "--eof")) {
    close(1);
    close(2);
  }
  if (!strcmp(mode, "--held-writer")) {
    pid_t pid = fork();
    if (pid < 0)
      return 2;
    if (pid == 0) {
      usleep(3000000);
      _exit(0);
    }
    return 0;
  }
  if (!strcmp(mode, "--partial")) {
    printf("pid=%ld\n", (long)getpid());
    fflush(stdout);
  }
  if (!strcmp(mode, "--flood")) {
    int64_t until = zan_monotonic_us() + 3000000;
    while (zan_monotonic_us() < until)
      puts("0123456789abcdefghijklmnopqrstuvwxyz");
  } else {
    usleep(3000000);
  }
  return 0;
}
static void timed(const char *self, const char *mode) {
  char *buf = NULL;
  int32_t len = 0, code = 0;
  int64_t start = zan_monotonic_us();
  int rc = capture(self, mode, 150, &buf, &len, &code);
  int64_t elapsed = zan_monotonic_us() - start;
  check(rc == 0 && code == -1 && elapsed < 1500000, mode);
  if (!strcmp(mode, "--partial")) {
    long pid = 0;
    check(buf && sscanf(buf, "pid=%ld", &pid) == 1 && pid > 0,
          "partial output survives timeout");
    if (pid > 0)
      check(kill((pid_t)pid, 0) < 0 && errno == ESRCH,
            "timed child exited and reaped");
  }
  zan_proc_free_buf(buf);
}
static void snapshot_drain(const char *self) {
#if defined(__linux__) && !defined(ZAN_PROC_TEST_LEGACY)
  char *buf = NULL;
  int32_t len = 0, code = 0;
  snapshot_enabled = 1;
  snapshot_written = 0;
  int rc = capture(self, "--silent", 150, &buf, &len, &code);
  snapshot_enabled = 0;
  if (snapshot_writer >= 0) {
    close(snapshot_writer);
    snapshot_writer = -1;
  }
  check(snapshot_written && rc == 0 && code == -1 && buf &&
            len == (int32_t)(sizeof(snapshot_marker) - 1) &&
            !strcmp(buf, snapshot_marker),
        "timeout drains bytes buffered at termination boundary");
  zan_proc_free_buf(buf);
#else
  (void)self; /* The deterministic pipe2 injection is Linux-only. */
#endif
}
static void untimed(const char *self, int timeout) {
  char *buf = NULL;
  int32_t len = 0, code = 0;
  int rc = capture(self, "--short", timeout, &buf, &len, &code);
  check(rc == 0 && code == 127 && len == 5 && buf && !strcmp(buf, "done\n"),
        timeout == 0 ? "zero means infinite" : "negative means infinite");
  zan_proc_free_buf(buf);
}
static int descriptor_count(void) {
  int count = 0;
  for (int fd = 0; fd < 256; fd++)
    if (fcntl(fd, F_GETFD) >= 0)
      count++;
  return count;
}
static void write_script(const char *path, const char *text, int mode) {
  FILE *file = fopen(path, "w");
  if (!file || fputs(text, file) < 0 || fclose(file) != 0 ||
      chmod(path, mode) != 0)
    abort();
}
static void closed_streams(const char *self) {
  pid_t pid = fork();
  if (pid < 0) {
    check(0, "closed standard streams fork");
    return;
  }
  if (pid == 0) {
    close(0);
    close(1);
    close(2);
    int ok = zan_proc_start_program_safe("/bin/true", "") == 0 &&
             zan_proc_start_program_safe("/zan-no-such-program-7f4a", "") == -1;
    char *buf = NULL;
    int32_t len = 0, code = 0;
    int rc = capture(self, "--short", 1000, &buf, &len, &code);
    ok = ok && rc == 0 && code == 127 && len == 5 && buf &&
         !strcmp(buf, "done\n");
    zan_proc_free_buf(buf);
    _exit(ok ? 0 : 1);
  }
  int status = 0;
  pid_t waited;
  do {
    waited = waitpid(pid, &status, 0);
  } while (waited < 0 && errno == EINTR);
  check(pid > 0 && waited == pid && WIFEXITED(status) &&
            WEXITSTATUS(status) == 0,
        "launch and capture with closed standard streams");
}
static void launch_contract(void) {
  char dir[] = "/tmp/zan-proc-contract-XXXXXX";
  if (!mkdtemp(dir))
    abort();
  char script[256], log[256], invalid_log[256];
  snprintf(script, sizeof(script), "%s/child with space", dir);
  snprintf(log, sizeof(log), "%s/output.log", dir);
  snprintf(invalid_log, sizeof(invalid_log), "%s/missing/log", dir);
  check(zan_proc_start_program_safe("/zan-no-such-program-7f4a", "") == -1,
        "StartProgram missing executable fails");
  write_script(script, "#!/zan-no-such-interpreter-7f4a\n", 0700);
  check(zan_proc_start_program_safe(script, "") == -1,
        "StartProgram final exec failure");
  write_script(script, "#!/bin/sh\nexit 0\n", 0600);
  check(zan_proc_start_program_safe(script, "") == -1,
        "StartProgram permission failure");
  write_script(script, "#!/bin/sh\nprintf 'started\\n'\nexit 127\n", 0700);
  check(zan_proc_start_program_safe(script, log) == 0,
        "successful exec may exit 127");
  check(zan_proc_start_program_safe(script, invalid_log) == -1,
        "failed log redirection fails launch");
  int seen = 0;
  for (int i = 0; i < 100; i++) {
    FILE *file = fopen(log, "r");
    char text[32] = {0};
    if (file) {
      seen = fgets(text, sizeof(text), file) && !strcmp(text, "started\n");
      fclose(file);
    }
    if (seen)
      break;
    usleep(10000);
  }
  check(seen, "StartProgram redirects output");
  unlink(script);
  unlink(log);
  rmdir(dir);
  const char *args[] = {"-c", "exit 127"};
  check(zan_proc_start_detached_safe("/bin/sh", args, 2) == 0,
        "shared detached launcher accepts successful exec");
  check(zan_proc_start_detached_safe("/zan-no-such-program-7f4a", NULL, 0) ==
            -1,
        "shared detached launcher reports exec failure");
}
int main(int argc, char **argv) {
  alarm(18);
  if (argc > 1)
    return child(argv[1]);
  char *self = realpath(argv[0], NULL);
  if (!self)
    return 2;
  int before = descriptor_count();
  timed(self, "--silent");
  timed(self, "--partial");
  timed(self, "--flood");
  timed(self, "--eof");
  timed(self, "--held-writer");
  snapshot_drain(self);
  untimed(self, 0);
  untimed(self, -1);
  char *buf = NULL;
  int32_t len = 0, code = 0;
  int rc = capture(self, "--normal", 5000, &buf, &len, &code);
  check(rc == 0 && code == 37 && len == 4096 * 37,
        "normal exit drains beyond pipe capacity");
  zan_proc_free_buf(buf);
  const char *short_args[] = {"--short"};
  buf = NULL;
  rc = zan_proc_capture_safe(self, short_args, 1, &buf, &len, &code);
  check(rc == 0 && code == 127 && len == 5,
        "legacy capture ABI remains infinite");
  zan_proc_free_buf(buf);
  int64_t start = zan_monotonic_us();
  const char *silent_args[] = {"--silent"};
  check(zan_proc_run_safe_timeout(self, silent_args, 1, 80) == -1,
        "RunSafe timeout is reported as failure");
  check(zan_monotonic_us() - start < 2000000,
        "RunSafe returns before the child exits");
  check(zan_proc_run_safe_timeout(self, short_args, 1, 5000) == 127,
        "RunSafe returns the completed child's exit code");
  launch_contract();
  closed_streams(self);
  check(descriptor_count() == before, "all parent descriptors closed");
  int status;
  check(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD,
        "all direct children reaped");
  free(self);
  return failures ? 1 : 0;
}
