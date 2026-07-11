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
        name, 64, 32, "i:count;f:load;s:32:label;");
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

    int64_t second = zan_shared_table_open(name);
    CHECK(second != 0, "could not open second table handle");
    if (second) {
        CHECK(zan_shared_table_increment(second, "workers", "count", -1) == 1999,
              "second handle could not update shared row");
        zan_shared_table_close(second);
    }
    CHECK(zan_shared_table_delete(table, "workers") == 1,
          "could not delete shared row");
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
