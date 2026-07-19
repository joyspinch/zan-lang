/* zanpkg_main.c -- Zan Package Manager CLI.
 *
 * Commands:
 *   zanpkg init [name]          Create a new zan.pkg manifest
 *   zanpkg add <name> <source>  Add a dependency
 *   zanpkg remove <name>        Remove a dependency
 *   zanpkg build                Compile the project using manifest
 *   zanpkg publish              Build optimized release binary
 *   zanpkg info                 Show package info
 */
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define getcwd _getcwd
#define PATH_SEP "\\"
#else
#include <unistd.h>
#include <sys/stat.h>   /* mkdir() */
#include <dirent.h>     /* opendir()/readdir() */
#define PATH_SEP "/"
#endif

#include "../common/host_oom.h"
/* Inline minimal manifest handling (avoids linking full package.c + LLVM) */

static void cmd_init(int argc, char **argv) {
    const char *name = "myproject";
    if (argc > 0) name = argv[0];

    FILE *f = fopen("zan.pkg", "r");
    if (f) { fclose(f); fprintf(stderr, "error: zan.pkg already exists\n"); return; }

    f = fopen("zan.pkg", "w");
    if (!f) { fprintf(stderr, "error: cannot create zan.pkg\n"); return; }

    fprintf(f, "# Zan Package Manifest\n");
    fprintf(f, "name = \"%s\"\n", name);
    fprintf(f, "version = \"0.1.0\"\n");
    fprintf(f, "description = \"\"\n");
    fprintf(f, "author = \"\"\n");
    fprintf(f, "license = \"MIT\"\n");
    fprintf(f, "entry = \"src/Program.zan\"\n");
    fprintf(f, "\n");
    fprintf(f, "[sources]\n");
    fprintf(f, "\"src\"\n");
    fprintf(f, "\n");
    fprintf(f, "[dependencies]\n");
    fprintf(f, "# name = \"source@version\"\n");
    fclose(f);

    /* Create src directory */
#ifdef _WIN32
    _mkdir("src");
#else
    mkdir("src", 0755);
#endif

    /* Create starter Program.zan */
    FILE *fp = fopen("src" PATH_SEP "Program.zan", "r");
    if (!fp) {
        fp = fopen("src" PATH_SEP "Program.zan", "w");
        if (fp) {
            fprintf(fp, "using System;\n\n");
            fprintf(fp, "class Program {\n");
            fprintf(fp, "    static void Main() {\n");
            fprintf(fp, "        Console.WriteLine(\"Hello from %s!\");\n", name);
            fprintf(fp, "    }\n");
            fprintf(fp, "}\n");
            fclose(fp);
        }
    } else {
        fclose(fp);
    }

    printf("Created zan.pkg for '%s'\n", name);
    printf("  src/Program.zan  (entry point)\n");
    printf("\nRun 'zanpkg build' to compile.\n");
}

static void cmd_add(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: zanpkg add <name> <source[@version]>\n");
        return;
    }
    const char *dep_name = argv[0];
    const char *dep_source = argv[1];

    FILE *f = fopen("zan.pkg", "a");
    if (!f) { fprintf(stderr, "error: no zan.pkg found. Run 'zanpkg init' first.\n"); return; }
    fprintf(f, "%s = \"%s\"\n", dep_name, dep_source);
    fclose(f);
    printf("Added dependency: %s = \"%s\"\n", dep_name, dep_source);
}

static void cmd_remove(int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "usage: zanpkg remove <name>\n");
        return;
    }
    const char *dep_name = argv[0];

    FILE *f = fopen("zan.pkg", "r");
    if (!f) { fprintf(stderr, "error: no zan.pkg found.\n"); return; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)sz + 1);
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = 0;
    fclose(f);

    f = fopen("zan.pkg", "w");
    char *line = buf;
    int removed = 0;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        int len = nl ? (int)(nl - line) : (int)strlen(line);
        /* skip lines starting with dep_name = */
        char prefix[256];
        snprintf(prefix, sizeof(prefix), "%s ", dep_name);
        char prefix2[256];
        snprintf(prefix2, sizeof(prefix2), "%s=", dep_name);
        if (strncmp(line, prefix, strlen(prefix)) != 0 &&
            strncmp(line, prefix2, strlen(prefix2)) != 0) {
            fwrite(line, 1, (size_t)len, f);
            fputc('\n', f);
        } else {
            removed = 1;
        }
        line = nl ? nl + 1 : NULL;
    }
    fclose(f);
    free(buf);

    if (removed)
        printf("Removed dependency: %s\n", dep_name);
    else
        printf("Dependency '%s' not found in zan.pkg\n", dep_name);
}

/* Collect all .zan files in a directory recursively */
static int collect_zan_files(const char *dir, char files[][512], int max_files, int count) {
#ifdef _WIN32
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return count;
    do {
        if (fd.cFileName[0] == '.') continue;
        char path[512];
        snprintf(path, sizeof(path), "%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            count = collect_zan_files(path, files, max_files, count);
        } else {
            size_t len = strlen(fd.cFileName);
            if (len > 4 && strcmp(fd.cFileName + len - 4, ".zan") == 0) {
                if (count < max_files) {
                    strncpy(files[count], path, 511);
                    count++;
                }
            }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    /* POSIX: walk the directory tree directly. Building a shell "find" command
     * from `dir` would allow command injection when the source directory name
     * (read from an untrusted zan.pkg manifest) contains shell metacharacters. */
    DIR *d = opendir(dir);
    if (!d) return count;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && count < max_files) {
        if (ent->d_name[0] == '.') continue;   /* skip ., .. and dotfiles */
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            count = collect_zan_files(path, files, max_files, count);
        } else if (S_ISREG(st.st_mode)) {
            size_t len = strlen(ent->d_name);
            if (len > 4 && strcmp(ent->d_name + len - 4, ".zan") == 0) {
                strncpy(files[count], path, 511);
                files[count][511] = 0;
                count++;
            }
        }
    }
    closedir(d);
#endif
    return count;
}

