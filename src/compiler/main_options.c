/* main_options.c - 命令行帮助、@argfile 响应档展开与参数路径安全校验 */

static void print_usage(void) {
    fprintf(stderr, "Zan Compiler v%s\n", ZAN_VERSION);
    fprintf(stderr, "Usage: zanc <source.zan|dir...> [options]\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  Common:\n");
    fprintf(stderr, "    -o <output>      Output file (library: .dll/.so/.dylib shared,\n");
    fprintf(stderr, "                     .a/.lib static; executable: any other suffix)\n");
    fprintf(stderr, "    --emit-lib       Force library output (no CRT/entry point)\n");
    fprintf(stderr, "    -O0..-O3/-Os/-Oz Set optimization level (default: O2; --publish: Os;\n");
    fprintf(stderr, "                     -g forces O0; -Oz = minimum size for edge/embedded)\n");
    fprintf(stderr, "    --target <name>  Cross-compile for target (e.g. linux-x64, linux-musl)\n");
    fprintf(stderr, "    --list-targets   Show available cross-compilation targets\n");
    fprintf(stderr, "    --subsystem <s>  PE subsystem: windows (GUI; auto-detected for\n");
    fprintf(stderr, "                     GUI apps) or console (Windows only)\n");
    fprintf(stderr, "    --stdlib-path <dir>  Path to stdlib directory\n");
    fprintf(stderr, "    --auto-stdlib    Automatically find stdlib and installed package namespaces (default)\n");
    fprintf(stderr, "    --no-stdlib      Disable automatic stdlib and package discovery\n");
    fprintf(stderr, "    --no-packages    Disable package discovery only (stdlib pull-in stays on)\n");
    fprintf(stderr, "    --fast-alloc     Use the per-thread small-object allocator (default on\n");
    fprintf(stderr, "                     native targets; ignored where no allocator object exists)\n");
    fprintf(stderr, "    --no-fast-alloc  Fall back to CRT malloc (last of the two wins)\n");
    fprintf(stderr, "    -L<dir>/--libpath <dir>  Add a native library search directory\n");
    fprintf(stderr, "    --driver-dir <d> Override the bundled native driver directory\n");
    fprintf(stderr, "    --link-lib <name>  Link an extra native library (-> -l<name>)\n");
    fprintf(stderr, "    --link-input <f>   Link an extra object/resource/library file\n");
    fprintf(stderr, "    --embed <p[=n]>  Bake a file/directory into the executable as\n");
    fprintf(stderr, "                     resources named <n>/<relative path> (repeatable)\n");
    fprintf(stderr, "    --icon <f>       Embed an icon (Windows exe; also used by --emit-ipa)\n");
    fprintf(stderr, "    --no-icon        Do not embed any icon (skip --icon and the built-in default)\n");
    fprintf(stderr, "    --default-icon   Explicitly embed the built-in icon (re-enables it in\n");
    fprintf(stderr, "                     --publish, where it is skipped by default)\n");
    fprintf(stderr, "    --quiet, -q      Suppress progress lines on stdout (driver bundling,\n");
    fprintf(stderr, "                     APK packaging, the \"Compiled N files\" summary)\n");
    fprintf(stderr, "    --time           Report how long each compiler phase took\n");
    fprintf(stderr, "    -D<name>[=value] Define preprocessor symbol\n");
    fprintf(stderr, "    @<file>          Read further arguments from <file> (one per line or\n");
    fprintf(stderr, "                     whitespace separated, \"quoted\" for paths with spaces)\n");
    fprintf(stderr, "    --version, -v    Print version and exit\n");
    fprintf(stderr, "    --help, -h       Show this help and exit\n");
    fprintf(stderr, "  Publish:\n");
    fprintf(stderr, "    --publish        Release build: -Os by default, strip symbols, shard modules.\n");
    fprintf(stderr, "                     Leak checks/ARC quarantine default off; the over-release net\n");
    fprintf(stderr, "                     stays on unless --arc-guard/--no-arc-guard is given.\n");
    fprintf(stderr, "    --strip-symbols  Strip debug and non-exported symbols at link time\n");
    fprintf(stderr, "                     (implied by --publish, independent of optimization level)\n");
    fprintf(stderr, "    --link-mode <m>  Native driver linking on publish: shared (copy driver\n");
    fprintf(stderr, "                     libs next to the exe, default) or static (link into the exe)\n");
    fprintf(stderr, "    --obfuscate-strings  XOR-obfuscate user string literals in readonly data\n");
    fprintf(stderr, "                         (also ZAN_OBF=1; default off) (internal/unstable)\n");
    fprintf(stderr, "    --no-obfuscate-strings  Disable string obfuscation (last one wins; also\n");
    fprintf(stderr, "                            ZAN_NO_OBF=1) (internal/unstable)\n");
    fprintf(stderr, "    --hide-diagnostics  Replace file:line:col in runtime guard reports with\n");
    fprintf(stderr, "                        #site IDs (compile diagnostics unchanged) (internal/unstable)\n");
    fprintf(stderr, "    --emit-site-map <f>  Write the #ID -> file:line:col map used by\n");
    fprintf(stderr, "                         --hide-diagnostics (internal/unstable)\n");
    fprintf(stderr, "  Debug:\n");
    fprintf(stderr, "    -g, --debug      Emit DWARF debug info (forces -O0; also enables\n");
    fprintf(stderr, "                     --check-leaks and --arc-guard unless --publish is set)\n");
    fprintf(stderr, "    --check-leaks    Report unreleased objects at program exit (default with -g,\n");
    fprintf(stderr, "                     except under --publish)\n");
    fprintf(stderr, "    --no-check-leaks Turn leak reporting off\n");
    fprintf(stderr, "    --arc-guard      Quarantine freed objects and trap stale retain/release\n");
    fprintf(stderr, "                     (default with -g, except under --publish)\n");
    fprintf(stderr, "    --no-arc-guard   Turn quarantine off (also turns off the --publish\n");
    fprintf(stderr, "                     over-release net)\n");
    fprintf(stderr, "    --runtime-checks Enable runtime guards (default, including --publish)\n");
    fprintf(stderr, "    --no-runtime-checks  Disable runtime guards (e.g. division by zero)\n");
    fprintf(stderr, "    --strict-runtime  Guard failures exit(70); ZAN_RT_HARD=0 forces soft mode\n");
    fprintf(stderr, "    --deny-warnings   Treat compiler warnings as fatal errors\n");
    fprintf(stderr, "    --warn-level <lv> none | default | error (default: default;\n");
    fprintf(stderr, "                     error = --deny-warnings)\n");
    fprintf(stderr, "    --suppress <list> Suppress warnings by numeric code, e.g. W0003,0003\n");
    fprintf(stderr, "                     (comma/space separated; letter prefix is optional)\n");
    fprintf(stderr, "    --dump-tokens   Dump lexer tokens and exit\n");
    fprintf(stderr, "    --dump-ast      Dump parse tree (compilation continues)\n");
    fprintf(stderr, "    --emit-ir       Emit LLVM IR to stdout and exit without linking\n");
    fprintf(stderr, "    --emit-symbols <f> Write the type/member index (IDE completion) and exit\n");
    fprintf(stderr, "    --gen-meta <f>   Export compilation-unit metadata JSON to <f> and exit\n");
    fprintf(stderr, "    --api <symbol>   Query member signatures and types from API index\n");
    fprintf(stderr, "  Packaging:\n");
    fprintf(stderr, "    --emit-apk <f>   Build an Android APK (requires --target android-x64|android-arm64):\n");
    fprintf(stderr, "                     compile -> NativeActivity shell -> binary AXML -> sign\n");
    fprintf(stderr, "    --apk-package <id>  APK package name (CLI over zan.proj androidPackage; default\n");
    fprintf(stderr, "                     dev.zan.<input basename>)\n");
    fprintf(stderr, "    --apk-label <name>  APK display label (CLI over zan.proj androidLabel; default\n");
    fprintf(stderr, "                     input basename)\n");
    fprintf(stderr, "    --emit-ipa <f>   Build an iOS IPA (requires --target ios-arm64; -o <name>.ipa\n");
    fprintf(stderr, "                     enables it too)\n");
    fprintf(stderr, "    --ipa-bundle-id <id>  IPA bundle identifier (default: dev.zan.<app name>)\n");
    fprintf(stderr, "    --ipa-name <name>  App name inside the IPA (default: from the .ipa path)\n");
    fprintf(stderr, "  Internal (unstable):\n");
    fprintf(stderr, "    -ferror-limit=N  Stop after N errors (default: 100; 0 = unlimited)\n");
    fprintf(stderr, "    --package-api <url>  Configure marketplace API (HTTPS; localhost HTTP allowed);\n");
    fprintf(stderr, "                     must be paired with --package-install\n");
    fprintf(stderr, "    --package-list-missing  Scan inputs and print missing namespace diagnostics\n");
    fprintf(stderr, "                     (exit 3 when anything is missing)\n");
    fprintf(stderr, "    --package-install <dir> --package-name <name>  Install a local package directory\n");
    fprintf(stderr, "    --package-scope <project|global>  Select install scope (default: project)\n");
    fprintf(stderr, "    --package-project <dir>  Project root for package discovery and\n");
    fprintf(stderr, "                     project-scope installation\n");
}

