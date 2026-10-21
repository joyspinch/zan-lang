#!/usr/bin/env python3
"""shot_window.py — 抓运行中 Zan 程序的窗口像素，存 PNG。

为什么不用 UiDriver 的 `dump pixels`：GuiHost 游戏每帧 present 后画布表面
被清空，dump 拿到的是全白帧（尺寸对、像素全 255），那是通道问题不是渲染
失败；窗口 BitBlt 抓的是屏幕上的真实最后一帧。Form 类应用相反——窗口
截图可能抓到还没重绘的空帧，那种场景仍用 `dump pixels`（ZPX1→PNG）。

用法：
    python scripts/shot_window.py <exe> <out.png> [等待秒数] [工作目录]

依赖：Pillow（pip install pillow）。仅 Windows。
"""

import ctypes
from ctypes import wintypes
import os
import subprocess
import sys
import time

from PIL import Image

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))

SRCCOPY = 0x00CC0020
DIB_RGB_COLORS = 0


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wintypes.DWORD), ("biWidth", wintypes.LONG),
                ("biHeight", wintypes.LONG), ("biPlanes", wintypes.WORD),
                ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
                ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", wintypes.LONG),
                ("biYPelsPerMeter", wintypes.LONG), ("biClrUsed", wintypes.DWORD),
                ("biClrImportant", wintypes.DWORD)]


class BITMAPINFO(ctypes.Structure):
    _fields_ = [("bmiHeader", BITMAPINFOHEADER), ("bmiColors", wintypes.DWORD * 3)]


def find_window(pid):
    """找 pid 拥有的第一个可见顶层窗口（0 = 没找到）。"""
    found = []
    callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def visit(hwnd, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    user32.EnumWindows(callback(visit), 0)
    return found[0] if found else 0


def capture(hwnd, path):
    """抓客户区像素存 PNG，返回 (宽, 高)。"""
    rect = wintypes.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    w, h = rect.right - rect.left, rect.bottom - rect.top
    window_dc = user32.GetDC(hwnd)
    mem_dc = gdi32.CreateCompatibleDC(window_dc)
    bmp = gdi32.CreateCompatibleBitmap(window_dc, w, h)
    gdi32.SelectObject(mem_dc, bmp)
    gdi32.BitBlt(mem_dc, 0, 0, w, h, window_dc, 0, 0, SRCCOPY)

    info = BITMAPINFO()
    info.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    info.bmiHeader.biWidth = w
    info.bmiHeader.biHeight = -h
    info.bmiHeader.biPlanes = 1
    info.bmiHeader.biBitCount = 32
    info.bmiHeader.biCompression = 0
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mem_dc, bmp, 0, h, buf, ctypes.byref(info), DIB_RGB_COLORS)
    img = Image.frombuffer("RGBA", (w, h), buf, "raw", "BGRA", 0, 1).convert("RGB")
    img.save(path)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem_dc)
    user32.ReleaseDC(hwnd, window_dc)
    return img.size


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    exe, out = sys.argv[1], sys.argv[2]
    wait = float(sys.argv[3]) if len(sys.argv) > 3 else 2.0
    cwd = sys.argv[4] if len(sys.argv) > 4 else None
    proc = subprocess.Popen([exe], cwd=cwd, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    try:
        deadline = time.monotonic() + 10
        hwnd = 0
        while not hwnd and time.monotonic() < deadline:
            hwnd = find_window(proc.pid)
            time.sleep(0.05)
        if not hwnd:
            print("NO WINDOW for pid", proc.pid)
            return 1
        time.sleep(wait)
        size = capture(hwnd, out)
        print("captured", size[0], "x", size[1], "->", out)
    finally:
        proc.kill()
        proc.wait()
    return 0


if __name__ == "__main__":
    sys.exit(main())
