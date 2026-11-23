/* rt_sync_proc.c: 跨平台安全子进程创建、执行与输出捕获实现 */

#ifdef _WIN32
static void zan_win_buf_append(char *buf, size_t cap, size_t *len,
                               const char *s) {
  if (!s)
    return;
  size_t slen = strlen(s);
  if (*len + slen >= cap)
    return;
  memcpy(buf + *len, s, slen);
  *len += slen;
  buf[*len] = '\0';
}

static void zan_win_append_arg(char *buf, size_t cap, size_t *len,
                               const char *a) {
  if (!a || !*a) {
    zan_win_buf_append(buf, cap, len, "\"\"");
    return;
  }
  int need_q = 0;
  for (const char *p = a; *p; p++) {
    if (*p == ' ' || *p == '\t' || *p == '\"' || *p == '\n' || *p == '\r') {
      need_q = 1;
      break;
    }
  }
  if (!need_q) {
    zan_win_buf_append(buf, cap, len, a);
    return;
  }
  zan_win_buf_append(buf, cap, len, "\"");
  int bs = 0;
  for (const char *p = a; *p; p++) {
    if (*p == '\\') {
      bs++;
    } else if (*p == '\"') {
      for (int b = 0; b < bs * 2 + 1; b++)
        zan_win_buf_append(buf, cap, len, "\\");
      zan_win_buf_append(buf, cap, len, "\"");
      bs = 0;
    } else {
      for (int b = 0; b < bs; b++)
        zan_win_buf_append(buf, cap, len, "\\");
      bs = 0;
      char ch[2] = {*p, '\0'};
      zan_win_buf_append(buf, cap, len, ch);
    }
  }
  for (int b = 0; b < bs * 2; b++)
    zan_win_buf_append(buf, cap, len, "\\");
  zan_win_buf_append(buf, cap, len, "\"");
}

static char *zan_win_quote_cmdline(const char *exe, const char **args,
                                   int32_t argc) {
  size_t cap = 256;
  for (int32_t i = 0; i < argc; i++) {
    if (args[i])
      cap += strlen(args[i]) * 2 + 8;
  }
  if (exe)
    cap += strlen(exe) * 2 + 8;
  char *buf = (char *)malloc(cap);
  if (!buf)
    return NULL;
  buf[0] = '\0';
  size_t len = 0;

  zan_win_append_arg(buf, cap, &len, exe);
  for (int32_t i = 0; i < argc; i++) {
    zan_win_buf_append(buf, cap, &len, " ");
    zan_win_append_arg(buf, cap, &len, args[i]);
  }
  return buf;
}
#endif

int32_t zan_proc_run_safe_timeout(const char *exe, const char **args,
                                  int32_t argc, int32_t timeout_ms) {
  if (!exe || argc < 0 || (argc > 0 && !args))
    return -1;
#ifdef _WIN32
  char *cmdline = zan_win_quote_cmdline(exe, args, argc);
  if (!cmdline)
    return -1;

  int wlen = MultiByteToWideChar(CP_UTF8, 0, cmdline, -1, NULL, 0);
  if (wlen <= 0) {
    free(cmdline);
    return -1;
  }
  wchar_t *wcmd = (wchar_t *)malloc(wlen * sizeof(wchar_t));
  if (!wcmd) {
    free(cmdline);
    return -1;
  }
  MultiByteToWideChar(CP_UTF8, 0, cmdline, -1, wcmd, wlen);
  free(cmdline);

  STARTUPINFOW si;
  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi;
  memset(&pi, 0, sizeof(pi));

  BOOL ok = CreateProcessW(NULL, wcmd, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                           NULL, NULL, &si, &pi);
  free(wcmd);
  if (!ok)
    return -1;

  DWORD result = WaitForSingleObject(
      pi.hProcess, timeout_ms > 0 ? (DWORD)timeout_ms : INFINITE);
  if (result == WAIT_TIMEOUT) {
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, 5000);
  }
  DWORD code = 0;
  BOOL exited =
      result == WAIT_OBJECT_0 && GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return exited ? (int32_t)code : -1;
#else
  char **argv = (char **)malloc(((size_t)argc + 2) * sizeof(char *));
  if (!argv)
    return -1;
  argv[0] = (char *)exe;
  for (int32_t i = 0; i < argc; i++) {
    argv[i + 1] = (char *)args[i];
  }
  argv[argc + 1] = NULL;

  pid_t pid = fork();
  if (pid < 0) {
    free(argv);
    return -1;
  }
  if (pid == 0) {
    execvp(exe, argv);
    _exit(127);
  }
  free(argv);
  int status = 0;
  int64_t deadline =
      timeout_ms > 0 ? zan_monotonic_us() + (int64_t)timeout_ms * 1000 : 0;
  for (;;) {
    pid_t waited = waitpid(pid, &status, deadline ? WNOHANG : 0);
    if (waited == pid)
      break;
    if (waited < 0 && errno != EINTR)
      return -1;
    if (!deadline)
      continue;
    int64_t left = deadline - zan_monotonic_us();
    if (left <= 0) {
      kill(pid, SIGKILL);
      do {
        waited = waitpid(pid, &status, 0);
      } while (waited < 0 && errno == EINTR);
      return -1;
    }
    struct timespec delay = {0, (long)(left < 10000 ? left : 10000) * 1000};
    nanosleep(&delay, NULL);
  }
  if (WIFEXITED(status))
    return (int32_t)WEXITSTATUS(status);
  if (WIFSIGNALED(status))
    return 128 + (int32_t)WTERMSIG(status);
  return -1;
