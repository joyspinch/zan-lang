"""Compile the model and real scenes, then exercise the Windows game window."""
import argparse
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
TESTS = Path(__file__).resolve().parent
TEMPLATE = ROOT / "templates/game/pin-circle"
WORK = ROOT / "_scratch/pin-circle-tests"
SOURCES = [TEMPLATE / "src" / name for name in ("main.zan", "World.zan", "View.zan")]


def compile_program(compiler, name, sources, publish=False):
    exe = WORK / (name + ".exe")
    command = [str(compiler), *map(str, sources), "--auto-stdlib", "-o", str(exe)]
    if len(sources) > 2:
        command += ["--embed", "assets=" + str(TEMPLATE / "assets")]
    if publish:
        command += ["--publish", "--subsystem", "windows"]
    result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=240)
    log = result.stdout.decode("utf-8", errors="replace")
    (WORK / (name + "-build.log")).write_text(log, encoding="utf-8")
    success = "Published " in log if publish else "Compiled " in log
    if result.returncode or not success or "error:" in log or not exe.exists():
        raise AssertionError(log)
    return exe


def window_geometry(pid, resize):
    user = ctypes.WinDLL("user32", use_last_error=True)
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.IsWindowVisible.argtypes = [wintypes.HWND]
    user.GetDpiForWindow.argtypes = [wintypes.HWND]
    user.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int,
                                  ctypes.c_int, ctypes.c_int, wintypes.UINT]
    found = []
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    @callback_type
    def visit(hwnd, unused):
        owner = wintypes.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    deadline = time.monotonic() + 5
    while not found and time.monotonic() < deadline:
        user.EnumWindows(visit, 0)
        time.sleep(0.02)
    if not found:
        raise AssertionError("No game window for PID " + str(pid))
    title_height = round(32 * user.GetDpiForWindow(found[0]) / 96)
    if resize and not user.SetWindowPos(found[0], None, 0, 0, 900, 1350 + title_height, 0x16):
        raise ctypes.WinError(ctypes.get_last_error())
    return title_height


def check_pixels(out, size):
    for name in ("menu", "play", "paused", "failed", "cleared", "next-level"):
        path = out / (name + ".zpx")
        if not path.exists():
            continue
        data = path.read_bytes()
        if data[:4] != b"ZPX1":
            raise AssertionError("Invalid pixels: " + str(path))
        _, _, width, height, bpp = struct.unpack_from("<5I", data, 4)
        assert (width, height) == size and bpp == 4, (path, width, height, bpp)
        assert len(data) == 24 + width * height * 4
        # The former overwrite-gradient hid the top third and cut the wallpaper at y=300.
        if name == "menu":
            x, y = round(width * 0.03), round(height * 0.2)
            blue, green, red, _ = data[24 + (y * width + x) * 4:28 + (y * width + x) * 4]
            assert max(red, green, blue) > 24, "Wallpaper was covered or not loaded"
        subprocess.run([sys.executable, str(ROOT / "scripts/zpx2png.py"),
                        str(path), str(path.with_suffix(".png"))], check=True)


def window_test(exe, script, name, resize=False):
    out = WORK / name
    out.mkdir(exist_ok=True)
    adjusted = out / "driver.ui"
    original = script.read_text(encoding="utf-8")
    expected_passes = sum(line.startswith("assert ") for line in original.splitlines())
    title_height = round(32 * ctypes.WinDLL("user32").GetDpiForSystem() / 96)
    scale = 1.5 if resize else 1
    lines = []
    for line in original.splitlines():
        if line.startswith("click "):
            _, x, y = line.split()
            line = "click {} {}".format(round(int(x) * scale),
                                        round((int(y) - 48) * scale) + title_height)
        lines.append(line)
    adjusted.write_text("\n".join(lines) + "\n", encoding="utf-8")
    env = os.environ.copy()
    env["ZAN_UI_SCRIPT"] = str(adjusted)
    env["ZAN_UI_OUT"] = str(out)
    with (out / "window.log").open("wb") as log:
        process = subprocess.Popen([str(exe)], cwd=WORK, env=env,
                                   stdout=log, stderr=subprocess.STDOUT)
        print(name + " PID=" + str(process.pid), flush=True)
        try:
            actual_title = window_geometry(process.pid, resize)
            assert actual_title == title_height, "Test window moved to a different DPI monitor"
            code = process.wait(timeout=45)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
    results = (out / "results.log").read_text(encoding="utf-8")
    if code or "ASSERT FAIL" in results or not re.search(
            r"DONE pass={} fail=0\b".format(expected_passes), results):
        raise AssertionError(results)
    check_pixels(out, (900, 1350) if resize else (600, 900))
    print(name + ": " + results.splitlines()[-1])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build/zanc.exe")
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("Window regression requires Windows; world_test.zan is platform-independent")
    ctypes.WinDLL("user32").SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
    WORK.mkdir(parents=True, exist_ok=True)
    world = compile_program(args.compiler, "world-test", [TESTS / "world_test.zan", SOURCES[1]])
    result = subprocess.run([str(world)], cwd=WORK, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=60)
    log = result.stdout.decode("utf-8", errors="replace")
    (WORK / "world-test.log").write_text(log, encoding="utf-8")
    assert result.returncode == 0 and "PASS pin-circle:" in log and "FAIL:" not in log, log
    print(log.strip())
    game = compile_program(args.compiler, "game", SOURCES, publish=True)
    scene = compile_program(args.compiler, "scene-test",
                            [TESTS / "scene_test.zan", TESTS / "world_test.zan", *SOURCES])
    window_test(game, TESTS / "gameplay.ui", "gameplay")
    window_test(scene, TESTS / "cleared.ui", "cleared")
    window_test(game, TESTS / "gameplay.ui", "resized", resize=True)
    print("PASS pin-circle: model, published assets, gameplay, clear/next-level, 900x1350 resize")


if __name__ == "__main__":
    main()
