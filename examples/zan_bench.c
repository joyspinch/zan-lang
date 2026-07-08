#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#pragma comment(lib, "ws2_32.lib")

/* ==================== TCP ECHO SERVER + BENCHMARK ==================== */
static volatile LONG g_tcp_running = 0;
static SOCKET g_tcp_server = INVALID_SOCKET;

DWORD WINAPI tcp_echo_thread(LPVOID param) {
    SOCKET client = (SOCKET)(uintptr_t)param;
    char buf[4096];
    int nodelay = 1;
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
    while (g_tcp_running) {
        int n = recv(client, buf, sizeof(buf), 0);
        if (n <= 0) break;
        int sent = 0;
        while (sent < n) {
            int r = send(client, buf + sent, n - sent, 0);
            if (r <= 0) goto done;
            sent += r;
        }
    }
done:
    closesocket(client);
    return 0;
}

__declspec(dllexport) int tcp_echo_server_start(int port) {
    g_tcp_running = 1;
    g_tcp_server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_tcp_server == INVALID_SOCKET) return -1;
    int reuse = 1;
    setsockopt(g_tcp_server, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((u_short)port);
    if (bind(g_tcp_server, (struct sockaddr*)&addr, sizeof(addr)) < 0) return -2;
    if (listen(g_tcp_server, 512) < 0) return -3;
    return 0;
}

DWORD WINAPI tcp_accept_loop(LPVOID param) {
    while (g_tcp_running) {
        SOCKET client = accept(g_tcp_server, NULL, NULL);
        if (client == INVALID_SOCKET) continue;
        CreateThread(NULL, 0, tcp_echo_thread, (LPVOID)(uintptr_t)client, 0, NULL);
    }
    return 0;
}

__declspec(dllexport) void tcp_echo_server_run(void) {
    CreateThread(NULL, 0, tcp_accept_loop, NULL, 0, NULL);
}

__declspec(dllexport) void tcp_echo_server_stop(void) {
    g_tcp_running = 0;
    if (g_tcp_server != INVALID_SOCKET) { closesocket(g_tcp_server); g_tcp_server = INVALID_SOCKET; }
}

typedef struct {
    int port; int msg_size; int msg_count; int thread_id;
    long long bytes_sent; long long msgs_done; long long elapsed_us;
} tcp_bench_ctx;

DWORD WINAPI tcp_bench_thread(LPVOID param) {
    tcp_bench_ctx* ctx = (tcp_bench_ctx*)param;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET; addr.sin_port = htons((u_short)ctx->port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    int nodelay = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) { closesocket(s); return 1; }
    char* sbuf = (char*)malloc(ctx->msg_size);
    char* rbuf = (char*)malloc(ctx->msg_size);
    memset(sbuf, 65, ctx->msg_size);
    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    for (int i = 0; i < ctx->msg_count; i++) {
        int sent = 0;
        while (sent < ctx->msg_size) { int r = send(s, sbuf+sent, ctx->msg_size-sent, 0); if(r<=0) goto done; sent+=r; }
        int recvd = 0;
        while (recvd < ctx->msg_size) { int r = recv(s, rbuf+recvd, ctx->msg_size-recvd, 0); if(r<=0) goto done; recvd+=r; }
        ctx->msgs_done++;
        ctx->bytes_sent += ctx->msg_size * 2;
    }
done:
    QueryPerformanceCounter(&end);
    ctx->elapsed_us = (end.QuadPart - start.QuadPart) * 1000000 / freq.QuadPart;
    closesocket(s); free(sbuf); free(rbuf);
    return 0;
}