#endif
}

int32_t zan_proc_run_safe(const char *exe, const char **args, int32_t argc) {
  return zan_proc_run_safe_timeout(exe, args, argc, -1);
}

#ifndef _WIN32
#include <stdbool.h>
#include <sys/ioctl.h>
#if defined(__linux__)
#include <stddef.h>
#include <sys/syscall.h>
#endif

#if !defined(__linux__)
static pthread_mutex_t g_proc_pipe_guard = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t g_proc_pipe_once = PTHREAD_ONCE_INIT;
static int g_proc_pipe_guard_error;
static void zan_proc_pipe_fork_prepare(void) {
  pthread_mutex_lock(&g_proc_pipe_guard);
}
static void zan_proc_pipe_fork_done(void) {
  pthread_mutex_unlock(&g_proc_pipe_guard);
}
static void zan_proc_pipe_guard_init(void) {
  g_proc_pipe_guard_error =
      pthread_atfork(zan_proc_pipe_fork_prepare, zan_proc_pipe_fork_done,
                     zan_proc_pipe_fork_done);
}
#endif

static int zan_proc_launch_pipe(int fds[2]) {
#if defined(__linux__)
  if (pipe2(fds, O_CLOEXEC) < 0)
    return -1;
#else
  pthread_once(&g_proc_pipe_once, zan_proc_pipe_guard_init);
  if (g_proc_pipe_guard_error)
    return -1;
  pthread_mutex_lock(&g_proc_pipe_guard);
  if (pipe(fds) < 0) {
    pthread_mutex_unlock(&g_proc_pipe_guard);
    return -1;
  }
  if (fcntl(fds[0], F_SETFD, FD_CLOEXEC) < 0 ||
      fcntl(fds[1], F_SETFD, FD_CLOEXEC) < 0)
    goto failed;
#endif
  for (int i = 0; i < 2; i++) {
    if (fds[i] >= 3)
      continue;
    int moved = fcntl(fds[i], F_DUPFD_CLOEXEC, 3);
    if (moved < 0)
      goto failed;
    close(fds[i]);
    fds[i] = moved;
  }
#if !defined(__linux__)
  pthread_mutex_unlock(&g_proc_pipe_guard);
#endif
  return 0;
failed:
  close(fds[0]);
  close(fds[1]);
#if !defined(__linux__)
  pthread_mutex_unlock(&g_proc_pipe_guard);
#endif
  return -1;
}

/* argv, PATH and scratch storage are prepared in the parent. Use execv so
 * the child calls only async-signal-safe routines, including on PATH lookup. */
static void zan_proc_spawn_exec(char *const argv[], const char *path,
                                char *candidate, char *const env[]) {
  const char *exe = argv[0];
  if (!path) {
    if (env)
      execve(exe, argv, env);
    else
      execv(exe, argv);
    return;
  }
  size_t exe_len = strlen(exe) + 1;
  for (const char *dir = path;;) {
    size_t len = strcspn(dir, ":");
    const char *file = exe;
    if (len > 0) {
      memcpy(candidate, dir, len);
      candidate[len] = '/';
      memcpy(candidate + len + 1, exe, exe_len);
      file = candidate;
    }
    if (env)
      execve(file, argv, env);
    else
      execv(file, argv);
    if (errno != ENOENT && errno != ENOTDIR && errno != EACCES)
      return;
    if (!dir[len])
      return;
    dir += len + 1;
  }
}

static void zan_proc_close_fd(int *fd) {
  if (*fd >= 0)
    close(*fd);
  *fd = -1;
}

#include "rt_sync_proc_sandbox.c"

