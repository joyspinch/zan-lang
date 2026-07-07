#include <stdio.h>
#include <windows.h>

typedef long long (*callback_fn)(long long a, long long b);

typedef struct { callback_fn fn; long long a, b; long long result; HANDLE evt; } ctx_t;

static DWORD WINAPI thread_fn(LPVOID p) {
    ctx_t* c = (ctx_t*)p;
    printf("Thread: calling callback with (%lld, %lld)\n", c->a, c->b);
    fflush(stdout);
    c->result = c->fn(c->a, c->b);
    printf("Thread: callback returned %lld\n", c->result);
    fflush(stdout);
    SetEvent(c->evt);
    return 0;
}

__declspec(dllexport) long long test_callback(callback_fn fn, long long a, long long b) {
    printf("C: calling callback with (%lld, %lld)\n", a, b);
    fflush(stdout);
    long long result = fn(a, b);
    printf("C: callback returned %lld\n", result);
    fflush(stdout);
    return result;
}

__declspec(dllexport) long long test_callback_in_thread(callback_fn fn, long long a, long long b) {
    ctx_t ctx = { fn, a, b, 0, CreateEvent(NULL, TRUE, FALSE, NULL) };
    CreateThread(NULL, 0, thread_fn, &ctx, 0, NULL);
    WaitForSingleObject(ctx.evt, INFINITE);
    CloseHandle(ctx.evt);
    return ctx.result;
}
