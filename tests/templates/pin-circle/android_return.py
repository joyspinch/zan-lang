"""Check Android scene returns on an explicitly selected, dedicated emulator.

Requires Pillow and adb. Example:
  python tests/templates/pin-circle/android_return.py --serial emulator-5560 \
    --apk build/pin-circle/PinCircle-arm64-menu-fix.apk
"""
import argparse
import io
from pathlib import Path
import re
import subprocess
import time

from PIL import Image, ImageChops, ImageStat

ROOT = Path(__file__).resolve().parents[3]
PACKAGE = "com.zan.pincircle"
ACTIVITY = PACKAGE + "/android.app.NativeActivity"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True, help="Dedicated Android test device")
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--apk", type=Path, required=True)
    parser.add_argument("--cleared-apk", type=Path,
                        help="Optional APK built from scene_test.zan, package com.zan.pincircle.probe")
    parser.add_argument("--out", type=Path,
                        default=ROOT / "build/pin-circle/verification/menu-return")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    base = [args.adb, "-s", args.serial]
    package = PACKAGE
    checks = []

    def adb(*words, allow_failure=False):
        result = subprocess.run(base + list(words), capture_output=True, timeout=45)
        if result.returncode and not allow_failure:
            raise RuntimeError(result.stderr.decode(errors="replace"))
        return result.stdout

    def check(condition, label):
        if not condition:
            raise AssertionError(label)
        checks.append(label)
        print("PASS", label, flush=True)

    def screenshot(name):
        data = adb("exec-out", "screencap", "-p")
        (args.out / (name + ".png")).write_bytes(data)
        return Image.open(io.BytesIO(data)).convert("RGB")

    def pid():
        return adb("shell", "pidof", package, allow_failure=True).decode().strip()

    def tap(x, y):
        adb("shell", "input", "tap", str(round(x)), str(round(y)))

    def back():
        adb("shell", "input", "keyevent", "4")

    def pixels(image):
        data = image.tobytes()
        return zip(data[0::3], data[1::3], data[2::3])

    def launch():
        adb("shell", "am", "start", "-n", package + "/android.app.NativeActivity")
        ready = False
        for _ in range(30):
            time.sleep(.5)
            image = screenshot("checked-launch")
            if package == PACKAGE:
                ready = sum(r > 150 and g > 125 and r > b * 1.25
                            for r, g, b in pixels(title(image))) > 1000
            else:
                ready = marker(image, won=True)
            if ready:
                break
        check(ready and bool(pid()), package + " renders after launch")

    def marker(image, won=False):
        width, height = image.size
        unit = min(width / 600, height / 900)
        cy = height / 2 - round(12 * unit) - round(126 * unit)
        mark = pixels(image.crop((width / 2 - 24 * unit, cy - 20 * unit,
                                  width / 2 + 24 * unit, cy + 20 * unit)))
        if won:
            return sum(g > 180 and r < 175 and b > 130 for r, g, b in mark) > 25
        return sum(r > 185 and g < 175 and b < 155 for r, g, b in mark) > 25

    def title(image):
        width, height = image.size
        return image.crop((width * .2, height * .025, width * .8, height * .16))

    reference = None
    expected_pid = None

    def menu(name):
        time.sleep(.6)
        image = screenshot(name)
        diff = ImageStat.Stat(ImageChops.difference(reference, title(image))).mean
        check(max(diff) < 2, name + " shows menu title")
        check(pid() == expected_pid, name + " keeps native process")

    def fail_round(name):
        tap(w / 2, h - 196 * unit)
        time.sleep(.4)
        for attempt in range(8):
            for _ in range(8):
                tap(w / 2, h / 2 - 84 * unit + 312 * 1.32 * unit)
                time.sleep(.08)
            time.sleep(.7)
            image = screenshot(name + ("" if attempt == 0 else "-" + str(attempt)))
            if marker(image):
                check(True, name + " reaches real failure result")
                return
            if marker(image, won=True):
                tap(w / 2, h / 2 - 12 * unit + 80 * unit)
                time.sleep(.4)
        raise AssertionError(name + " did not reach failure result")

    def logs(name):
        text = adb("logcat", "-d", "-v", "threadtime").decode(errors="replace")
        (args.out / (name + ".log")).write_text(text, encoding="utf-8")
        check(not re.search(r"Fatal signal|FATAL EXCEPTION|ANR in " + re.escape(package)
                            + r"|Input dispatching timed out", text), name + " has no crash or ANR")

    def exited(name):
        time.sleep(6)
        activity = adb("shell", "dumpsys", "activity", "activities").decode(errors="replace")
        (args.out / (name + "-activity.txt")).write_text(activity, encoding="utf-8")
        check(not any(package + "/android.app.NativeActivity" in line
                      and ("mResumedActivity" in line or "topResumedActivity" in line)
                      for line in activity.splitlines()), name + " leaves Activity")
        screenshot(name)
        logs(name)

    adb("shell", "am", "force-stop", package)
    adb("install", "-r", str(args.apk.resolve()))
    adb("logcat", "-c")
    try:
        launch()
        initial = screenshot("checked-menu")
        w, h = initial.size
        unit = min(w / 600, h / 900)
        reference = title(initial)
        expected_pid = pid()
        fail_round("checked-failed-button")
        tap(w / 2, h / 2 - 12 * unit + 182 * unit)
        menu("checked-result-button-menu")
        fail_round("checked-failed-back")
        back()
        menu("checked-result-back-menu")
        tap(w / 2, h - 196 * unit)
        time.sleep(.4)
        tap(w - 44 * unit, 48 * unit)
        time.sleep(.5)
        screenshot("checked-paused")
        back()
        menu("checked-paused-back-menu")
        tap(w / 2, h - 196 * unit)
        time.sleep(.5)
        back()
        menu("checked-play-back-menu")
        adb("shell", "input", "keyevent", "3")
        time.sleep(.6)
        adb("shell", "am", "start", "-n", ACTIVITY)
        menu("checked-background-menu")
        back()
        exited("checked-menu-exit")
        launch()
        expected_pid = pid()
        menu("checked-relaunch-menu")
        fail_round("checked-failed-immediate-back")
        tap(w / 2, h / 2 - 12 * unit + 182 * unit)
        back()
        exited("checked-return-then-exit")

        if args.cleared_apk:
            package = "com.zan.pincircle.probe"
            adb("shell", "am", "force-stop", package)
            adb("install", "-r", str(args.cleared_apk.resolve()))
            adb("logcat", "-c")
            launch()
            expected_pid = pid()
            for attempt in range(30):
                time.sleep(.5)
                image = screenshot("checked-cleared")
                if marker(image, won=True):
                    break
            check(marker(image, won=True), "real scene fixture reaches clear result")
            tap(w / 2, h / 2 - 12 * unit + 182 * unit)
            menu("checked-clear-button-menu")
            tap(w / 2, h - 196 * unit)
            for attempt in range(30):
                time.sleep(.5)
                image = screenshot("checked-cleared-back")
                if marker(image, won=True):
                    break
            check(marker(image, won=True), "restarted fixture reaches clear result")
            back()
            menu("checked-clear-back-menu")
            back()
            exited("checked-clear-menu-exit")
        (args.out / "android-return-results.txt").write_text(
            "\n".join("PASS " + label for label in checks) + "\n", encoding="utf-8")
        print("PASS Android return regression:", len(checks), "checks", flush=True)
    finally:
        adb("shell", "am", "force-stop", package, allow_failure=True)


if __name__ == "__main__":
    main()