static int32_t proc_spawn(char *const argv[], const char *cwd, int32_t fds[2],
                          bool managed, intptr_t sandbox_group, int ruleset) {
#ifndef ZAN_PROC_SANDBOX_LINUX
  (void)sandbox_group;
  (void)ruleset;
#endif
  if (!argv || !argv[0] || !argv[0][0] || !fds)
    return -1;
  char *path = NULL, *candidate = NULL;
  int32_t result = -1;
  enum { INPUT_PIPE, OUTPUT_PIPE, LAUNCH_PIPE, PIPE_COUNT };
  int pipes[PIPE_COUNT][2];
  int opened = 0;
  if (!strchr(argv[0], '/')) {
    const char *env_path = sandbox_group ? "/usr/bin:/bin" : getenv("PATH");
    path = strdup(env_path ? env_path : "/bin:/usr/bin");
    if (!path)
      goto done;
    candidate = (char *)malloc(strlen(path) + strlen(argv[0]) + 2);
    if (!candidate)
      goto done;
  }
  for (; opened < PIPE_COUNT; opened++) {
    if (zan_proc_launch_pipe(pipes[opened]) < 0)
      goto done;
  }
  int *input = pipes[INPUT_PIPE], *output = pipes[OUTPUT_PIPE];
  int *launch = pipes[LAUNCH_PIPE];
  int flags = fcntl(output[0], F_GETFL);
  if (flags < 0 || fcntl(output[0], F_SETFL, flags | O_NONBLOCK) < 0)
    goto done;

  pid_t pid = fork();
  if (pid < 0)
    goto done;
  if (pid == 0) {
    close(launch[0]);
    if ((!managed || setpgid(0, 0) == 0) &&
        (!cwd || !cwd[0] || chdir(cwd) == 0) &&
        dup2(input[0], STDIN_FILENO) >= 0 &&
        dup2(output[1], STDOUT_FILENO) >= 0 &&
        dup2(output[1], STDERR_FILENO) >= 0) {
      close(input[0]);
      close(input[1]);
      close(output[0]);
      close(output[1]);
#ifdef ZAN_PROC_SANDBOX_LINUX
      if (sandbox_group) {
        if (sandbox_enter((zan_proc_sandbox_group *)sandbox_group, ruleset,
                          launch[1]) == 0) {
          char *const env[] = {"PATH=/usr/bin:/bin", "LANG=C", "TMPDIR=.",
                               NULL};
          zan_proc_spawn_exec(argv, path, candidate, env);
        }
      } else
#endif
      {
        zan_proc_spawn_exec(argv, path, candidate, NULL);
      }
    }
    /* EOF on this CLOEXEC pipe confirms exec, even if the program exits 127. */
    char failed = 1;
    while (write(launch[1], &failed, 1) < 0 && errno == EINTR) {
    }
    _exit(127);
  }

  zan_proc_close_fd(&input[0]);
  zan_proc_close_fd(&output[1]);
  zan_proc_close_fd(&launch[1]);
  char failed;
  ssize_t received;
  do {
    received = read(launch[0], &failed, 1);
  } while (received < 0 && errno == EINTR);
  if (received == 0) {
    fds[0] = input[1];
    fds[1] = output[0];
    input[1] = output[0] = -1;
    result = (int32_t)pid;
  } else {
    if (received < 0) {
      if (managed)
        kill(-pid, SIGKILL);
      kill(pid, SIGKILL);
    }
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
    }
  }

done:
  for (int i = 0; i < opened; i++) {
    zan_proc_close_fd(&pipes[i][0]);
    zan_proc_close_fd(&pipes[i][1]);
  }
  free(candidate);
  free(path);
  return result;
}

int32_t zan_proc_spawn(char *const argv[], const char *cwd, int32_t fds[2]) {
  return proc_spawn(argv, cwd, fds, false, 0, -1);
}

int32_t zan_proc_spawn_managed(char *const argv[], const char *cwd,
                               int32_t fds[2]) {
  return proc_spawn(argv, cwd, fds, true, 0, -1);
}

int32_t zan_proc_sandbox_available(const char *cgroup_root) {
#ifdef ZAN_PROC_SANDBOX_LINUX
  return sandbox_unprivileged() && sandbox_cgroup_root(cgroup_root) &&
         syscall(__NR_landlock_create_ruleset, NULL, 0,
                 LANDLOCK_CREATE_RULESET_VERSION) >= ZAN_LANDLOCK_ABI;
#else
  (void)cgroup_root;
  return 0;
#endif
}

int32_t zan_proc_spawn_sandboxed(char *const argv[], const char *cwd,
                                 const char *read_roots, const char *write_root,
                                 const char *cgroup_root, int32_t fds[2],
                                 intptr_t *handle) {
#ifdef ZAN_PROC_SANDBOX_LINUX
  if (!handle || !zan_proc_sandbox_available(cgroup_root))
    return -1;
  *handle = 0;
  int ruleset = sandbox_ruleset(read_roots, write_root);
  if (ruleset < 0)
    return -1;
  zan_proc_sandbox_group *group = sandbox_group_create(cgroup_root);
  int32_t pid = -1;
  if (group) {
    pid = proc_spawn(argv, cwd, fds, true, (intptr_t)group, ruleset);
    if (pid < 0)
      zan_proc_sandbox_dispose((intptr_t)group, 0, -1, -1);
    else
      *handle = (intptr_t)group;
  }
  close(ruleset);
  return pid;
#else
  (void)argv;
  (void)cwd;
  (void)read_roots;
  (void)write_root;
  (void)cgroup_root;
  (void)fds;
  (void)handle;
  return -1;
#endif
}

