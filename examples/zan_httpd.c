#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#pragma comment(lib, "ws2_32.lib")

static const char* g_response = NULL;
static int g_resp_len = 0;

#define MAX_ROUTES 16
static const char* g_routes[MAX_ROUTES] = {0};
static int g_route_lens[MAX_ROUTES] = {0};

static volatile long g_req_count = 0;
static volatile long g_err_count = 0;
static volatile long g_thread_count = 0;

__declspec(dllexport) void httpd_set_route(int id, const char* resp, int len) {
    if (id >= 0 && id < MAX_ROUTES) {
        g_routes[id] = resp;
        g_route_lens[id] = len;
    }
}

__declspec(dllexport) void httpd_send(long long sock, const char* data, int len) {
    send((SOCKET)sock, data, len, 0);
}

__declspec(dllexport) long httpd_get_req_count(void) { return g_req_count; }
__declspec(dllexport) long httpd_get_err_count(void) { return g_err_count; }

typedef long long (*zan_handler_fn)(long long sock, char* buf, long long len);

typedef struct {
    SOCKET client;
    zan_handler_fn handler;
} thread_ctx_t;

DWORD WINAPI client_thread_handler(LPVOID param) {
    thread_ctx_t* ctx = (thread_ctx_t*)param;
    SOCKET client = ctx->client;
    zan_handler_fn handler = ctx->handler;
    free(ctx);
    long tid = InterlockedIncrement(&g_thread_count);
    char buf[4096];
    int nodelay = 1;
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
    while (1) {
        int n = recv(client, buf, 4095, 0);
        if (n <= 0) break;
        buf[n] = 0;
        long long rc = handler((long long)client, buf, (long long)n);
        InterlockedIncrement(&g_req_count);
        if (rc == 0) {
            if (g_response) send(client, g_response, g_resp_len, 0);
        } else if (rc > 0 && rc <= MAX_ROUTES) {
            int idx = (int)(rc - 1);
            if (g_routes[idx]) send(client, g_routes[idx], g_route_lens[idx], 0);
        } else {
            InterlockedIncrement(&g_err_count);
        }
    }
    closesocket(client);
    return 0;
}

DWORD WINAPI client_thread(LPVOID param) {
    SOCKET client = (SOCKET)(uintptr_t)param;
    char buf[4096];
    int nodelay = 1;
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
    while (1) {
        int n = recv(client, buf, 4095, 0);
        if (n <= 0) break;
        send(client, g_response, g_resp_len, 0);
    }
    closesocket(client);
    return 0;
}

__declspec(dllexport) void httpd_set_response(const char* resp, int len) {
    g_response = resp;
    g_resp_len = len;
}

__declspec(dllexport) void httpd_serve(long long server_sock) {
    while (1) {
        SOCKET client = accept((SOCKET)server_sock, NULL, NULL);
        if (client == INVALID_SOCKET) continue;
        CreateThread(NULL, 0, client_thread, (LPVOID)(uintptr_t)client, 0, NULL);
    }
}

__declspec(dllexport) void httpd_serve_with_handler(long long server_sock, zan_handler_fn handler) {
    while (1) {
        SOCKET client = accept((SOCKET)server_sock, NULL, NULL);
        if (client == INVALID_SOCKET) continue;
        thread_ctx_t* ctx = (thread_ctx_t*)malloc(sizeof(thread_ctx_t));
        ctx->client = client;
        ctx->handler = handler;
        CreateThread(NULL, 0, client_thread_handler, ctx, 0, NULL);
    }
}