__declspec(dllexport) void tcp_bench_run(int port, int msg_size, int msg_count, int threads,
    long long* out_total_msgs, long long* out_total_bytes, long long* out_elapsed_us) {
    tcp_bench_ctx* ctxs = (tcp_bench_ctx*)calloc(threads, sizeof(tcp_bench_ctx));
    HANDLE* handles = (HANDLE*)malloc(threads * sizeof(HANDLE));
    for (int i = 0; i < threads; i++) {
        ctxs[i].port = port; ctxs[i].msg_size = msg_size;
        ctxs[i].msg_count = msg_count / threads; ctxs[i].thread_id = i;
        handles[i] = CreateThread(NULL, 0, tcp_bench_thread, &ctxs[i], 0, NULL);
    }
    WaitForMultipleObjects(threads, handles, TRUE, INFINITE);
    long long total_msgs = 0, total_bytes = 0, max_us = 0;
    for (int i = 0; i < threads; i++) {
        total_msgs += ctxs[i].msgs_done; total_bytes += ctxs[i].bytes_sent;
        if (ctxs[i].elapsed_us > max_us) max_us = ctxs[i].elapsed_us;
        CloseHandle(handles[i]);
    }
    *out_total_msgs = total_msgs; *out_total_bytes = total_bytes; *out_elapsed_us = max_us;
    free(ctxs); free(handles);
}

/* ==================== UDP ECHO SERVER + BENCHMARK ==================== */
static volatile LONG g_udp_running = 0;
static SOCKET g_udp_server = INVALID_SOCKET;

DWORD WINAPI udp_echo_loop(LPVOID param) {
    char buf[65536];
    struct sockaddr_in from; int fromlen;
    while (g_udp_running) {
        fromlen = sizeof(from);
        int n = recvfrom(g_udp_server, buf, sizeof(buf), 0, (struct sockaddr*)&from, &fromlen);
        if (n <= 0) continue;
        sendto(g_udp_server, buf, n, 0, (struct sockaddr*)&from, fromlen);
    }
    return 0;
}

__declspec(dllexport) int udp_echo_server_start(int port) {
    g_udp_running = 1;
    g_udp_server = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_udp_server == INVALID_SOCKET) return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons((u_short)port);
    if (bind(g_udp_server, (struct sockaddr*)&addr, sizeof(addr)) < 0) return -2;
    return 0;
}

__declspec(dllexport) void udp_echo_server_run(void) {
    CreateThread(NULL, 0, udp_echo_loop, NULL, 0, NULL);
}

__declspec(dllexport) void udp_echo_server_stop(void) {
    g_udp_running = 0;
    if (g_udp_server != INVALID_SOCKET) { closesocket(g_udp_server); g_udp_server = INVALID_SOCKET; }
}

__declspec(dllexport) void udp_bench_run(int port, int msg_size, int msg_count,
    long long* out_total_msgs, long long* out_total_bytes, long long* out_elapsed_us) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET; addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    char* sbuf = (char*)malloc(msg_size);
    char* rbuf = (char*)malloc(msg_size);
    memset(sbuf, 66, msg_size);
    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&start);
    long long msgs_done = 0;
    struct sockaddr_in from; int fromlen;
    for (int i = 0; i < msg_count; i++) {
        sendto(s, sbuf, msg_size, 0, (struct sockaddr*)&addr, sizeof(addr));
        fromlen = sizeof(from);
        int n = recvfrom(s, rbuf, msg_size, 0, (struct sockaddr*)&from, &fromlen);
        if (n > 0) msgs_done++;
    }
    QueryPerformanceCounter(&end);
    *out_total_msgs = msgs_done;
    *out_total_bytes = msgs_done * msg_size * 2;
    *out_elapsed_us = (end.QuadPart - start.QuadPart) * 1000000 / freq.QuadPart;
    closesocket(s); free(sbuf); free(rbuf);
}

/* ==================== HTTP CLIENT BENCHMARK ==================== */
typedef struct {
    int port; int request_count; int thread_id;
    long long reqs_done; long long bytes_recv; long long elapsed_us;
} http_bench_ctx;

static const char* g_http_req = "GET /api/hello HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: keep-alive\r\n\r\n";