int32_t zan_proc_spawn_available(int32_t fd) {
  int count = 0;
  return ioctl(fd, FIONREAD, &count) == 0 ? count : 0;
}

int32_t zan_proc_spawn_read(int32_t fd, void *buf, int32_t count) {
  if (count <= 0)
    return 0;
  ssize_t received;
  do {
    received = read(fd, buf, (size_t)count);
  } while (received < 0 && errno == EINTR);
  return (int32_t)received;
}

int32_t zan_proc_spawn_write(int32_t fd, const void *buf, int32_t count) {
  if (count < 0)
    return 0;
  sigset_t pipe_signal, previous, pending;
  sigemptyset(&pipe_signal);
  sigaddset(&pipe_signal, SIGPIPE);
  if (pthread_sigmask(SIG_BLOCK, &pipe_signal, &previous) != 0)
    return 0;

  int32_t written = 0;
  if (sigpending(&pending) == 0) {
    int was_pending = sigismember(&pending, SIGPIPE);
    while (written < count) {
      ssize_t n =
          write(fd, (const char *)buf + written, (size_t)(count - written));
      if (n > 0) {
        written += (int32_t)n;
        continue;
      }
      if (n < 0 && errno == EINTR)
        continue;
      /* Consume only the SIGPIPE from this write. If SIGPIPE is ignored,
       * the signal may not be pending at all. Leave the host policy intact. */
      if (n < 0 && errno == EPIPE && was_pending == 0 &&
          sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE) == 1) {
        int signal_number;
        while (sigwait(&pipe_signal, &signal_number) == EINTR) {
        }
      }
      break;
    }
  }
  pthread_sigmask(SIG_SETMASK, &previous, NULL);
  return written == count;
}

int32_t zan_proc_spawn_kill(int32_t pid) {
  return pid > 0 && kill((pid_t)pid, SIGKILL) == 0;
}

static bool proc_signal_group(pid_t pid) {
  return kill(-pid, SIGKILL) == 0 || errno == ESRCH;
}

int32_t zan_proc_spawn_managed_kill(int32_t pid) {
  if (pid <= 0)
    return 0;
  siginfo_t info = {0};
  int observed;
  do {
    observed = waitid(P_PID, (id_t)pid, &info, WEXITED | WNOWAIT | WNOHANG);
  } while (observed < 0 && errno == EINTR);
  return observed == 0 && proc_signal_group((pid_t)pid);
}

static int32_t proc_wait(int32_t pid, int32_t timeout_ms, bool managed) {
  if (pid <= 0)
    return -1;
  int64_t deadline =
      timeout_ms > 0 ? zan_monotonic_us() + (int64_t)timeout_ms * 1000 : 0;
  for (;;) {
    int status;
    int flags = deadline ? WNOHANG : 0;
    if (managed) {
      /* Hold the leader's PID until its group has been signalled. */
      siginfo_t info = {0};
      if (waitid(P_PID, (id_t)pid, &info, WEXITED | WNOWAIT | flags) < 0) {
        if (errno == EINTR)
          goto delay;
        return errno == ECHILD ? -1 : ZAN_PROC_WAIT_OWNED_ERROR;
      }
      if (info.si_pid == 0)
        goto delay;
      if (!proc_signal_group((pid_t)pid))
        return ZAN_PROC_WAIT_OWNED_ERROR;
      flags = 0;
    }
    pid_t waited = waitpid((pid_t)pid, &status, flags);
    if (waited == pid) {
      if (WIFEXITED(status))
        return (int32_t)WEXITSTATUS(status);
      if (WIFSIGNALED(status))
        return 128 + (int32_t)WTERMSIG(status);
    }
    if (waited < 0 && errno != EINTR) {
      return managed && errno != ECHILD ? ZAN_PROC_WAIT_OWNED_ERROR : -1;
    }
  delay:
    if (!deadline)
      continue;
    int64_t left = deadline - zan_monotonic_us();
    if (left <= 0)
      return -258;
    struct timespec sleep_for = {0, (long)(left < 1000 ? left : 1000) * 1000};
    nanosleep(&sleep_for, NULL);
  }
}

int32_t zan_proc_spawn_wait(int32_t pid, int32_t timeout_ms) {
  return proc_wait(pid, timeout_ms, false);
}

int32_t zan_proc_spawn_managed_wait(int32_t pid, int32_t timeout_ms) {
  return proc_wait(pid, timeout_ms, true);
}

int32_t zan_proc_sandbox_kill(intptr_t handle) {
#ifdef ZAN_PROC_SANDBOX_LINUX
  zan_proc_sandbox_group *group = (zan_proc_sandbox_group *)handle;
  return group && sandbox_write(group->kill, "1", 1) == 0;
#else
  (void)handle;
  return 0;
#endif
}

