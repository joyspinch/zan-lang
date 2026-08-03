#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601
#endif
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "src/runtime/rt_sync.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
typedef HANDLE thread_t;
#else
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
typedef pthread_t thread_t;
#endif

static int checks;
static int failures;

#define CHECK(cond, ...) do {                                                \
    checks++;                                                                \
    if (!(cond)) {                                                           \
        failures++;                                                          \
        fprintf(stderr, "FAIL: ");                                           \
        fprintf(stderr, __VA_ARGS__);                                        \
        fprintf(stderr, "\n");                                               \
    }                                                                        \
} while (0)

typedef struct {
    int64_t atomic;
    int iterations;
} atomic_worker_args;

#ifdef _WIN32
static DWORD WINAPI atomic_worker(LPVOID arg) {
#else
static void *atomic_worker(void *arg) {
#endif
    atomic_worker_args *args = (atomic_worker_args *)arg;
    for (int i = 0; i < args->iterations; i++) {
        zan_atomic_int_add(args->atomic, 1);
    }
    return 0;
}

static void test_atomic_int(void) {
    int64_t atomic = zan_atomic_int_create(0);
    CHECK(atomic != 0, "could not create atomic integer");
    if (!atomic) return;

    enum { THREAD_COUNT = 4, ITERATIONS = 10000 };
    atomic_worker_args args = {atomic, ITERATIONS};
    thread_t threads[THREAD_COUNT];
    memset(threads, 0, sizeof(threads));
    for (int i = 0; i < THREAD_COUNT; i++) {
#ifdef _WIN32
        threads[i] = CreateThread(NULL, 0, atomic_worker, &args, 0, NULL);
#else
        pthread_create(&threads[i], NULL, atomic_worker, &args);
#endif
    }
    for (int i = 0; i < THREAD_COUNT; i++) {
#ifdef _WIN32
        if (threads[i]) {
            WaitForSingleObject(threads[i], INFINITE);
            CloseHandle(threads[i]);
        }
#else
        pthread_join(threads[i], NULL);
#endif
    }

    CHECK(zan_atomic_int_load(atomic) == THREAD_COUNT * ITERATIONS,
          "atomic add lost updates");
    CHECK(zan_atomic_int_exchange(atomic, 7) == THREAD_COUNT * ITERATIONS,
          "atomic exchange returned wrong previous value");
    CHECK(zan_atomic_int_compare_exchange(atomic, 7, 11) == 7,
          "successful compare-exchange returned wrong previous value");
    CHECK(zan_atomic_int_load(atomic) == 11,
          "successful compare-exchange did not update value");
    CHECK(zan_atomic_int_compare_exchange(atomic, 7, 13) == 11,
          "failed compare-exchange returned wrong observed value");
    zan_atomic_int_store(atomic, 23);
    CHECK(zan_atomic_int_load(atomic) == 23, "atomic store failed");
    zan_atomic_int_destroy(atomic);
}

static int shared_table_child(const char *name) {
    int64_t table = zan_shared_table_open(name);
    if (!table) return 2;
    for (int i = 0; i < 1000; i++) {
        zan_shared_table_increment(table, "workers", "count", 1);
    }
    zan_shared_table_set_float(table, "workers", "load", 2.5);
    zan_shared_table_set_string(table, "workers", "label", "child");
    zan_shared_table_close(table);
    return 0;
}

static void test_shared_table(void) {
    char name[96];
#ifdef _WIN32
    snprintf(name, sizeof(name), "rt_sync_%lu_%lld",
             (unsigned long)GetCurrentProcessId(), (long long)time(NULL));
#else
    snprintf(name, sizeof(name), "rt_sync_%ld_%lld",
             (long)getpid(), (long long)time(NULL));
#endif
    int64_t table = zan_shared_table_create(
        name, 64, 32,
        "i:count;i:window_start;i:owner;f:load;s:32:label;");
    CHECK(table != 0, "could not create shared table");
    if (!table) return;

    CHECK(zan_shared_table_set_int(table, "workers", "count", 0) == 1,
          "could not set integer column");
    CHECK(zan_shared_table_set_float(table, "workers", "load", 1.25) == 1,
          "could not set float column");
    CHECK(zan_shared_table_set_string(table, "workers", "label", "parent") == 1,
          "could not set string column");
    CHECK(zan_shared_table_exists(table, "workers") == 1,
          "inserted row is missing");
    CHECK(zan_shared_table_count(table) == 1, "shared table count is wrong");

    int child_status = -1;
#ifdef _WIN32
    char executable[MAX_PATH];
    char command[512];
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    memset(&startup, 0, sizeof(startup));
    memset(&process, 0, sizeof(process));
    startup.cb = sizeof(startup);
    GetModuleFileNameA(NULL, executable, MAX_PATH);
    snprintf(command, sizeof(command), "\"%s\" --shared-child %s", executable, name);
    if (CreateProcessA(
            NULL, command, NULL, NULL, FALSE, 0, NULL, NULL,
            &startup, &process)) {
        for (int i = 0; i < 1000; i++) {
            zan_shared_table_increment(table, "workers", "count", 1);
        }
        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD exit_code = 1;
        GetExitCodeProcess(process.hProcess, &exit_code);
        child_status = (int)exit_code;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
#else
    pid_t child = fork();
    if (child == 0) {
        _exit(shared_table_child(name));
    }
    if (child > 0) {
        for (int i = 0; i < 1000; i++) {
            zan_shared_table_increment(table, "workers", "count", 1);
        }
        int status = 0;
        if (waitpid(child, &status, 0) == child && WIFEXITED(status)) {
            child_status = WEXITSTATUS(status);
        }
    }
#endif

    CHECK(child_status == 0, "shared-table child failed: %d", child_status);
    CHECK(zan_shared_table_get_int(table, "workers", "count") == 2000,
          "cross-process increments were not atomic");
    CHECK(zan_shared_table_get_float(table, "workers", "load") == 2.5,
          "cross-process float update was not visible");
    CHECK(strcmp(
              zan_shared_table_get_string(table, "workers", "label"),
              "child") == 0,
          "cross-process string update was not visible");

    int64_t expiry_base = INT64_C(4102444800000);
    CHECK(zan_shared_table_set_int(table, "ttl-a", "count", 1) == 1,
          "could not create first expiring row");
    CHECK(zan_shared_table_set_int(table, "ttl-b", "count", 2) == 1,
          "could not create second expiring row");
    CHECK(zan_shared_table_expire_at(table, "ttl-a", expiry_base + 2000) == 1,
          "could not set first expiration");
    CHECK(zan_shared_table_expire_at(table, "ttl-b", expiry_base + 1000) == 1,
          "could not set earlier expiration");
    CHECK(zan_shared_table_expires_at(table, "ttl-a") == expiry_base + 2000,
          "expiration timestamp was not retained");
    CHECK(zan_shared_table_purge_expired(table, expiry_base + 999) == 0,
          "row expired before its deadline");
    CHECK(zan_shared_table_purge_expired(table, expiry_base + 1000) == 1,
          "heap did not remove its earliest row");
    CHECK(zan_shared_table_exists(table, "ttl-b") == 0,
          "expired row remains visible");
    CHECK(zan_shared_table_exists(table, "ttl-a") == 1,
          "later row was removed with heap root");
    CHECK(zan_shared_table_expire_at(table, "ttl-a", 0) == 1,
          "persist could not remove row from expiration heap");
    CHECK(zan_shared_table_purge_expired(table, expiry_base + 3000) == 0,
          "persisted row was removed");
    CHECK(zan_shared_table_expires_at(table, "ttl-a") == 0,
          "persisted row still has an expiration");
    CHECK(zan_shared_table_set_int(table, "ttl-c", "count", 3) == 1,
          "expired tombstone slot was not reusable");

    CHECK(zan_shared_table_rate_allow(
              table, "rate", expiry_base + 10000, 1000, 2) == 1,
          "rate limiter rejected first request");
    CHECK(zan_shared_table_rate_allow(
              table, "rate", expiry_base + 10500, 1000, 2) == 1,
          "rate limiter rejected request within limit");
    CHECK(zan_shared_table_rate_allow(
              table, "rate", expiry_base + 10999, 1000, 2) == 0,
          "rate limiter allowed request over limit");
    CHECK(zan_shared_table_rate_allow(
              table, "rate", expiry_base + 11000, 1000, 2) == 1,
          "rate limiter did not reset at window boundary");

    CHECK(zan_shared_table_lock_acquire(
              table, "lease", 11, expiry_base + 20000, 1000) == 1,
          "lease lock could not be acquired");
    CHECK(zan_shared_table_lock_acquire(
              table, "lease", 22, expiry_base + 20500, 1000) == 0,
          "lease lock admitted a second owner");
    CHECK(zan_shared_table_lock_release(table, "lease", 22) == 0,
          "lease lock accepted wrong owner release");
    CHECK(zan_shared_table_lock_release(table, "lease", 11) == 1,
          "lease lock rejected owner release");
    CHECK(zan_shared_table_lock_acquire(
              table, "lease", 22, expiry_base + 21000, 1000) == 1,
          "released lease lock could not be reacquired");
    CHECK(zan_shared_table_lock_acquire(
              table, "lease", 33, expiry_base + 22000, 1000) == 1,
          "expired lease lock was not reclaimable");

    int64_t second = zan_shared_table_open(name);
    CHECK(second != 0, "could not open second table handle");
    if (second) {
        CHECK(zan_shared_table_increment(second, "workers", "count", -1) == 1999,
              "second handle could not update shared row");
        zan_shared_table_close(second);
    }
    CHECK(zan_shared_table_delete(table, "workers") == 1,
          "could not delete shared row");
    CHECK(zan_shared_table_delete(table, "ttl-a") == 1,
          "could not delete persisted ttl row");
    CHECK(zan_shared_table_delete(table, "ttl-c") == 1,
          "could not delete replacement ttl row");
    CHECK(zan_shared_table_exists(table, "rate") == 0,
          "expired rate-limit row was not reclaimed");
    CHECK(zan_shared_table_lock_release(table, "lease", 33) == 1,
          "could not release final lease row");
    CHECK(zan_shared_table_count(table) == 0, "delete did not update count");
    CHECK(zan_shared_table_destroy(table) == 1, "could not destroy shared table");
    CHECK(zan_shared_table_open(name) == 0,
          "destroyed shared table can still be opened");
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--shared-child") == 0) {
        return shared_table_child(argv[2]);
    }

    test_atomic_int();
    test_shared_table();
    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
