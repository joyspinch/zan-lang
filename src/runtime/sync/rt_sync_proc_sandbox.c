/* Linux strict process sandbox. Included by rt_sync_proc.c. */
#if defined(__linux__) && defined(__has_include)
#if __has_include(<linux/landlock.h>)
#include <linux/landlock.h>
#endif
#endif

#if defined(LANDLOCK_ACCESS_FS_EXECUTE) && defined(__linux__) &&               \
    defined(__NR_landlock_create_ruleset) && defined(__NR_close_range) &&      \
    (defined(__x86_64__) || defined(__aarch64__))
#define ZAN_PROC_SANDBOX_LINUX 1
#include <linux/audit.h>
#include <linux/capability.h>
#include <linux/filter.h>
#include <linux/magic.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/vfs.h>

/* ABI 6 adds signal scoping. Declare the ABI layout locally to support
 * building against distribution headers older than the running kernel. */
#define ZAN_LANDLOCK_ABI 6
#define ZAN_LANDLOCK_TRUNCATE (UINT64_C(1) << 14)
#define ZAN_LANDLOCK_IOCTL_DEV (UINT64_C(1) << 15)
#define ZAN_LANDLOCK_SCOPE_SIGNAL (UINT64_C(1) << 1)

typedef struct {
  int dir;
  int join;
  int kill;
  int events;
  char *path;
} zan_proc_sandbox_group;

static int sandbox_write(int fd, const char *text, size_t size) {
  ssize_t n;
  do {
    n = write(fd, text, size);
  } while (n < 0 && errno == EINTR);
  return n == (ssize_t)size ? 0 : -1;
}

static int sandbox_controller(int dir, const char *key, const char *value) {
  int fd = openat(dir, key, O_WRONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;
  int rc = sandbox_write(fd, value, strlen(value));
  close(fd);
  return rc;
}

static bool sandbox_group_empty(zan_proc_sandbox_group *group) {
  char events[256];
  ssize_t n;
  do {
    n = pread(group->events, events, sizeof(events) - 1, 0);
  } while (n < 0 && errno == EINTR);
  if (n <= 0)
    return false;
  events[n] = '\0';
  return strstr(events, "populated 0\n") != NULL;
}

static bool sandbox_group_remove(zan_proc_sandbox_group *group) {
  return rmdir(group->path) == 0 || errno == ENOENT;
}

static void sandbox_group_free(zan_proc_sandbox_group *group) {
  zan_proc_close_fd(&group->join);
  zan_proc_close_fd(&group->kill);
  zan_proc_close_fd(&group->events);
  zan_proc_close_fd(&group->dir);
  free(group->path);
  free(group);
}

static bool sandbox_cgroup_root(const char *root) {
  struct statfs fs;
  return root && root[0] == '/' && statfs(root, &fs) == 0 &&
         fs.f_type == CGROUP2_SUPER_MAGIC && access(root, W_OK | X_OK) == 0;
}

static zan_proc_sandbox_group *sandbox_group_create(const char *root) {
  if (!sandbox_cgroup_root(root))
    return NULL;
  static const char suffix[] = "/zan-mcp-XXXXXX";
  size_t len = strlen(root);
  zan_proc_sandbox_group *group = malloc(sizeof(*group));
  if (!group)
    return NULL;
  *group =
      (zan_proc_sandbox_group){.dir = -1, .join = -1, .kill = -1, .events = -1};
  group->path = malloc(len + sizeof(suffix));
  if (!group->path) {
    free(group);
    return NULL;
  }
  memcpy(group->path, root, len);
  memcpy(group->path + len, suffix, sizeof(suffix));
  if (!mkdtemp(group->path)) {
    free(group->path);
    free(group);
    return NULL;
  }
  group->dir = open(group->path, O_DIRECTORY | O_RDONLY | O_CLOEXEC);
  if (group->dir < 0 ||
      sandbox_controller(group->dir, "cpu.max", "100000 100000") != 0 ||
      sandbox_controller(group->dir, "memory.max", "1073741824") != 0 ||
      sandbox_controller(group->dir, "memory.swap.max", "0") != 0 ||
      sandbox_controller(group->dir, "pids.max", "64") != 0)
    goto failed;
  group->kill = openat(group->dir, "cgroup.kill", O_WRONLY | O_CLOEXEC);
  group->events = openat(group->dir, "cgroup.events", O_RDONLY | O_CLOEXEC);
  group->join = openat(group->dir, "cgroup.procs", O_WRONLY | O_CLOEXEC);
  if (group->kill < 0 || group->events < 0 || group->join < 0)
    goto failed;
  return group;
failed:
  sandbox_group_remove(group);
  sandbox_group_free(group);
  return NULL;
}

static int sandbox_path_rule(int ruleset, const char *path, uint64_t access) {
  int fd = open(path, O_PATH | O_CLOEXEC);
  if (fd < 0)
    return -1;
  struct stat st;
  int rc = fstat(fd, &st);
  if (rc == 0) {
    if (!S_ISDIR(st.st_mode)) {
      access &= LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_READ_FILE |
                LANDLOCK_ACCESS_FS_WRITE_FILE | ZAN_LANDLOCK_TRUNCATE |
                ZAN_LANDLOCK_IOCTL_DEV;
    }
    struct landlock_path_beneath_attr rule = {.parent_fd = fd,
                                              .allowed_access = access};
    rc = (int)syscall(__NR_landlock_add_rule, ruleset,
                      LANDLOCK_RULE_PATH_BENEATH, &rule, 0);
  }
  close(fd);
  return rc;
}

static int sandbox_ruleset(const char *read_roots, const char *write_root) {
  if (!read_roots || !*read_roots || !write_root || write_root[0] != '/')
    return -1;
  const uint64_t read = LANDLOCK_ACCESS_FS_EXECUTE |
                        LANDLOCK_ACCESS_FS_READ_FILE |
                        LANDLOCK_ACCESS_FS_READ_DIR;
  const uint64_t write =
      LANDLOCK_ACCESS_FS_WRITE_FILE | LANDLOCK_ACCESS_FS_REMOVE_DIR |
      LANDLOCK_ACCESS_FS_REMOVE_FILE | LANDLOCK_ACCESS_FS_MAKE_DIR |
      LANDLOCK_ACCESS_FS_MAKE_REG | LANDLOCK_ACCESS_FS_MAKE_SYM |
      LANDLOCK_ACCESS_FS_MAKE_FIFO | ZAN_LANDLOCK_TRUNCATE;
  const struct {
    uint64_t handled_access_fs, handled_access_net, scoped;
  } policy = {read | write | LANDLOCK_ACCESS_FS_MAKE_CHAR |
                  LANDLOCK_ACCESS_FS_MAKE_BLOCK | LANDLOCK_ACCESS_FS_MAKE_SOCK |
                  ZAN_LANDLOCK_IOCTL_DEV,
              0, ZAN_LANDLOCK_SCOPE_SIGNAL};
  int ruleset =
      (int)syscall(__NR_landlock_create_ruleset, &policy, sizeof(policy), 0);
  if (ruleset < 0)
    return -1;
  char *roots = strdup(read_roots);
  if (!roots) {
    close(ruleset);
    return -1;
  }
  char *remaining = roots;
  char *path;
  while ((path = strsep(&remaining, "\n")) != NULL) {
    if (path[0] != '/' || sandbox_path_rule(ruleset, path, read) < 0)
      goto failed;
  }
  if (sandbox_path_rule(ruleset, write_root, read | write) < 0 ||
      sandbox_path_rule(ruleset, "/dev/null",
                        LANDLOCK_ACCESS_FS_READ_FILE |
                            LANDLOCK_ACCESS_FS_WRITE_FILE) < 0)
    goto failed;
  free(roots);
  return ruleset;
failed:
  free(roots);
  close(ruleset);
  return -1;
}

static int sandbox_filter(void) {
  static struct sock_filter filter[] = {
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
#if defined(__x86_64__)
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 1, 0),
#else
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_AARCH64, 1, 0),
#endif
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
#if defined(__x86_64__)
      BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 0x40000000, 0, 1),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
