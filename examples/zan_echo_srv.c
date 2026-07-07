#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#pragma comment(lib, "ws2_32.lib")

/* High-res timer */
__declspec(dllexport) long long get_tick_us(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return c.QuadPart * 1000000 / f.QuadPart;
}

/* ===== TCP ECHO SERVER ===== */
static volatile LONG g_tcp_run = 0;
static SOCKET g_tcp_srv = INVALID_SOCKET;

DWORD WINAPI tcp_worker(LPVOID p) {
    SOCKET c = (SOCKET)(uintptr_t)p;
    char buf[8192];
    int nodelay = 1;
    setsockopt(c, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
    while (g_tcp_run) {
        int n = recv(c, buf, sizeof(buf), 0);
        if (n <= 0) break;
        int s = 0;
        while (s < n) { int r = send(c, buf+s, n-s, 0); if (r <= 0) goto out; s += r; }
    }
out: closesocket(c); return 0;
}

DWORD WINAPI tcp_acceptor(LPVOID p) {
    while (g_tcp_run) {
        SOCKET c = accept(g_tcp_srv, NULL, NULL);
        if (c == INVALID_SOCKET) continue;
        CreateThread(NULL, 0, tcp_worker, (LPVOID)(uintptr_t)c, 0, NULL);
    }
    return 0;
}

__declspec(dllexport) int echo_tcp_start(int port) {
    g_tcp_run = 1;
    g_tcp_srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int reuse = 1; setsockopt(g_tcp_srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_addr.s_addr = INADDR_ANY; a.sin_port = htons((u_short)port);
    if (bind(g_tcp_srv, (struct sockaddr*)&a, sizeof(a)) < 0) return -1;
    listen(g_tcp_srv, 512);
    CreateThread(NULL, 0, tcp_acceptor, NULL, 0, NULL);
    return 0;
}

__declspec(dllexport) void echo_tcp_stop(void) { g_tcp_run = 0; closesocket(g_tcp_srv); }

/* ===== UDP ECHO SERVER ===== */
static volatile LONG g_udp_run = 0;
static SOCKET g_udp_srv = INVALID_SOCKET;

DWORD WINAPI udp_worker(LPVOID p) {
    char buf[65536]; struct sockaddr_in from; int flen;
    while (g_udp_run) {
        flen = sizeof(from);
        int n = recvfrom(g_udp_srv, buf, sizeof(buf), 0, (struct sockaddr*)&from, &flen);
        if (n > 0) sendto(g_udp_srv, buf, n, 0, (struct sockaddr*)&from, flen);
    }
    return 0;
}

__declspec(dllexport) int echo_udp_start(int port) {
    g_udp_run = 1;
    g_udp_srv = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_addr.s_addr = INADDR_ANY; a.sin_port = htons((u_short)port);
    if (bind(g_udp_srv, (struct sockaddr*)&a, sizeof(a)) < 0) return -1;
    CreateThread(NULL, 0, udp_worker, NULL, 0, NULL);
    return 0;
}

__declspec(dllexport) void echo_udp_stop(void) { g_udp_run = 0; closesocket(g_udp_srv); }

/* ===== WEBSOCKET ECHO SERVER ===== */
static volatile LONG g_ws_run = 0;
static SOCKET g_ws_srv = INVALID_SOCKET;

static void sha1_block(unsigned int* h, const unsigned char* blk) {
    unsigned int w[80],a,b,c,d,e,f,k,t; int i;
    for(i=0;i<16;i++) w[i]=(blk[i*4]<<24)|(blk[i*4+1]<<16)|(blk[i*4+2]<<8)|blk[i*4+3];
    for(i=16;i<80;i++){t=w[i-3]^w[i-8]^w[i-14]^w[i-16]; w[i]=(t<<1)|(t>>31);}
    a=h[0];b=h[1];c=h[2];d=h[3];e=h[4];
    for(i=0;i<80;i++){
        if(i<20){f=(b&c)|((~b)&d);k=0x5A827999;}
        else if(i<40){f=b^c^d;k=0x6ED9EBA1;}
        else if(i<60){f=(b&c)|(b&d)|(c&d);k=0x8F1BBCDC;}
        else{f=b^c^d;k=0xCA62C1D6;}
        t=((a<<5)|(a>>27))+f+e+k+w[i]; e=d;d=c;c=(b<<30)|(b>>2);b=a;a=t;
    }
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;
}

static void sha1(const unsigned char* data, int len, unsigned char out[20]) {
    unsigned int h[5]={0x67452301,0xEFCDAB89,0x98BADCFE,0x10325476,0xC3D2E1F0};
    unsigned char blk[64]; int blocks=(len+9+63)/64;
    for(int i=0;i<blocks;i++){
        memset(blk,0,64); int off=i*64,cp=len-off; if(cp>64)cp=64; if(cp>0)memcpy(blk,data+off,cp);
        if(off+64>len||i==blocks-1){if(cp>=0&&off+cp==len)blk[cp]=0x80;
            if(i==blocks-1){long long bits=(long long)len*8; for(int j=0;j<8;j++)blk[56+j]=(unsigned char)(bits>>(56-j*8));}}
        sha1_block(h,blk);
    }
    for(int i=0;i<5;i++){out[i*4]=(h[i]>>24)&0xff;out[i*4+1]=(h[i]>>16)&0xff;out[i*4+2]=(h[i]>>8)&0xff;out[i*4+3]=h[i]&0xff;}
}

static const char b64c[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void b64enc(const unsigned char* in, int len, char* out) {
    int i,j=0;
    for(i=0;i<len;i+=3){
        unsigned int v=(in[i]<<16)|((i+1<len?in[i+1]:0)<<8)|(i+2<len?in[i+2]:0);
        out[j++]=b64c[(v>>18)&63]; out[j++]=b64c[(v>>12)&63];
        out[j++]=(i+1<len)?b64c[(v>>6)&63]:61; out[j++]=(i+2<len)?b64c[v&63]:61;
    }
    out[j]=0;
}

static int ws_handshake(SOCKET s) {
    char buf[4096]; int n=recv(s,buf,sizeof(buf)-1,0); if(n<=0) return -1; buf[n]=0;
    char* ks=strstr(buf,"Sec-WebSocket-Key: "); if(!ks) return -1;
    ks+=19; char* ke=strstr(ks,"\r\n"); if(!ke) return -1;
    char comb[256]; int kl=(int)(ke-ks); memcpy(comb,ks,kl);
    strcpy(comb+kl,"258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    unsigned char hash[20]; sha1((unsigned char*)comb,(int)strlen(comb),hash);
    char acc[32]; b64enc(hash,20,acc);
    char resp[512]; int rl=snprintf(resp,sizeof(resp),"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",acc);
    send(s,resp,rl,0); return 0;
}

static int ws_recv(SOCKET s, char* out, int maxlen) {
    unsigned char hdr[2]; int r=recv(s,(char*)hdr,2,0); if(r!=2) return -1;
    int masked=hdr[1]&0x80, len=hdr[1]&0x7F;
    if(len==126){unsigned char e[2]; recv(s,(char*)e,2,0); len=(e[0]<<8)|e[1];}
    else if(len==127){unsigned char e[8]; recv(s,(char*)e,8,0); len=(int)((e[4]<<24)|(e[5]<<16)|(e[6]<<8)|e[7]);}
    unsigned char mask[4]={0}; if(masked) recv(s,(char*)mask,4,0);
    if(len>maxlen) return -1;
    int total=0; while(total<len){r=recv(s,out+total,len-total,0); if(r<=0) return -1; total+=r;}
    if(masked) for(int i=0;i<len;i++) out[i]^=mask[i%4];
    return len;
}

static int ws_send(SOCKET s, const char* data, int len) {
    unsigned char hdr[10]; int hl=2;
    hdr[0]=0x82;
    if(len<126) hdr[1]=(unsigned char)len;
    else if(len<65536){hdr[1]=126;hdr[2]=(len>>8)&0xff;hdr[3]=len&0xff;hl=4;}
    else{hdr[1]=127;memset(hdr+2,0,4);hdr[6]=(len>>24)&0xff;hdr[7]=(len>>16)&0xff;hdr[8]=(len>>8)&0xff;hdr[9]=len&0xff;hl=10;}
    send(s,(char*)hdr,hl,0);
    return send(s,data,len,0);
}

DWORD WINAPI ws_worker(LPVOID p) {
    SOCKET c=(SOCKET)(uintptr_t)p;
    if(ws_handshake(c)<0){closesocket(c);return 1;}
    char buf[65536];
    while(g_ws_run){int n=ws_recv(c,buf,sizeof(buf)); if(n<0) break; ws_send(c,buf,n);}
    closesocket(c); return 0;
}

DWORD WINAPI ws_acceptor(LPVOID p) {
    while(g_ws_run){SOCKET c=accept(g_ws_srv,NULL,NULL); if(c==INVALID_SOCKET) continue; CreateThread(NULL,0,ws_worker,(LPVOID)(uintptr_t)c,0,NULL);}
    return 0;
}

__declspec(dllexport) int echo_ws_start(int port) {
    g_ws_run=1; g_ws_srv=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    int reuse=1; setsockopt(g_ws_srv,SOL_SOCKET,SO_REUSEADDR,(const char*)&reuse,sizeof(reuse));
    struct sockaddr_in a={0}; a.sin_family=AF_INET; a.sin_addr.s_addr=INADDR_ANY; a.sin_port=htons((u_short)port);
    if(bind(g_ws_srv,(struct sockaddr*)&a,sizeof(a))<0) return -1;
    listen(g_ws_srv,512); CreateThread(NULL,0,ws_acceptor,NULL,0,NULL); return 0;
}

__declspec(dllexport) void echo_ws_stop(void){g_ws_run=0; closesocket(g_ws_srv);}

/* ===== MQTT ECHO BROKER (minimal, QoS0 only) ===== */
static volatile LONG g_mq_run = 0;
static SOCKET g_mq_srv = INVALID_SOCKET;

DWORD WINAPI mqtt_worker(LPVOID p) {
    SOCKET c=(SOCKET)(uintptr_t)p;
    char buf[65536];
    while(g_mq_run) {
        int n=recv(c,buf,sizeof(buf),0); if(n<=0) break;
        unsigned char type=(unsigned char)buf[0]>>4;
        if(type==1){/* CONNECT -> CONNACK */ unsigned char ack[]={0x20,0x02,0x00,0x00}; send(c,(char*)ack,4,0);}
        else if(type==8){/* SUBSCRIBE -> SUBACK */ unsigned char sa[]={0x90,0x03,buf[2],buf[3],0x00}; send(c,(char*)sa,5,0);}
        else if(type==3){/* PUBLISH -> echo back as PUBLISH */ send(c,buf,n,0);}
        else if(type==12){/* PINGREQ -> PINGRESP */ unsigned char pr[]={0xD0,0x00}; send(c,(char*)pr,2,0);}
        else if(type==14){/* DISCONNECT */ break;}
    }
    closesocket(c); return 0;
}

DWORD WINAPI mqtt_acceptor(LPVOID p) {
    while(g_mq_run){SOCKET c=accept(g_mq_srv,NULL,NULL); if(c==INVALID_SOCKET) continue; CreateThread(NULL,0,mqtt_worker,(LPVOID)(uintptr_t)c,0,NULL);}
    return 0;
}

__declspec(dllexport) int echo_mqtt_start(int port) {
    g_mq_run=1; g_mq_srv=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    int reuse=1; setsockopt(g_mq_srv,SOL_SOCKET,SO_REUSEADDR,(const char*)&reuse,sizeof(reuse));
    struct sockaddr_in a={0}; a.sin_family=AF_INET; a.sin_addr.s_addr=INADDR_ANY; a.sin_port=htons((u_short)port);
    if(bind(g_mq_srv,(struct sockaddr*)&a,sizeof(a))<0) return -1;
    listen(g_mq_srv,512); CreateThread(NULL,0,mqtt_acceptor,NULL,0,NULL); return 0;
}

__declspec(dllexport) void echo_mqtt_stop(void){g_mq_run=0; closesocket(g_mq_srv);}