int32_t zan_proc_sandbox_wait(int32_t pid, int32_t timeout_ms,
                              intptr_t handle) {
#ifdef ZAN_PROC_SANDBOX_LINUX
  zan_proc_sandbox_group *group = (zan_proc_sandbox_group *)handle;
  if (!group || pid <= 0)
    return -1;
  int64_t deadline =
      timeout_ms > 0 ? zan_monotonic_us() + (int64_t)timeout_ms * 1000 : 0;
  for (;;) {
    siginfo_t info = {0};
    if (waitid(P_PID, (id_t)pid, &info, WEXITED | WNOWAIT | WNOHANG) == 0) {
      if (info.si_pid != 0) {
        if (!zan_proc_sandbox_kill(handle))
          return ZAN_PROC_WAIT_OWNED_ERROR;
        if (sandbox_group_empty(group))
          return proc_wait(pid, 0, false);
      }
    } else if (errno != EINTR) {
      return errno == ECHILD ? -1 : ZAN_PROC_WAIT_OWNED_ERROR;
    }
    if (deadline && zan_monotonic_us() >= deadline)
      return -258;
    struct timespec delay = {0, 1000000};
    nanosleep(&delay, NULL);
  }
#else
  (void)pid;
  (void)timeout_ms;
  (void)handle;
  return -1;
#endif
}

#ifdef ZAN_PROC_SANDBOX_LINUX
static void *proc_sandbox_reap(void *arg) {
  zan_proc_sandbox_group *group = arg;
  const struct timespec delay = {0, 100000000};
  for (int retry = 0; retry < 50; ++retry) {
    if (sandbox_group_empty(group) && sandbox_group_remove(group))
      break;
    zan_proc_sandbox_kill((intptr_t)group);
    nanosleep(&delay, NULL);
  }
  sandbox_group_free(group);
  return NULL;
}
#endif

int32_t zan_proc_sandbox_dispose(intptr_t handle, int32_t pid, int32_t input,
                                 int32_t output) {
  if (input >= 0)
    close(input);
  if (output >= 0)
    close(output);
#ifdef ZAN_PROC_SANDBOX_LINUX
  zan_proc_sandbox_group *group = (zan_proc_sandbox_group *)handle;
  if (!group)
    return 0;
  int32_t reaped = pid <= 0;
  if (pid > 0) {
    zan_proc_sandbox_kill(handle);
    reaped = zan_proc_spawn_managed_dispose(pid, -1, -1);
  }
  if (sandbox_group_empty(group) && sandbox_group_remove(group)) {
    sandbox_group_free(group);
    return reaped;
  }
  pthread_t reaper;
  if (pthread_create(&reaper, NULL, proc_sandbox_reap, group) == 0) {
    pthread_detach(reaper);
  } else {
    zan_proc_sandbox_kill(handle);
    sandbox_group_free(group);
  }
#else
  (void)handle;
  (void)pid;
#endif
  return 0;
}

static void *proc_reap_child(void *arg) {
  zan_proc_spawn_wait((int32_t)(intptr_t)arg, 0);
  return NULL;
}

int32_t zan_proc_spawn_managed_dispose(int32_t pid, int32_t input,
                                       int32_t output) {
  if (input >= 0)
    close(input);
  if (output >= 0)
    close(output);
  if (pid <= 0)
    return 1;

  zan_proc_spawn_managed_kill(pid);
  int32_t code = zan_proc_spawn_managed_wait(pid, 1);
  if (code >= 0)
    return 1;
  if (code == -1)
    return 0;

  /* Retire the direct child even if group cleanup failed or is still pending.
   */
  if (code == ZAN_PROC_WAIT_OWNED_ERROR)
    zan_proc_spawn_kill(pid);
  pthread_t reaper;
  if (pthread_create(&reaper, NULL, proc_reap_child, (void *)(intptr_t)pid) ==
      0)
    pthread_detach(reaper);
  return 0;
}

void zan_proc_spawn_dispose(int32_t pid, int32_t input, int32_t output) {
  if (input >= 0)
    close(input);
  if (output >= 0)
    close(output);
  if (pid <= 0)
    return;

  int status;
  pid_t waited;
  do {
    waited = waitpid((pid_t)pid, &status, WNOHANG);
  } while (waited < 0 && errno == EINTR);
  if (waited < 0 ||
      (waited == pid && (WIFEXITED(status) || WIFSIGNALED(status))))
    return;
  zan_proc_spawn_kill(pid);
  zan_proc_spawn_wait(pid, 0);
}

/* The CLOEXEC writer reports failure until the final exec succeeds. Reuse this
 * handshake for both detached APIs: reaping the intermediate fork alone cannot
 * distinguish successful exec from a missing executable/interpreter. */