#endif
#define ZAN_SANDBOX_DENY(number, error)                                        \
  BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, number, 0, 1),                           \
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | error),
      ZAN_SANDBOX_DENY(__NR_socket, EPERM) ZAN_SANDBOX_DENY(
          __NR_socketpair, EPERM) ZAN_SANDBOX_DENY(__NR_ptrace, EPERM)
          ZAN_SANDBOX_DENY(__NR_process_vm_readv, EPERM)
              ZAN_SANDBOX_DENY(__NR_process_vm_writev, EPERM)
                  ZAN_SANDBOX_DENY(__NR_unshare, EPERM)
                      ZAN_SANDBOX_DENY(__NR_setns, EPERM)
#ifdef __NR_pidfd_getfd
                          ZAN_SANDBOX_DENY(__NR_pidfd_getfd, EPERM)
#endif
#ifdef __NR_io_uring_setup
                              ZAN_SANDBOX_DENY(__NR_io_uring_setup, EPERM)
#endif
#ifdef __NR_clone3
                                  ZAN_SANDBOX_DENY(__NR_clone3, ENOSYS)
#endif
#undef ZAN_SANDBOX_DENY
                                      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                               __NR_clone, 0, 3),
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
               offsetof(struct seccomp_data, args[0])),
      BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0x7e020080, 0, 1), /* CLONE_NEW* */
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)};
  struct sock_fprog program = {
      (unsigned short)(sizeof(filter) / sizeof(filter[0])), filter};
  return prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program);
}

static bool sandbox_unprivileged(void) {
  struct __user_cap_header_struct header = {.version =
                                                _LINUX_CAPABILITY_VERSION_3};
  struct __user_cap_data_struct cap[2];
  return getuid() != 0 && geteuid() != 0 &&
         syscall(__NR_capget, &header, cap) == 0 && !cap[0].permitted &&
         !cap[1].permitted;
}

static int sandbox_enter(zan_proc_sandbox_group *group, int ruleset,
                         int launch_fd) {
  if (sandbox_write(group->join, "0", 1) != 0 ||
      prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0 ||
      syscall(__NR_landlock_restrict_self, ruleset, 0) != 0)
    return -1;
  if ((launch_fd > 3 &&
       syscall(__NR_close_range, 3U, (unsigned)launch_fd - 1, 0) != 0) ||
      syscall(__NR_close_range, (unsigned)launch_fd + 1, ~0U, 0) != 0)
    return -1;
  return sandbox_filter();
}
#endif
