/* A fake native linker for the Linux cross-link CLI regression. It records
 * actual argv and fails deliberately: no target runtime or executable is used. */
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const char *path = getenv("ZAN_TEST_LINK_ARGV");
    if (!path) return 74;
    FILE *out = fopen(path, "wb");
    if (!out) return 75;
    for (int i = 1; i < argc; ++i) {
        if (fprintf(out, "%s\n", argv[i]) < 0) {
            fclose(out);
            return 76;
        }
    }
    if (fclose(out) != 0) return 77;
    return 73;
}