static int32_t zan_proc_launch_detached(const char *exe, char *const argv[],
                                        const char *log_path) {
  int launch_pipe[2];
  if (zan_proc_launch_pipe(launch_pipe) < 0)
    return -1;
  pid_t pid = fork();
  if (pid < 0) {
    close(launch_pipe[0]);
    close(launch_pipe[1]);
    return -1;
  }
  if (pid == 0) {
    close(launch_pipe[0]);
    if (setsid() < 0)
      goto launch_failed;
    pid_t gc = fork();
    if (gc < 0)
      goto launch_failed;
    if (gc == 0) {
      int input = open("/dev/null", O_RDONLY);
      if (input < 0)
        goto launch_failed;
      int ok = dup2(input, STDIN_FILENO);
      if (input > STDERR_FILENO)
        close(input);
      if (ok < 0)
        goto launch_failed;
      int output = log_path && log_path[0]
                       ? open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644)
                       : open("/dev/null", O_WRONLY);
      if (output < 0)
        goto launch_failed;
      int out_ok = dup2(output, STDOUT_FILENO);
      int err_ok = dup2(output, STDERR_FILENO);
      if (output > STDERR_FILENO)
        close(output);
      if (out_ok < 0 || err_ok < 0)
        goto launch_failed;
      execvp(exe, argv);
      goto launch_failed;
    }
    close(launch_pipe[1]);
    _exit(0);
  launch_failed: {
    char failed = 1;
    while (write(launch_pipe[1], &failed, 1) < 0 && errno == EINTR) {
    }
    _exit(127);
  }
  }
  close(launch_pipe[1]);
  char failed;
  ssize_t launch_result;
  do {
    launch_result = read(launch_pipe[0], &failed, 1);
  } while (launch_result < 0 && errno == EINTR);
  close(launch_pipe[0]);
  int st;
  while (waitpid(pid, &st, 0) < 0) {
    if (errno != EINTR)
      return -1;
  }
  return launch_result == 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0 ? 0 : -1;
}
#endif

int32_t zan_proc_start_detached_safe(const char *exe, const char **args,
                                     int32_t argc) {
  if (!exe || argc < 0 || (argc > 0 && !args))
    return -1;
#ifdef _WIN32
  char *cmdline = zan_win_quote_cmdline(exe, args, argc);
  if (!cmdline)
    return -1;

  int wlen = MultiByteToWideChar(CP_UTF8, 0, cmdline, -1, NULL, 0);
  if (wlen <= 0) {
    free(cmdline);
    return -1;
  }
  wchar_t *wcmd = (wchar_t *)malloc(wlen * sizeof(wchar_t));
  if (!wcmd) {
    free(cmdline);
    return -1;
  }
  MultiByteToWideChar(CP_UTF8, 0, cmdline, -1, wcmd, wlen);
  free(cmdline);

  STARTUPINFOW si;
  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi;
  memset(&pi, 0, sizeof(pi));

  BOOL ok =
      CreateProcessW(NULL, wcmd, NULL, NULL, FALSE,
                     CREATE_NO_WINDOW | DETACHED_PROCESS, NULL, NULL, &si, &pi);
  free(wcmd);
  if (!ok)
    return -1;

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return 0;
#else
  char **argv = (char **)malloc(((size_t)argc + 2) * sizeof(char *));
  if (!argv)
    return -1;
  argv[0] = (char *)exe;
  for (int32_t i = 0; i < argc; i++) {
    argv[i + 1] = (char *)args[i];
  }
  argv[argc + 1] = NULL;

  int32_t result = zan_proc_launch_detached(exe, argv, NULL);
  free(argv);
  return result;
#endif
}

