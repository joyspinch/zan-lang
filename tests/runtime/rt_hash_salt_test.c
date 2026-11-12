/* POSIX deterministic nonadjacent-output regression. This TU includes
 * rt_timer.c; do not also link rt_timer.c. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static int inject_random;
static size_t injected_bytes;
static const uint64_t expected_keys[2] = {
    UINT64_C(0x0123456789abcdef), UINT64_C(0xfedcba9876543210)
};
static size_t test_fread(void *p, size_t size, size_t count, FILE *f) {
    if (!inject_random) return fread(p, size, count, f);
    size_t bytes = size * count;
    injected_bytes = bytes;
    if (bytes != sizeof(expected_keys)) return 0;
    memcpy(p, expected_keys, sizeof(expected_keys));
    return count;
}
#define fread test_fread
#include "../../src/runtime/rt_timer.c"
#undef fread

int main(void) {
    struct {
        uint64_t before, first, between[3], second, after;
    } slots;
    memset(&slots, 0xa5, sizeof(slots));
    inject_random = 1;
    zan_hash_salt_fill(&slots.first, &slots.second);
    if (injected_bytes != sizeof(expected_keys) ||
        slots.first != expected_keys[0] || slots.second != expected_keys[1] ||
        slots.before != UINT64_C(0xa5a5a5a5a5a5a5a5) ||
        slots.after != UINT64_C(0xa5a5a5a5a5a5a5a5)) return 1;
    for (size_t i = 0; i < 3; i++)
        if (slots.between[i] != UINT64_C(0xa5a5a5a5a5a5a5a5)) return 1;
    puts("PASS hash salt fills separate outputs without overwriting sentinels");
    return 0;
}
