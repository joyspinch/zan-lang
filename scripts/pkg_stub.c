/* Self-extracting single-file game launcher.
 *
 * package_games.ps1 appends a zip payload (game exe + SDL DLLs + assets) and a
 * 16-byte footer: <8-byte LE payload size> "ZANPKG1\0". On launch the stub
 * extracts the payload to %LOCALAPPDATA%\ZanGames\<exe-name>\ (skipped when
 * already extracted, keyed by payload size) and starts the inner game with
 * that directory as its working directory, so asset paths resolve.
 *
 * Build: gcc -O2 -mwindows scripts/pkg_stub.c -o build/pkg_stub.exe
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define FOOTER_MAGIC "ZANPKG1\0"

static void die(const char *msg) {
    MessageBoxA(NULL, msg, "Zan Game Launcher", MB_ICONERROR);
    ExitProcess(1);
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
    char root[MAX_PATH], dir[MAX_PATH], zip[MAX_PATH], stamp[MAX_PATH];
    if (GetEnvironmentVariableA("LOCALAPPDATA", root, MAX_PATH) == 0)
        die("LOCALAPPDATA not set");
    snprintf(dir, MAX_PATH, "%s\\ZanGames\\%s", root, base);
    snprintf(zip, MAX_PATH, "%s\\payload.zip", dir);
    snprintf(stamp, MAX_PATH, "%s\\payload.size", dir);

    /* skip extraction when the stamp matches the current payload size */
    int need = 1;
    FILE *sf = fopen(stamp, "rb");
    if (sf) {
        long long old = 0;
        if (fscanf(sf, "%lld", &old) == 1 && old == psize) need = 0;
        fclose(sf);
    }

    if (need) {
        char mk[MAX_PATH + 32];
        snprintf(mk, sizeof(mk), "%s\\ZanGames", root);
        CreateDirectoryA(mk, NULL);
        CreateDirectoryA(dir, NULL);
        /* copy payload bytes into payload.zip */
        FILE *z = fopen(zip, "wb");
        if (!z) die("cannot write payload.zip");
        _fseeki64(f, poff, SEEK_SET);
        char buf[1 << 16];
        long long left = psize;
        while (left > 0) {
            size_t take = left > (long long)sizeof(buf) ? sizeof(buf) : (size_t)left;
            size_t got = fread(buf, 1, take, f);
            if (got == 0) die("payload read error");
            fwrite(buf, 1, got, z);
            left -= (long long)got;
        }
        fclose(z);
        /* extract with PowerShell (present on every supported Windows) */
        char ps[2 * MAX_PATH + 256];
        snprintf(ps, sizeof(ps),
                 "powershell -NoProfile -ExecutionPolicy Bypass -Command "
                 "\"Expand-Archive -LiteralPath '%s' -DestinationPath '%s' -Force\"",
                 zip, dir);
        STARTUPINFOA si; PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
        if (!CreateProcessA(NULL, ps, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                            NULL, NULL, &si, &pi))
            die("failed to run extractor");
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        if (code != 0) die("payload extraction failed");
        DeleteFileA(zip);
        sf = fopen(stamp, "wb");
        if (sf) { fprintf(sf, "%lld", psize); fclose(sf); }
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

    /* launch the real program with the extraction dir as cwd: game.exe when
     * present (game packages), otherwise <stub-basename>.exe (app packages) */
    char game[MAX_PATH];
    snprintf(game, MAX_PATH, "%s\\game.exe", dir);
    if (GetFileAttributesA(game) == INVALID_FILE_ATTRIBUTES)
        snprintf(game, MAX_PATH, "%s\\%s.exe", dir, base);
    STARTUPINFOA si; PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    if (!CreateProcessA(game, NULL, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi))
        die("failed to start program");
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return 0;
}