DWORD WINAPI http_client_thread(LPVOID param) {
    http_bench_ctx* ctx = (http_bench_ctx*)param;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET; addr.sin_port = htons((u_short)ctx->port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    int nodelay = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) { closesocket(s); return 1; }
    int reqlen = (int)strlen(g_http_req);
    char rbuf[4096];
    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&start);
    for (int i = 0; i < ctx->request_count; i++) {
        if (send(s, g_http_req, reqlen, 0) <= 0) break;
        int n = recv(s, rbuf, sizeof(rbuf), 0);
        if (n <= 0) break;
        ctx->reqs_done++; ctx->bytes_recv += n;
    }
    QueryPerformanceCounter(&end);
    ctx->elapsed_us = (end.QuadPart - start.QuadPart) * 1000000 / freq.QuadPart;
    closesocket(s);
    return 0;
}

__declspec(dllexport) void http_client_bench_run(int port, int total_reqs, int threads,
    long long* out_reqs, long long* out_bytes, long long* out_elapsed_us) {
    http_bench_ctx* ctxs = (http_bench_ctx*)calloc(threads, sizeof(http_bench_ctx));
    HANDLE* handles = (HANDLE*)malloc(threads * sizeof(HANDLE));
    for (int i = 0; i < threads; i++) {
        ctxs[i].port = port; ctxs[i].request_count = total_reqs / threads; ctxs[i].thread_id = i;
        handles[i] = CreateThread(NULL, 0, http_client_thread, &ctxs[i], 0, NULL);
    }
    WaitForMultipleObjects(threads, handles, TRUE, INFINITE);
    long long total = 0, bytes = 0, max_us = 0;
    for (int i = 0; i < threads; i++) {
        total += ctxs[i].reqs_done; bytes += ctxs[i].bytes_recv;
        if (ctxs[i].elapsed_us > max_us) max_us = ctxs[i].elapsed_us;
        CloseHandle(handles[i]);
    }
    *out_reqs = total; *out_bytes = bytes; *out_elapsed_us = max_us;
    free(ctxs); free(handles);
}

/* ==================== WEBSOCKET ECHO SERVER + BENCHMARK ==================== */
#include <wchar.h>

static volatile LONG g_ws_running = 0;
static SOCKET g_ws_server = INVALID_SOCKET;

/* Minimal SHA-1 for WebSocket handshake */
static void sha1_block(unsigned int* h, const unsigned char* block) {
    unsigned int w[80], a,b,c,d,e,f,k,temp;
    int i;
    for(i=0;i<16;i++) w[i]=(block[i*4]<<24)|(block[i*4+1]<<16)|(block[i*4+2]<<8)|block[i*4+3];
    for(i=16;i<80;i++){temp=w[i-3]^w[i-8]^w[i-14]^w[i-16]; w[i]=(temp<<1)|(temp>>31);}
    a=h[0];b=h[1];c=h[2];d=h[3];e=h[4];
    for(i=0;i<80;i++){
        if(i<20){f=(b&c)|((~b)&d);k=0x5A827999;}
        else if(i<40){f=b^c^d;k=0x6ED9EBA1;}
        else if(i<60){f=(b&c)|(b&d)|(c&d);k=0x8F1BBCDC;}
        else{f=b^c^d;k=0xCA62C1D6;}
        temp=((a<<5)|(a>>27))+f+e+k+w[i]; e=d;d=c;c=(b<<30)|(b>>2);b=a;a=temp;
    }
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;
}

static void sha1(const unsigned char* data, int len, unsigned char out[20]) {
    unsigned int h[5]={0x67452301,0xEFCDAB89,0x98BADCFE,0x10325476,0xC3D2E1F0};
    unsigned char block[64]; int i, blocks=(len+9+63)/64;
    for(i=0;i<blocks;i++){
        memset(block,0,64);
        int off=i*64, cplen=len-off; if(cplen>64) cplen=64; if(cplen>0) memcpy(block,data+off,cplen);
        if(i==blocks-1||off+64>len){ if(cplen>=0 && off+cplen==len) block[cplen]=0x80;
            if(i==blocks-1){long long bits=(long long)len*8; for(int j=0;j<8;j++) block[56+j]=(unsigned char)(bits>>(56-j*8));}
        }
        sha1_block(h, block);
    }
    for(i=0;i<5;i++){out[i*4]=(h[i]>>24)&0xff;out[i*4+1]=(h[i]>>16)&0xff;out[i*4+2]=(h[i]>>8)&0xff;out[i*4+3]=h[i]&0xff;}
}