static void cmd_build(int argc, char **argv) {
    (void)argc; (void)argv;
    FILE *f = fopen("zan.pkg", "r");
    if (!f) { fprintf(stderr, "error: no zan.pkg found. Run 'zanpkg init' first.\n"); return; }

    char entry[256] = "src/Program.zan";
    char name_buf[128] = "output";
    char line[1024];
    bool in_sources = false;
    char source_dirs[16][256];
    int source_dir_count = 0;

    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p && (*p == ' ' || *p == '\t')) p++;
        if (*p == '#' || *p == '\n' || *p == '\r') continue;
        if (*p == '[') {
            in_sources = (strncmp(p, "[sources]", 9) == 0);
            continue;
        }
        if (in_sources) {
            /* quoted directory name */
            char *q = strchr(p, '"');
            if (q) {
                q++;
                char *e = strchr(q, '"');
                if (e) {
                    int len = (int)(e - q);
                    if (source_dir_count < 16) {
                        strncpy(source_dirs[source_dir_count], q, (size_t)len);
                        source_dirs[source_dir_count][len] = 0;
                        source_dir_count++;
                    }
                }
            }
            continue;
        }
        /* key = "value" pairs */
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = p;
        while (*key == ' ') key++;
        char *kend = eq - 1;
        while (kend > key && *kend == ' ') kend--;
        *(kend + 1) = 0;
        char *val = eq + 1;
        while (*val == ' ') val++;
        char *vq1 = strchr(val, '"');
        if (vq1) {
            vq1++;
            char *vq2 = strchr(vq1, '"');
            if (vq2) *vq2 = 0;
            val = vq1;
        }

        if (strcmp(key, "entry") == 0) strncpy(entry, val, sizeof(entry) - 1);
        if (strcmp(key, "name") == 0) strncpy(name_buf, val, sizeof(name_buf) - 1);
    }
    fclose(f);

    /* Collect all .zan files from source dirs */
    static char files[256][512];
    int file_count = 0;
    if (source_dir_count == 0) {
        file_count = collect_zan_files("src", files, 256, 0);
    } else {
        for (int i = 0; i < source_dir_count; i++) {
            file_count = collect_zan_files(source_dirs[i], files, 256, file_count);
        }
    }

    if (file_count == 0) {
        /* just build the entry point */
        file_count = 1;
        strncpy(files[0], entry, 511);
    }

    /* Build zanc command */
    char cmd[8192];
    int pos = 0;
#ifdef _WIN32
    pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos, "zanc.exe --auto-stdlib");
#else
    pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos, "zanc --auto-stdlib");
#endif
    for (int i = 0; i < file_count; i++) {
        pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos, " \"%s\"", files[i]);
    }

    /* Check if --publish was passed */
    bool publish = false;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--publish") == 0) publish = true;
    }
    if (publish) {
        pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos, " --publish");
    }

#ifdef _WIN32
    pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos, " -o build\\%s.exe", name_buf);
    _mkdir("build");
#else
    pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos, " -o build/%s", name_buf);
    mkdir("build", 0755);
#endif

    printf("Building %s (%d files)...\n", name_buf, file_count);
    int ret = system(cmd);
    if (ret == 0) {
        printf("Build successful!\n");
    } else {
        fprintf(stderr, "Build failed (exit code %d)\n", ret);
    }
}

static void cmd_info(void) {
    FILE *f = fopen("zan.pkg", "r");
    if (!f) { fprintf(stderr, "No zan.pkg found in current directory.\n"); return; }

    char line[1024];
    printf("Package info:\n");
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r') continue;
        printf("  %s", p);
    }
    fclose(f);
}

static void print_usage(void) {
    printf("Zan Package Manager (zanpkg)\n\n");
    printf("Usage:\n");
    printf("  zanpkg init [name]              Create a new project\n");
    printf("  zanpkg add <name> <source>      Add a dependency\n");
    printf("  zanpkg remove <name>            Remove a dependency\n");
    printf("  zanpkg build [--publish]        Build the project\n");
    printf("  zanpkg info                     Show package info\n");
    printf("  zanpkg help                     Show this help\n");
}

int main(int argc, char **argv) {
    if (argc < 2) { print_usage(); return 0; }

    const char *cmd_name = argv[1];

    if (strcmp(cmd_name, "init") == 0) {
        cmd_init(argc - 2, argv + 2);
    } else if (strcmp(cmd_name, "add") == 0) {
        cmd_add(argc - 2, argv + 2);
    } else if (strcmp(cmd_name, "remove") == 0) {
        cmd_remove(argc - 2, argv + 2);
    } else if (strcmp(cmd_name, "build") == 0) {
        cmd_build(argc - 2, argv + 2);
    } else if (strcmp(cmd_name, "publish") == 0) {
        /* publish = build --publish */
        char *new_argv[] = { argv[0], "build", "--publish" };
        cmd_build(1, new_argv + 2);
    } else if (strcmp(cmd_name, "info") == 0) {
        cmd_info();
    } else if (strcmp(cmd_name, "help") == 0 || strcmp(cmd_name, "--help") == 0) {
        print_usage();
    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd_name);
        print_usage();
        return 1;
    }

    return 0;
}