int32_t zan_proc_start_program_safe(const char *exe, const char *log_path) {
  if (!exe)
    return -1;
#ifdef _WIN32
  STARTUPINFOW si;
  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi;
  memset(&pi, 0, sizeof(pi));

  HANDLE hLog = INVALID_HANDLE_VALUE;
  DWORD flags = DETACHED_PROCESS;
  BOOL inherit = TRUE;

  if (log_path && log_path[0]) {
    int wpath_len = MultiByteToWideChar(CP_UTF8, 0, log_path, -1, NULL, 0);
    if (wpath_len > 0) {
      wchar_t *wlog = (wchar_t *)malloc(wpath_len * sizeof(wchar_t));
      if (wlog) {
        MultiByteToWideChar(CP_UTF8, 0, log_path, -1, wlog, wpath_len);
        SECURITY_ATTRIBUTES sa;
        memset(&sa, 0, sizeof(sa));
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        hLog = CreateFileW(wlog, FILE_APPEND_DATA | SYNCHRONIZE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
        free(wlog);
      }
    }
  }

  if (hLog != INVALID_HANDLE_VALUE) {
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = hLog;
    si.hStdError = hLog;
    flags = CREATE_NO_WINDOW;
  }

  /* 路径安全转义包装防注入 */
  int wexe_len = MultiByteToWideChar(CP_UTF8, 0, exe, -1, NULL, 0);
  if (wexe_len <= 0) {
    if (hLog != INVALID_HANDLE_VALUE)
      CloseHandle(hLog);
    return -1;
  }
  wchar_t *wcmd = (wchar_t *)malloc((wexe_len + 4) * sizeof(wchar_t));
  if (!wcmd) {
    if (hLog != INVALID_HANDLE_VALUE)
      CloseHandle(hLog);
    return -1;
  }
  wcmd[0] = L'"';
  MultiByteToWideChar(CP_UTF8, 0, exe, -1, wcmd + 1, wexe_len);
  wcmd[wexe_len] = L'"';
  wcmd[wexe_len + 1] = L'\0';

  BOOL ok = CreateProcessW(NULL, wcmd, NULL, NULL, inherit, flags, NULL, NULL,
                           &si, &pi);
  free(wcmd);
  if (hLog != INVALID_HANDLE_VALUE)
    CloseHandle(hLog);
  if (!ok)
    return -1;

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return 0;
#else
  char *argv[] = {(char *)exe, NULL};
  return zan_proc_launch_detached(exe, argv, log_path);
#endif
}

#ifndef _WIN32
static int zan_proc_capture_append(char **buf, size_t *cap, size_t *len,
                                   const char *bytes, size_t count) {
  /* out_len is int32_t; never truncate it or overflow the grow calculation. */
  if (count > (size_t)2147483647 - *len)
    return -1;
  size_t need = *len + count + 1;
  if (need > *cap) {
    size_t grown = *cap;
    while (grown < need)
      grown *= 2;
    char *next = (char *)realloc(*buf, grown);
    if (!next)
      return -1;
    *buf = next;
    *cap = grown;
  }
  memcpy(*buf + *len, bytes, count);
  *len += count;
  return 0;
}

/* Preserve the original six-argument ABI. Only this new POSIX entry receives a
 * deadline; timeout_ms <= 0 means infinite, just like the public Zan API.
 * A timeout is a captured result (return 0, exit_code -1), retaining partial
 * output. Other errors return -1 and transfer no buffer to the caller. */
int32_t zan_proc_capture_safe_timeout(const char *exe, const char **args,
                                      int32_t argc, char **out_buf,
                                      int32_t *out_len, int32_t *exit_code,
                                      int32_t timeout_ms) {
  if (!out_buf || !out_len || !exit_code)
    return -1;
  *out_buf = NULL;
  *out_len = 0;
  *exit_code = -1;
  if (!exe || argc < 0 || (argc > 0 && !args))
    return -1;
  int64_t deadline =
      timeout_ms > 0 ? zan_monotonic_us() + (int64_t)timeout_ms * 1000 : 0;
  int pipefd[2];
  if (zan_proc_launch_pipe(pipefd) < 0)
    return -1;
  int flags = fcntl(pipefd[0], F_GETFL);
  if (flags < 0 || fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK) < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return -1;
  }
  size_t cap = 4096, len = 0;
  char *buf = (char *)malloc(cap);
  char **argv = (char **)malloc(((size_t)argc + 2) * sizeof(char *));
  if (!buf || !argv) {
    free(buf);
    free(argv);
    close(pipefd[0]);
    close(pipefd[1]);
    return -1;
  }
  argv[0] = (char *)exe;
  for (int32_t i = 0; i < argc; i++)
    argv[i + 1] = (char *)args[i];
  argv[argc + 1] = NULL;
  pid_t pid = fork();
  if (pid < 0) {
    free(buf);
    free(argv);
    close(pipefd[0]);
    close(pipefd[1]);
    return -1;
  }
  if (pid == 0) {
    close(pipefd[0]);
    if (dup2(pipefd[1], STDOUT_FILENO) < 0 ||
        dup2(pipefd[1], STDERR_FILENO) < 0)
      _exit(127);
    close(pipefd[1]);
    execvp(exe, argv);
    _exit(127);
  }
  free(argv);
  close(pipefd[1]);
  int reaped = 0, eof = 0, failed = 0, timed_out = 0, status = 0;
  char bytes[4096];
  for (;;) {
    if (deadline && zan_monotonic_us() >= deadline) {
      timed_out = 1;
      break;
    }
    if (!reaped) {
      pid_t waited = waitpid(pid, &status, WNOHANG);
      if (waited == pid)
        reaped = 1;
      else if (waited < 0) {
        if (errno == EINTR)
          continue; /* Recheck the deadline too. */
        if (errno == ECHILD)
          reaped = 1; /* No longer own this PID. */
        failed = 1;
        break;
      }
    }
    if (!eof) {
      ssize_t got = read(pipefd[0], bytes, sizeof(bytes));
      if (got > 0) {
        if (zan_proc_capture_append(&buf, &cap, &len, bytes, (size_t)got) < 0) {
          failed = 1;
          break;
        }
        /* One chunk per iteration: an always-ready writer must not
         * hide the clock behind an unbounded drain loop. */
        continue;
      }
      if (got == 0)
        eof = 1;
      else if (errno == EINTR)
        continue;
      else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        failed = 1;
        break;
      }
    }
    if (reaped && eof)
      break;
    int wait_ms = 10;
    if (deadline) {
      int64_t left = deadline - zan_monotonic_us();
      if (left <= 0)
        continue;
      if (left < 10000)
        wait_ms = (int)((left + 999) / 1000);
    }
    /* EOF does not mean the child exited. Conversely, child exit does not
     * mean EOF when descendants inherited stdout. Both obey the deadline. */
    struct pollfd ready = {eof ? -1 : pipefd[0], POLLIN, 0};
    int polled = poll(&ready, 1, wait_ms);
    if ((polled < 0 && errno != EINTR) || (ready.revents & POLLNVAL)) {
      failed = 1;
      break;
    }
  }
  if ((timed_out || failed) && !reaped) {
    kill(pid, SIGKILL);
    pid_t waited;
    do {
      waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited != pid)
      failed = 1;
  }
  if (timed_out && !failed && !eof) {
    /* Drain only the bytes already buffered after termination/reap. A
     * descendant may keep writing; waiting for its EOF would defeat timeout. */
    int pending = 0;
    if (ioctl(pipefd[0], FIONREAD, &pending) == 0) {
      while (pending > 0) {
        size_t count =
            pending < (int)sizeof(bytes) ? (size_t)pending : sizeof(bytes);
        ssize_t got = read(pipefd[0], bytes, count);
        if (got <= 0)
          break;
        if (zan_proc_capture_append(&buf, &cap, &len, bytes, (size_t)got) < 0) {
          failed = 1;
          break;
        }
        pending -= (int)got;
      }
    }
  }
  close(pipefd[0]);
  if (failed) {
    free(buf);
    return -1;
  }
  buf[len] = '\0';
  *out_buf = buf;
  *out_len = (int32_t)len;
  if (!timed_out) {
    if (WIFEXITED(status))
      *exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
      *exit_code = 128 + WTERMSIG(status);
  }
  return 0;
}
#endif

