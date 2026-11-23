/* Apple --fast-alloc: ld64 has no --wrap, so Zan code calls zan_mem_* by
 * name while C runtime objects and drivers keep libc malloc. Every pointer
 * that crosses that line must still be released by the allocator that owns
 * it: zan_mem_free forwards foreign blocks to libc, and the registered
 * "zan_mem" malloc zone routes libc free/realloc/malloc_size of a Zan block
 * back to rt_mem.c. */
#include <malloc/malloc.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

void *zan_mem_malloc(size_t n);
void zan_mem_free(void *p);
void *zan_mem_calloc(size_t n, size_t m);
void *zan_mem_realloc(void *p, size_t n);
size_t zan_mem_slabs(void);

static int failures;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } \
} while (0)

static void *thread_free(void *p) {
    free(p);                      /* libc free, other thread, Zan block */
    return NULL;
}

int main(void) {
    unsigned char *z = zan_mem_malloc(40);
    CHECK(z != NULL, "zan_mem_malloc");
    CHECK(zan_mem_slabs() > 0, "small block comes from a slab");
    malloc_zone_t *zone = malloc_zone_from_ptr(z);
    CHECK(zone && zone->zone_name && strcmp(zone->zone_name, "zan_mem") == 0,
          "libmalloc attributes Zan blocks to the zan_mem zone");
    CHECK(malloc_size(z) == 48, "malloc_size reports the size class");
    for (int i = 0; i < 40; i++) z[i] = (unsigned char)i;

    /* C code growing a buffer Zan handed it. */
    unsigned char *g = realloc(z, 6000);
    CHECK(g != NULL, "libc realloc of a Zan block");
    int same = 1;
    for (int i = 0; i < 40; i++) same &= g[i] == (unsigned char)i;
    CHECK(same, "realloc keeps the payload");
    CHECK(malloc_zone_from_ptr(g) != zone, "large realloc lands in libc");
    zan_mem_free(g);              /* Zan freeing what is now a libc block */

    /* C code freeing a Zan block, then Zan reusing the slot. */
    void *a = zan_mem_calloc(3, 8);
    free(a);
    void *b = zan_mem_malloc(24);
    CHECK(b == a, "libc free returned the block to the Zan free list");
    zan_mem_free(b);

    /* Zan freeing / growing libc-owned buffers (strdup, driver strings). */
    char *s = strdup("driver string");
    zan_mem_free(s);
    char *r = malloc(32);
    r = zan_mem_realloc(r, 64);
    CHECK(r != NULL && malloc_zone_from_ptr(r) != zone,
          "zan_mem_realloc forwards foreign blocks to libc");
    free(r);

    /* Cross-thread libc free of a Zan block goes to the owner's remote list. */
    void *t = zan_mem_malloc(100);
    pthread_t th;
    CHECK(pthread_create(&th, NULL, thread_free, t) == 0, "pthread_create");
    pthread_join(th, NULL);

    /* libmalloc force_lock/unlock/reinit our lock around fork(). */
    pid_t pid = fork();
    if (pid == 0) {
        void *c = zan_mem_malloc(64);
        free(c);
        _exit(c ? 0 : 1);
    }
    int st = 0;
    CHECK(pid > 0 && waitpid(pid, &st, 0) == pid && WIFEXITED(st) &&
          WEXITSTATUS(st) == 0, "allocate/free in a forked child");

    if (failures) return 1;
    puts("rt_mem apple zone: ok");
    return 0;
}
