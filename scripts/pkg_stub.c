/* Self-extracting single-file game launcher.
 *
 * package_games.ps1 appends a zip payload (game exe + SDL DLLs + assets) and a
 * 16-byte footer: <8-byte LE payload size> "ZANPKG1\0". On launch the stub
 * extracts the payload to %LOCALAPPDATA%\ZanGames\<exe-name>\ (skipped when the
 * cache already holds this exact payload) and starts the inner program with
 * the LAUNCHER's OWN directory as its working directory.
 *
 * The working directory is deliberately not the extraction directory: a
 * program launched there resolves every relative path -- including everything
 * it writes: save games, logs, config -- inside a per-user cache the next
 * publish replaces, which is not where the user put the program. Bundled
 * read-only resources are still found: the payload directory is exported as
 * ZAN_PKG_DIR and the runtime retries a relative read there (see
 * zan_pkg_fopen in src/runtime/rt_file.c).
 *
 * Extraction hands the launcher itself to bsdtar (tar.exe, shipped with Windows
 * since 10/1803): libarchive finds the zip's end-of-central-directory record
 * past our footer and reads the entries in place, so the payload is never
 * copied to a temporary zip first. PowerShell -- whose start-up alone costs
 * more than the extraction, before Expand-Archive walks the archive entry by
 * entry -- is only the fallback on Windows without tar.exe.
 *
 * Build: gcc -O2 -mwindows scripts/pkg_stub.c -o build/pkg_stub.exe
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define FOOTER_MAGIC "ZANPKG1\0"
#define STAMP_MAGIC "ZANPKG2"

static void die(const char *msg) {
    MessageBoxA(NULL, msg, "Zan Game Launcher", MB_ICONERROR);
    ExitProcess(1);
}

/* Identity of a payload, cheap enough to compute on every launch: its size
 * plus a hash of its tail. The zip central directory lives there and carries
 * every entry's name, size and CRC, so any change to any packaged file changes
 * this -- unlike the size alone, which happily matched a stale cache after a
 * rebuild and is why a fixed bug could come back on the next run. */
static unsigned long long payload_id(FILE *f, long long poff, long long psize) {
    long long tail = psize > (1 << 20) ? (1 << 20) : psize;
    unsigned long long h = 1469598103934665603ULL; /* FNV-1a */
    if (_fseeki64(f, poff + psize - tail, SEEK_SET) != 0) return 0;
    unsigned char buf[1 << 16];
    long long left = tail;
    while (left > 0) {
        size_t take = left > (long long)sizeof(buf) ? sizeof(buf) : (size_t)left;
        size_t got = fread(buf, 1, take, f);
        if (got == 0) break;
        for (size_t i = 0; i < got; i++) {
            h ^= buf[i];
            h *= 1099511628211ULL;
        }
        left -= (long long)got;
    }
    return h ^ (unsigned long long)psize;
}

/* Extracts the zip appended to `archive` into `dir` with bsdtar. */
static int extract_via_tar(const char *tar, const char *archive,
                           const char *dir) {
    char line[2 * MAX_PATH + 64];
    snprintf(line, sizeof(line), "\"%s\" -xf \"%s\" -C \"%s\"", tar, archive,
             dir);
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (!CreateProcessA(NULL, line, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL,
                        NULL, &si, &pi))
        return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}