int32_t zan_proc_capture_safe(const char *exe, const char **args, int32_t argc,
                              char **out_buf, int32_t *out_len,
                              int32_t *exit_code) {
  if (!exe || !out_buf || !out_len || !exit_code)
    return -1;
  *out_buf = NULL;
  *out_len = 0;
  *exit_code = -1;

#ifdef _WIN32
  SECURITY_ATTRIBUTES sa;
  memset(&sa, 0, sizeof(sa));
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE hRead = NULL;
  HANDLE hWrite = NULL;
  if (!CreatePipe(&hRead, &hWrite, &sa, 0))
    return -1;
  SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si;
  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags |= STARTF_USESTDHANDLES;
  si.hStdOutput = hWrite;
  si.hStdError = hWrite;

  PROCESS_INFORMATION pi;
  memset(&pi, 0, sizeof(pi));

  char *cmdline = zan_win_quote_cmdline(exe, args, argc);
  if (!cmdline) {
    CloseHandle(hRead);
    CloseHandle(hWrite);
    return -1;
  }
  int wlen = MultiByteToWideChar(CP_UTF8, 0, cmdline, -1, NULL, 0);
  wchar_t *wcmd = (wchar_t *)malloc(wlen * sizeof(wchar_t));
  if (!wcmd) {
    free(cmdline);
    CloseHandle(hRead);
    CloseHandle(hWrite);
    return -1;
  }
  MultiByteToWideChar(CP_UTF8, 0, cmdline, -1, wcmd, wlen);
  free(cmdline);

  BOOL ok = CreateProcessW(NULL, wcmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL,
                           NULL, &si, &pi);
  free(wcmd);
  CloseHandle(hWrite);

  if (!ok) {
    CloseHandle(hRead);
    return -1;
  }

  size_t cap = 4096;
  size_t len = 0;
  char *buf = (char *)malloc(cap);
  if (!buf) {
    CloseHandle(hRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return -1;
  }

  DWORD bytesRead = 0;
  while (ReadFile(hRead, buf + len, (DWORD)(cap - len - 1), &bytesRead, NULL) &&
         bytesRead > 0) {
    len += bytesRead;
    if (len + 1024 >= cap) {
      cap *= 2;
      char *nb = (char *)realloc(buf, cap);
      if (!nb)
        break;
      buf = nb;
    }
  }
  CloseHandle(hRead);
  buf[len] = '\0';

  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  *out_buf = buf;
  *out_len = (int32_t)len;
  *exit_code = (int32_t)code;
  return 0;
#else
  return zan_proc_capture_safe_timeout(exe, args, argc, out_buf, out_len,
                                       exit_code, -1);
#endif
}

void zan_proc_free_buf(char *buf) {
  if (buf)
    free(buf);
}