static const char b64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void base64_encode(const unsigned char* in, int len, char* out) {
    int i,j=0;
    for(i=0;i<len;i+=3){
        unsigned int v=(in[i]<<16)|((i+1<len?in[i+1]:0)<<8)|(i+2<len?in[i+2]:0);
        out[j++]=b64[(v>>18)&63]; out[j++]=b64[(v>>12)&63];
        out[j++]=(i+1<len)?b64[(v>>6)&63]:61; out[j++]=(i+2<len)?b64[v&63]:61;
    }
    out[j]=0;
}

static int ws_handshake(SOCKET s) {
    char buf[4096]; int n=recv(s,buf,sizeof(buf)-1,0); if(n<=0) return -1; buf[n]=0;
    char* key_start=strstr(buf,"Sec-WebSocket-Key: ");
    if(!key_start) return -1;
    key_start+=19; char* key_end=strstr(key_start,"\r\n"); if(!key_end) return -1;
    char combined[256]; int klen=(int)(key_end-key_start);
    memcpy(combined,key_start,klen);
    strcpy(combined+klen,"258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    unsigned char hash[20]; sha1((unsigned char*)combined,(int)strlen(combined),hash);
    char accept[32]; base64_encode(hash,20,accept);
    char resp[512]; int rlen=snprintf(resp,sizeof(resp),
        "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",accept);
    send(s,resp,rlen,0);
    return 0;
}

static int ws_recv_frame(SOCKET s, char* payload, int maxlen) {
    unsigned char hdr[2]; if(recv(s,(char*)hdr,2,0)!=2) return -1;
    int masked=hdr[1]&0x80; int len=hdr[1]&0x7F;
    if(len==126){unsigned char ext[2]; recv(s,(char*)ext,2,0); len=(ext[0]<<8)|ext[1];}
    else if(len==127){unsigned char ext[8]; recv(s,(char*)ext,8,0); len=(int)((ext[4]<<24)|(ext[5]<<16)|(ext[6]<<8)|ext[7]);}
    unsigned char mask[4]={0};
    if(masked) recv(s,(char*)mask,4,0);
    if(len>maxlen) return -1;
    int total=0;
    while(total<len){int r=recv(s,payload+total,len-total,0); if(r<=0) return -1; total+=r;}
    if(masked) for(int i=0;i<len;i++) payload[i]^=mask[i%4];
    return len;
}

static int ws_send_frame(SOCKET s, const char* data, int len) {
    unsigned char hdr[10]; int hlen=2;
    hdr[0]=0x82; /* binary, fin */
    if(len<126){hdr[1]=(unsigned char)len;}
    else if(len<65536){hdr[1]=126;hdr[2]=(len>>8)&0xff;hdr[3]=len&0xff;hlen=4;}
    else{hdr[1]=127;memset(hdr+2,0,4);hdr[6]=(len>>24)&0xff;hdr[7]=(len>>16)&0xff;hdr[8]=(len>>8)&0xff;hdr[9]=len&0xff;hlen=10;}
    send(s,(char*)hdr,hlen,0);
    return send(s,data,len,0);
}

DWORD WINAPI ws_echo_thread(LPVOID param) {
    SOCKET client=(SOCKET)(uintptr_t)param;
    if(ws_handshake(client)<0){closesocket(client);return 1;}
    char buf[65536];
    while(g_ws_running){
        int n=ws_recv_frame(client,buf,sizeof(buf)); if(n<0) break;
        ws_send_frame(client,buf,n);
    }
    closesocket(client); return 0;
}

__declspec(dllexport) int ws_echo_server_start(int port) {
    g_ws_running=1;
    g_ws_server=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    int reuse=1; setsockopt(g_ws_server,SOL_SOCKET,SO_REUSEADDR,(const char*)&reuse,sizeof(reuse));
    struct sockaddr_in addr={0}; addr.sin_family=AF_INET; addr.sin_addr.s_addr=INADDR_ANY; addr.sin_port=htons((u_short)port);
    if(bind(g_ws_server,(struct sockaddr*)&addr,sizeof(addr))<0) return -1;
    listen(g_ws_server,512); return 0;
}

DWORD WINAPI ws_accept_loop(LPVOID param) {
    while(g_ws_running){
        SOCKET c=accept(g_ws_server,NULL,NULL); if(c==INVALID_SOCKET) continue;
        CreateThread(NULL,0,ws_echo_thread,(LPVOID)(uintptr_t)c,0,NULL);
    }
    return 0;
}

__declspec(dllexport) void ws_echo_server_run(void){CreateThread(NULL,0,ws_accept_loop,NULL,0,NULL);}
__declspec(dllexport) void ws_echo_server_stop(void){g_ws_running=0; if(g_ws_server!=INVALID_SOCKET){closesocket(g_ws_server);g_ws_server=INVALID_SOCKET;}}

__declspec(dllexport) void ws_bench_run(int port, int msg_size, int msg_count, int threads,
    long long* out_msgs, long long* out_bytes, long long* out_elapsed_us) {
    typedef struct { int port,msg_size,msg_count; long long msgs_done,bytes,elapsed_us; } wctx;
    DWORD WINAPI ws_bench_thread_fn(LPVOID p) {
        /* Nested function not allowed in GCC - will flatten */
        return 0;
    }
    /* Single-threaded WS bench for simplicity */
    SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    struct sockaddr_in addr={0}; addr.sin_family=AF_INET; addr.sin_port=htons((u_short)port); addr.sin_addr.s_addr=inet_addr("127.0.0.1");
    int nodelay=1; setsockopt(s,IPPROTO_TCP,TCP_NODELAY,(const char*)&nodelay,sizeof(nodelay));
    if(connect(s,(struct sockaddr*)&addr,sizeof(addr))<0){closesocket(s);*out_msgs=0;*out_bytes=0;*out_elapsed_us=0;return;}
    /* Send WS upgrade */
    const char* upgrade="GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
    send(s,upgrade,(int)strlen(upgrade),0);
    char hbuf[1024]; recv(s,hbuf,sizeof(hbuf),0); /* read handshake response */
    char* sbuf=(char*)malloc(msg_size); memset(sbuf,67,msg_size);
    char* rbuf=(char*)malloc(msg_size+16);
    LARGE_INTEGER freq,start,end; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&start);
    long long msgs=0;
    for(int i=0;i<msg_count;i++){
        /* Send masked frame */
        unsigned char fhdr[14]; int fhlen=6;
        fhdr[0]=0x82; /* binary fin */
        if(msg_size<126){fhdr[1]=0x80|(unsigned char)msg_size; fhlen=6;}
        else if(msg_size<65536){fhdr[1]=0x80|126;fhdr[2]=(msg_size>>8)&0xff;fhdr[3]=msg_size&0xff;fhlen=8;}
        else{fhdr[1]=0x80|127;memset(fhdr+2,0,4);fhdr[6]=(msg_size>>24)&0xff;fhdr[7]=(msg_size>>16)&0xff;fhdr[8]=(msg_size>>8)&0xff;fhdr[9]=msg_size&0xff;fhlen=14;}
        /* mask key = 0 for simplicity */
        memset(fhdr+fhlen-4,0,4);
        send(s,(char*)fhdr,fhlen,0);
        send(s,sbuf,msg_size,0);
        /* Receive echo frame */
        int n=ws_recv_frame(s,rbuf,msg_size+16);
        if(n>0) msgs++;
    }
    QueryPerformanceCounter(&end);
    *out_msgs=msgs; *out_bytes=msgs*msg_size*2;
    *out_elapsed_us=(end.QuadPart-start.QuadPart)*1000000/freq.QuadPart;
    closesocket(s); free(sbuf); free(rbuf);
}

