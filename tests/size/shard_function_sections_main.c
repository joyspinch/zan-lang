/* No MinGW headers are needed: the bundled sysroot contains link libraries. */
#ifdef SHARD_GC_DUPLICATE
int ShardGc_Live(int value) { return value + 21; }
#else
extern int ShardGc_Live(int value);
#endif
extern int puts(const char *text);

int main(void) {
    volatile int value = 21;
    if (ShardGc_Live(value) != 42) return 1;
    return puts("42") < 0 ? 2 : 0;
}
