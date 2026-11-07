/* rt_sync_proc.c: 跨平台安全子进程创建、执行与输出捕获实现 */

#ifdef _WIN32
static void zan_win_buf_append(char *buf, size_t cap, size_t *len, const char *s) {
    if (!s) return;
    size_t slen = strlen(s);
    if (*len + slen >= cap) return;
    memcpy(buf + *len, s, slen);
    *len += slen;
    buf[*len] = '\0';
}

static void zan_win_append_arg(char *buf, size_t cap, size_t *len, const char *a) {
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
            for (int b = 0; b < bs * 2 + 1; b++) zan_win_buf_append(buf, cap, len, "\\");
            zan_win_buf_append(buf, cap, len, "\"");
            bs = 0;
        } else {
            for (int b = 0; b < bs; b++) zan_win_buf_append(buf, cap, len, "\\");
            bs = 0;
            char ch[2] = { *p, '\0' };
            zan_win_buf_append(buf, cap, len, ch);
        }
    }
    for (int b = 0; b < bs * 2; b++) zan_win_buf_append(buf, cap, len, "\\");
    zan_win_buf_append(buf, cap, len, "\"");
}

static char *zan_win_quote_cmdline(const char *exe, const char **args, int32_t argc) {
    size_t cap = 256;
    for (int32_t i = 0; i < argc; i++) {
        if (args[i]) cap += strlen(args[i]) * 2 + 8;
    }
    if (exe) cap += strlen(exe) * 2 + 8;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
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

int32_t zan_proc_run_safe(const char *exe, const char **args, int32_t argc) {
    if (!exe) return -1;
#ifdef _WIN32
    char *cmdline = zan_win_quote_cmdline(exe, args, argc);
    if (!cmdline) return -1;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, cmdline, -1, NULL, 0);
    if (wlen <= 0) { free(cmdline); return -1; }
    wchar_t *wcmd = (wchar_t *)malloc(wlen * sizeof(wchar_t));
    if (!wcmd) { free(cmdline); return -1; }
    MultiByteToWideChar(CP_UTF8, 0, cmdline, -1, wcmd, wlen);
    free(cmdline);

    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    BOOL ok = CreateProcessW(NULL, wcmd, NULL, NULL, FALSE,
                             CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(wcmd);
    if (!ok) return -1;

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int32_t)code;
#else
    char **argv = (char **)malloc(((size_t)argc + 2) * sizeof(char *));
    if (!argv) return -1;
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
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status)) return (int32_t)WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + (int32_t)WTERMSIG(status);
    return -1;
#endif
}

#ifndef _WIN32
#if !defined(__linux__)
static pthread_mutex_t g_proc_pipe_guard = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t g_proc_pipe_once = PTHREAD_ONCE_INIT;
static int g_proc_pipe_guard_error;
static void zan_proc_pipe_fork_prepare(void) { pthread_mutex_lock(&g_proc_pipe_guard); }
static void zan_proc_pipe_fork_done(void) { pthread_mutex_unlock(&g_proc_pipe_guard); }
static void zan_proc_pipe_guard_init(void) {
    g_proc_pipe_guard_error = pthread_atfork(zan_proc_pipe_fork_prepare,
                                            zan_proc_pipe_fork_done,
                                            zan_proc_pipe_fork_done);
}
#endif

