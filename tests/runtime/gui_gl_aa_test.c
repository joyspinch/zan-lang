/* 底层系统交互与数据协议契约 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
static HMODULE lib;
#define SYMBOL(n) GetProcAddress(lib, n)
#else
#include <dlfcn.h>
static void *lib;
#define SYMBOL(n) dlsym(lib, n)
#endif
#define W 96
#define H 80
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "GL AA: %s (line %d)\n", #c, __LINE__); exit(1); } } while (0)
static int32_t (*create)(int32_t,int32_t);
static int32_t (*destroy)(int32_t);
static int32_t (*backend)(int32_t);
static const char *(*backend_name)(void);
static void *(*pixels)(int32_t);
static void (*clear)(int32_t,int32_t);
static void (*circle)(int32_t,int32_t,int32_t,int32_t,int32_t);
static void (*ring)(int32_t,int32_t,int32_t,int32_t,int32_t,int32_t);
static void (*sector)(int32_t,int32_t,int32_t,int32_t,int32_t,int32_t,int32_t,int32_t);
static void (*line)(int32_t,int32_t,int32_t,int32_t,int32_t,int32_t,int32_t);
static void (*poly)(int32_t,const int32_t *,int32_t,int32_t,int32_t);
static void (*polyfx)(int32_t,const int32_t *,int32_t,int32_t,int32_t);
static void (*clip)(int32_t,int32_t,int32_t,int32_t,int32_t);
static void (*unclip)(int32_t);
static void (*surface_round)(int32_t,int32_t,int32_t,int32_t,int32_t,int32_t,int32_t,int32_t,int32_t);
#define LOAD(v,n) do { *(void **)(&(v)) = (void *)SYMBOL(n); CHECK(v); } while (0)
static uint32_t reference[W*H], actual[W*H];
static int failures;
static void capture(int s, uint32_t *out) { const void *p = pixels(s); CHECK(p); memcpy(out,p,sizeof(reference)); }
static void compare(const char *label, int tolerance) {
    int worst=0, index=0;
    for (int i=0;i<W*H;i++) for(int sh=0;sh<32;sh+=8) {
        int d=(int)((reference[i]>>sh)&255)-(int)((actual[i]>>sh)&255);
        if(d<0)d=-d;
        if(d>worst) { worst=d; index=i; }
    }
    if(worst>tolerance) { fprintf(stderr,"%s: maximum channel error %d > %d at %d,%d CPU=%08x GPU=%08x\n",label,worst,tolerance,index%W,index/W,reference[index],actual[index]); failures++; }
}
static void shape(int s,int kind) {
    if(kind==0) circle(s,45,37,19,0xFFFFFFFFu);
    if(kind==1) ring(s,45,37,19,0xFFFFFFFFu,3);
    if(kind==2) sector(s,45,37,9,23,30,290,0xFFFFFFFFu);
    if(kind==3) line(s,12,17,78,54,0xFFFFFFFFu,5);
}
static const int32_t path[]={10,20,45,31,46,14,45,31,79,56,19,54,45,31};
static void paths(int s,int color,int fixed) {
    int32_t p[sizeof(path)/sizeof(path[0])];
    for(unsigned i=0;i<sizeof(p)/sizeof(p[0]);i++) p[i]=path[i]*256+64;
    clip(s,7,9,76,57);
    if(fixed) polyfx(s,p,7,color,3); else poly(s,path,7,color,3);
    unclip(s);
}
int main(int argc,char **argv) {
    CHECK(argc==2);
#ifdef _WIN32
    lib=LoadLibraryA(argv[1]);
#else
    lib=dlopen(argv[1],RTLD_NOW);
#endif
    CHECK(lib);
    LOAD(create,"zan_gui_create_surface"); LOAD(destroy,"zan_gui_destroy_surface");
    LOAD(backend,"zan_gui_set_render_backend"); LOAD(backend_name,"zan_gui_render_backend");
    LOAD(pixels,"zan_gui_get_pixels"); LOAD(clear,"zan_gui_clear");
    LOAD(circle,"zan_gui_fill_circle"); LOAD(ring,"zan_gui_draw_circle");
    LOAD(sector,"zan_gui_fill_sector"); LOAD(line,"zan_gui_draw_line");
    LOAD(poly,"zan_gui_draw_polyline"); LOAD(polyfx,"zan_gui_draw_polyline_fx");
    LOAD(surface_round,"zan_gui_surface_rounded_rect");
    LOAD(clip,"zan_gui_push_clip"); LOAD(unclip,"zan_gui_pop_clip");
    if(!backend(1) || strcmp(backend_name(),"gl")) { fprintf(stderr,"SKIP: real GL 3.3 unavailable\n"); return 77; }
    int s=create(W,H); CHECK(s>=0);
    for(int kind=0;kind<4;kind++) {
        backend(0); clear(s,0xFF17293Bu); shape(s,kind); capture(s,reference);
        CHECK(backend(1)); clear(s,0xFF17293Bu); shape(s,kind); capture(s,actual);
        const char *names[]={"fill circle","ring","sector","thick line"};
        compare(names[kind],3);
    }
    for(int fixed=0;fixed<2;fixed++) for(int alpha=0;alpha<2;alpha++) {
        int color=alpha ? (int)0x8061D3A7u : (int)0xFF61D3A7u;
        backend(0); clear(s,0x00203040); paths(s,color,fixed); capture(s,reference);
        CHECK(backend(1)); clear(s,0x00203040); paths(s,color,fixed); capture(s,actual);
        compare("clipped max-union path",3);
    }
    /* 底层系统交互与数据协议契约 */
    const int32_t once[]={12,19,78,53};
    const int32_t retraced[]={12,19,78,53,12,19,78,53};
    for(int alpha=0;alpha<2;alpha++) {
        int color=alpha ? (int)0x8044CCFFu : (int)0xFF44CCFFu;
        clear(s,0xFF192B3Du); poly(s,once,2,color,3); capture(s,reference);
        clear(s,0xFF192B3Du); poly(s,retraced,4,color,3); capture(s,actual);
        compare("retrace idempotence",0);
    }
    /* 底层系统交互与数据协议契约 */
    backend(0); clear(s,0xFF192B3Du);
    poly(s,retraced,4,0x8044CCFFu,3); paths(s,0xFFCB7744u,1); shape(s,0);
    capture(s,reference);
    CHECK(backend(1)); clear(s,0xFF192B3Du);
    poly(s,retraced,4,0x8044CCFFu,3); paths(s,0xFFCB7744u,1); shape(s,0);
    capture(s,actual); compare("painter order and blend restoration",4);
    /* 底层系统交互与数据协议契约 */
    for(int pass=0;pass<2;pass++) {
        if(pass) CHECK(backend(1)); else backend(0);
        clear(s,0xFF192B3Du);
        for(int j=0;j<12;j++) {
            int x=3+(j%4)*22, y=3+(j/4)*24;
            int32_t tiny[]={x,y,x+7,y+5,x,y};
            clip(s,x,y,8,7); poly(s,tiny,3,0x80FFCC44u,3); unclip(s);
        }
        capture(s,pass?actual:reference);
    }
    compare("bounded scratch clear",3);
    /* 底层系统交互与数据协议契约 */
    int other=create(W,H); CHECK(other>=0);
    clear(other,0xFF192B3Du); poly(other,once,2,0xFFFFFFFFu,3); capture(other,reference);
    destroy(s); s=create(W,H); CHECK(s>=0);
    clear(s,0xFF192B3Du); poly(s,once,2,0xFFFFFFFFu,3); capture(s,actual);
    compare("surface lifecycle",0);
    const int32_t degenerate[]={35,35,35,35,35,35};
    clear(s,0xFF192B3Du); capture(s,reference);
    poly(s,degenerate,3,0xFFFFFFFFu,7); capture(s,actual);
    compare("degenerate path",0);
    const int widths[]={0,1,4,24,80};
    const uint32_t fills[]={0xFF44AA88u,0x8044AA88u,0};
    const uint32_t borders[]={0xFFDD6633u,0x80DD6633u,0};
    for(int w=0;w<5;w++) for(int f=0;f<3;f++) for(int b=0;b<3;b++) {
        backend(0); clear(s,0x00203040);
        surface_round(s,12,9,63,47,13,fills[f],borders[b],widths[w]); capture(s,reference);
        CHECK(backend(1)); clear(s,0x00203040);
        surface_round(s,12,9,63,47,13,fills[f],borders[b],widths[w]); capture(s,actual);
        compare("combined rounded material",4);
    }
    destroy(other); destroy(s); backend(0);
    if (failures) return 1;
    puts("GL AA regression passed");
    return 0;
}