typedef struct {
    char **items;
    int count;
    int cap;
} arg_list_t;

static void arg_list_push(arg_list_t *list, char *item) {
    if (list->count == list->cap) {
        list->cap = list->cap ? list->cap * 2 : 32;
        list->items = (char **)realloc(list->items, sizeof(char *) * (size_t)list->cap);
        if (!list->items) {
            fprintf(stderr, "error: out of memory expanding arguments\n");
            exit(1);
        }
    }
    list->items[list->count++] = item;
}

static bool expand_arg_file(const char *path, arg_list_t *out, int depth);

static void arg_list_add(arg_list_t *out, const char *token, int depth) {
    if (token[0] == '@' && token[1] != '\0') {
        if (!expand_arg_file(token + 1, out, depth + 1)) { exit(1); }
        return;
    }
    char *copy = strdup(token);
    if (!copy) {
        fprintf(stderr, "error: out of memory expanding arguments\n");
        exit(1);
    }
    arg_list_push(out, copy);
}

static bool expand_arg_file(const char *path, arg_list_t *out, int depth) {
    if (depth > 8) {
        fprintf(stderr, "error: argument file '%s' nested too deeply\n", path);
        return false;
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "error: cannot open argument file '%s'\n", path);
        return false;
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size < 0) { fclose(fp); return false; }
    char *text = (char *)malloc((size_t)size + 1);
    if (!text) { fclose(fp); return false; }
    size_t got = fread(text, 1, (size_t)size, fp);
    fclose(fp);
    text[got] = '\0';

    char *token = (char *)malloc(got + 1);
    if (!token) { free(text); return false; }
    size_t i = 0;
    if (got >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB
        && (unsigned char)text[2] == 0xBF) {
        i = 3;
    }
    while (i < got) {
        while (i < got && (unsigned char)text[i] <= ' ') { i++; }
        if (i >= got) { break; }
        if (text[i] == '#') {
            while (i < got && text[i] != '\n') { i++; }
            continue;
        }
        size_t n = 0;
        bool quoted = false;
        while (i < got) {
            char c = text[i];
            if (c == '"') { quoted = !quoted; i++; continue; }
            if (!quoted && (unsigned char)c <= ' ') { break; }
            token[n++] = c;
            i++;
        }
        token[n] = '\0';
        if (n > 0) { arg_list_add(out, token, depth); }
    }
    free(token);
    free(text);
    return true;
}