static int zan_proc_launch_pipe(int fds[2]) {
#if defined(__linux__)
    if (pipe2(fds, O_CLOEXEC) < 0) return -1;
#else
    pthread_once(&g_proc_pipe_once, zan_proc_pipe_guard_init);
    if (g_proc_pipe_guard_error) return -1;
    pthread_mutex_lock(&g_proc_pipe_guard);
    if (pipe(fds) < 0) {
        pthread_mutex_unlock(&g_proc_pipe_guard);
        return -1;
    }
    if (fcntl(fds[0], F_SETFD, FD_CLOEXEC) < 0 ||
        fcntl(fds[1], F_SETFD, FD_CLOEXEC) < 0) goto failed;
#endif
    for (int i = 0; i < 2; i++) {
        if (fds[i] >= 3) continue;
        int moved = fcntl(fds[i], F_DUPFD_CLOEXEC, 3);
        if (moved < 0) goto failed;
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
#endif

int32_t zan_proc_start_detached_safe(const char *exe, const char **args, int32_t argc) {
    if (!exe) return -1;
#ifdef _WIN32
    char *cmdline = zan_win_quote_cmdline(exe, args, argc);
    if (!cmdline) return -1;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, cmdline, -1, NULL, 0);
    if (wlen <= 0) { free(cmdline); return -1; }
    wchar_t *wcmd = (wchar_t *)malloc(wlen * sizeof(wchar_t));
    if (!wcmd) { free(cmdline); return -1; }
    MultiByteToWideChar(CP_UTF8, 0, cmdline, -1, wcmd, wlen);
    free(cmdline);

    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    BOOL ok = CreateProcessW(NULL, wcmd, NULL, NULL, FALSE,
                             CREATE_NO_WINDOW | DETACHED_PROCESS, NULL, NULL, &si, &pi);
    free(wcmd);
    if (!ok) return -1;

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
#else
    char **argv = (char **)malloc(((size_t)argc + 2) * sizeof(char *));
    if (!argv) return -1;
    argv[0] = (char *)exe;
    for (int32_t i = 0; i < argc; i++) {
        argv[i + 1] = (char *)args[i];
    }
    argv[argc + 1] = NULL;

    int launch_pipe[2];
    if (zan_proc_launch_pipe(launch_pipe) < 0) { free(argv); return -1; }
    pid_t pid = fork();
    if (pid < 0) {
        close(launch_pipe[0]);
        close(launch_pipe[1]);
        free(argv);
        return -1;
    }
    if (pid == 0) {
        close(launch_pipe[0]);
        /* 双重 fork 隔离守护子进程 */
        if (setsid() < 0) goto launch_failed;
        pid_t gc = fork();
        if (gc < 0) goto launch_failed;
        if (gc == 0) {
            int devnull = open("/dev/null", O_RDWR);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                if (devnull > STDERR_FILENO) close(devnull);
            }
            execvp(exe, argv);
            goto launch_failed;
        }
        close(launch_pipe[1]);
        _exit(0);
launch_failed: {
            char failed = 1;
            while (write(launch_pipe[1], &failed, 1) < 0 && errno == EINTR) {}
            _exit(127);
        }
    }
    free(argv);
    close(launch_pipe[1]);
    char failed;
    ssize_t launch_result;
    do { launch_result = read(launch_pipe[0], &failed, 1); }
    while (launch_result < 0 && errno == EINTR);
    close(launch_pipe[0]);
    int st;
    while (waitpid(pid, &st, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return launch_result == 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0 ? 0 : -1;
#endif
}

int32_t zan_proc_start_program_safe(const char *exe, const char *log_path) {
    if (!exe) return -1;
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
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
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
        if (hLog != INVALID_HANDLE_VALUE) CloseHandle(hLog);
        return -1;
    }
    wchar_t *wcmd = (wchar_t *)malloc((wexe_len + 4) * sizeof(wchar_t));
    if (!wcmd) {
        if (hLog != INVALID_HANDLE_VALUE) CloseHandle(hLog);
        return -1;
    }
    wcmd[0] = L'"';
    MultiByteToWideChar(CP_UTF8, 0, exe, -1, wcmd + 1, wexe_len);
    wcmd[wexe_len] = L'"';
    wcmd[wexe_len + 1] = L'\0';

    BOOL ok = CreateProcessW(NULL, wcmd, NULL, NULL, inherit, flags, NULL, NULL, &si, &pi);
    free(wcmd);
    if (hLog != INVALID_HANDLE_VALUE) CloseHandle(hLog);
    if (!ok) return -1;

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
#else
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        if (setsid() < 0) _exit(127);
        pid_t gc = fork();
        if (gc < 0) _exit(127);
        if (gc == 0) {
            int devnull = open("/dev/null", O_RDONLY);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                if (devnull > STDIN_FILENO) close(devnull);
            }
            if (log_path && log_path[0]) {
                int outfd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (outfd >= 0) {
                    dup2(outfd, STDOUT_FILENO);
                    dup2(outfd, STDERR_FILENO);
                    if (outfd > STDERR_FILENO) close(outfd);
                }
            } else {
                int outnull = open("/dev/null", O_WRONLY);
                if (outnull >= 0) {
                    dup2(outnull, STDOUT_FILENO);
                    dup2(outnull, STDERR_FILENO);
                    if (outnull > STDERR_FILENO) close(outnull);
                }
            }
            char *argv[2];
            argv[0] = (char *)exe;
            argv[1] = NULL;
            execvp(exe, argv);
            _exit(127);
        }
        _exit(0);
    }
    /* 回收中间引导进程防止僵尸进程 */
    int st;
    while (waitpid(pid, &st, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return 0;
#endif
}

int32_t zan_proc_capture_safe(const char *exe, const char **args, int32_t argc,
                              char **out_buf, int32_t *out_len, int32_t *exit_code) {
    if (!exe || !out_buf || !out_len || !exit_code) return -1;
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
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return -1;
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

    BOOL ok = CreateProcessW(NULL, wcmd, NULL, NULL, TRUE,
                             CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
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
    while (ReadFile(hRead, buf + len, (DWORD)(cap - len - 1), &bytesRead, NULL) && bytesRead > 0) {
        len += bytesRead;
        if (len + 1024 >= cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) break;
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
    int pipefd[2];
    if (pipe(pipefd) < 0) return -1;
    fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
    fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);

    char **argv = (char **)malloc(((size_t)argc + 2) * sizeof(char *));
    if (!argv) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    argv[0] = (char *)exe;
    for (int32_t i = 0; i < argc; i++) {
        argv[i + 1] = (char *)args[i];
    }
    argv[argc + 1] = NULL;

    pid_t pid = fork();
    if (pid < 0) {
        free(argv);
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        /* Child */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execvp(exe, argv);
        _exit(127);
    }
    free(argv);

    close(pipefd[1]);
    size_t cap = 4096;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        close(pipefd[0]);
        return -1;
    }

    ssize_t r;
    while ((r = read(pipefd[0], buf + len, cap - len - 1)) > 0 ||
           (r < 0 && errno == EINTR)) {
        if (r > 0) {
            len += (size_t)r;
            if (len + 1024 >= cap) {
                cap *= 2;
                char *nb = (char *)realloc(buf, cap);
                if (!nb) break;
                buf = nb;
            }
        }
    }
    close(pipefd[0]);
    buf[len] = '\0';

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) break;
    }
    int code = -1;
    if (WIFEXITED(status)) code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) code = 128 + WTERMSIG(status);

    *out_buf = buf;
    *out_len = (int32_t)len;
    *exit_code = code;
    return 0;
#endif
}

void zan_proc_free_buf(char *buf) {
    if (buf) free(buf);
}