/* Fallback for Windows without tar.exe: the old copy-then-PowerShell path. */
static int extract_via_powershell(const char *dir, FILE *f, long long poff,
                                  long long psize) {
    char zip[MAX_PATH];
    snprintf(zip, MAX_PATH, "%s\\payload.zip", dir);
    FILE *z = fopen(zip, "wb");
    if (!z) return -1;
    if (_fseeki64(f, poff, SEEK_SET) != 0) { fclose(z); return -1; }
    char buf[1 << 16];
    long long left = psize;
    while (left > 0) {
        size_t take = left > (long long)sizeof(buf) ? sizeof(buf) : (size_t)left;
        size_t got = fread(buf, 1, take, f);
        if (got == 0) { fclose(z); return -1; }
        fwrite(buf, 1, got, z);
        left -= (long long)got;
    }
    fclose(z);

    char ps[2 * MAX_PATH + 256];
    snprintf(ps, sizeof(ps),
             "powershell -NoProfile -ExecutionPolicy Bypass -Command "
             "\"Expand-Archive -LiteralPath '%s' -DestinationPath '%s' -Force\"",
             zip, dir);
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (!CreateProcessA(NULL, ps, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL,
                        NULL, &si, &pi)) {
        DeleteFileA(zip);
        return -1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    DeleteFileA(zip);
    return (int)code;
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show) {
    char self[MAX_PATH];
    GetModuleFileNameA(NULL, self, MAX_PATH);

    /* read footer */
    FILE *f = fopen(self, "rb");
    if (!f) die("cannot open launcher file");
    fseek(f, -16, SEEK_END);
    long long total_end = _ftelli64(f) + 16;
    unsigned char foot[16];
    if (fread(foot, 1, 16, f) != 16) die("cannot read footer");
    if (memcmp(foot + 8, FOOTER_MAGIC, 8) != 0) die("no payload appended");
    long long psize = 0;
    for (int i = 7; i >= 0; i--) psize = (psize << 8) | foot[i];
    long long poff = total_end - 16 - psize;

    /* target dir: %LOCALAPPDATA%\ZanGames\<basename-without-ext> */
    char base[MAX_PATH];
    const char *bs = strrchr(self, '\\');
    lstrcpynA(base, bs ? bs + 1 : self, MAX_PATH);
    char *dot = strrchr(base, '.');
    if (dot) *dot = 0;
    char root[MAX_PATH], dir[MAX_PATH], stamp[MAX_PATH];
    if (GetEnvironmentVariableA("LOCALAPPDATA", root, MAX_PATH) == 0)
        die("LOCALAPPDATA not set");
    snprintf(dir, MAX_PATH, "%s\\ZanGames\\%s", root, base);
    snprintf(stamp, MAX_PATH, "%s\\payload.id", dir);

    /* skip extraction when the cache already holds this exact payload */
    unsigned long long id = payload_id(f, poff, psize);
    int need = 1;
    FILE *sf = fopen(stamp, "rb");
    if (sf) {
        char tag[16];
        unsigned long long old = 0;
        if (fscanf(sf, "%15s %llu", tag, &old) == 2
            && strcmp(tag, STAMP_MAGIC) == 0 && old == id)
            need = 0;
        fclose(sf);
    }

    if (need) {
        char mk[MAX_PATH + 32];
        snprintf(mk, sizeof(mk), "%s\\ZanGames", root);
        CreateDirectoryA(mk, NULL);
        CreateDirectoryA(dir, NULL);
        /* A stale stamp must not survive a half-written extraction. */
        DeleteFileA(stamp);

        char tar[MAX_PATH];
        UINT n = GetSystemDirectoryA(tar, MAX_PATH);
        int code = -1;
        if (n > 0 && n < MAX_PATH - 16) {
            lstrcatA(tar, "\\tar.exe");
            if (GetFileAttributesA(tar) != INVALID_FILE_ATTRIBUTES)
                code = extract_via_tar(tar, self, dir);
        }
        if (code != 0) code = extract_via_powershell(dir, f, poff, psize);
        if (code != 0) die("payload extraction failed");

        sf = fopen(stamp, "wb");
        if (sf) { fprintf(sf, "%s %llu", STAMP_MAGIC, id); fclose(sf); }
    }
    fclose(f);

    /* tell the launched program where the launcher lives: apps that ship
     * sibling folders next to the launcher (e.g. the IDE's toolchain\,
     * stdlib\ ...) resolve them via ZAN_APP_DIR instead of the cache dir */
    char selfdir[MAX_PATH];
    lstrcpynA(selfdir, self, MAX_PATH);
    char *sl = strrchr(selfdir, '\\');
    if (sl) *sl = 0;
    SetEnvironmentVariableA("ZAN_APP_DIR", selfdir);
    /* where the payload was unpacked, for the runtime's read fallback */
    SetEnvironmentVariableA("ZAN_PKG_DIR", dir);

    /* launch the real program from the LAUNCHER's directory: game.exe when
     * present (game packages), otherwise <stub-basename>.exe (app packages) */
    char game[MAX_PATH];
    snprintf(game, MAX_PATH, "%s\\game.exe", dir);
    if (GetFileAttributesA(game) == INVALID_FILE_ATTRIBUTES)
        snprintf(game, MAX_PATH, "%s\\%s.exe", dir, base);
    STARTUPINFOA si; PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    if (!CreateProcessA(game, NULL, NULL, NULL, FALSE, 0, NULL, selfdir, &si,
                        &pi))
        die("failed to start program");
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return 0;
}