static void expand_arg_files(int *argc, char ***argv) {
    bool any = false;
    for (int i = 1; i < *argc; i++) {
        if ((*argv)[i][0] == '@' && (*argv)[i][1] != '\0') { any = true; break; }
    }
    if (!any) { return; }
    arg_list_t out = {NULL, 0, 0};
    arg_list_add(&out, (*argv)[0], 0);
    for (int i = 1; i < *argc; i++) { arg_list_add(&out, (*argv)[i], 0); }
    arg_list_push(&out, NULL);
    *argc = out.count - 1;
    *argv = out.items;
}

/* 链接器参数值（--link-input / --libpath / -L）的最低限度校验：
 * 主路径走 spawnv 的 argv 数组（无 shell，任何字节都安全）；但无捆绑
 * ld 的回退档与 POSIX 分支仍以引号包裹拼进 system() 命令行——值里出现
 * 双引号会破引号逃逸成命令注入（审计 P1-11）。合法路径不携带双引号
 * （Windows 文件名非法字符；POSIX 上也从未必要——参数经 argv 传递，
 * 空格无需引号包裹），控制字符同理一律拒绝。不做转义：转义层要同时
 * 伺候 cmd 与 /bin/sh 两套规则，漏一条就是注入。 */
static bool zan_link_path_is_safe(const char *v) {
    for (const char *p = v; *p; p++) {
        if (*p == '"' || (unsigned char)*p < 0x20) {
            return false;
        }
    }
    return true;
}