/* ==================== MQTT BENCHMARK (connect to broker) ==================== */
__declspec(dllexport) void mqtt_bench_run(const char* broker_ip, int broker_port,
    int msg_count, int msg_size,
    long long* out_msgs_pub, long long* out_elapsed_us) {
    SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    struct sockaddr_in addr={0}; addr.sin_family=AF_INET; addr.sin_port=htons((u_short)broker_port);
    addr.sin_addr.s_addr=inet_addr(broker_ip);
    if(connect(s,(struct sockaddr*)&addr,sizeof(addr))<0){closesocket(s);*out_msgs_pub=0;*out_elapsed_us=0;return;}
    /* MQTT CONNECT */
    unsigned char conn[]={0x10,0x12,0x00,0x04,0x4D,0x51,0x54,0x54,0x04,0x02,0x00,0x3C,0x00,0x06,0x7A,0x61,0x6E,0x5F,0x62,0x6E};
    send(s,(char*)conn,sizeof(conn),0);
    char rbuf[256]; recv(s,rbuf,sizeof(rbuf),0); /* CONNACK */
    /* SUBSCRIBE to test/echo */
    unsigned char sub[]={0x82,0x0D,0x00,0x01,0x00,0x08,0x74,0x65,0x73,0x74,0x2F,0x7A,0x61,0x6E,0x00};
    send(s,(char*)sub,sizeof(sub),0);
    recv(s,rbuf,sizeof(rbuf),0); /* SUBACK */
    char* payload=(char*)malloc(msg_size); memset(payload,68,msg_size);
    /* Build PUBLISH packet template: topic="test/zan" */
    int topic_len=8; /* test/zan */
    int pub_remain=2+topic_len+msg_size;
    int pub_hdr_len=2;
    if(pub_remain>127){pub_hdr_len=3; if(pub_remain>16383) pub_hdr_len=4;}
    unsigned char* pub_pkt=(unsigned char*)malloc(pub_hdr_len+pub_remain);
    pub_pkt[0]=0x30; /* PUBLISH, QoS 0 */
    if(pub_hdr_len==2) pub_pkt[1]=(unsigned char)pub_remain;
    else if(pub_hdr_len==3){pub_pkt[1]=(pub_remain&0x7F)|0x80;pub_pkt[2]=(pub_remain>>7)&0x7F;}
    else{pub_pkt[1]=(pub_remain&0x7F)|0x80;pub_pkt[2]=((pub_remain>>7)&0x7F)|0x80;pub_pkt[3]=(pub_remain>>14)&0x7F;}
    int off=pub_hdr_len;
    pub_pkt[off]=0; pub_pkt[off+1]=(unsigned char)topic_len; off+=2;
    memcpy(pub_pkt+off,"test/zan",topic_len); off+=topic_len;
    memcpy(pub_pkt+off,payload,msg_size);
    int total_pkt_len=pub_hdr_len+pub_remain;
    LARGE_INTEGER freq,start,end; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&start);
    long long published=0;
    for(int i=0;i<msg_count;i++){
        if(send(s,(char*)pub_pkt,total_pkt_len,0)>0) published++;
        else break;
    }
    QueryPerformanceCounter(&end);
    *out_msgs_pub=published;
    *out_elapsed_us=(end.QuadPart-start.QuadPart)*1000000/freq.QuadPart;
    /* DISCONNECT */
    unsigned char disc[]={0xE0,0x00}; send(s,(char*)disc,2,0);
    closesocket(s); free(payload); free(pub_pkt);
}
