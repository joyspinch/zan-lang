#!/usr/bin/env python3
"""Run complete template links with at most two workers inside ONE external gate.

No locks or scheduler registrations are acquired here. --filter is a Python
case-sensitive regex over category/template (and html_gallery). No cached passes
are reused; every selected project links again. Use --list to inspect selection.
"""

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import threading
import time
import uuid


ROOT = Path(__file__).resolve().parents[1]


def timestamp():
    return datetime.now(timezone.utc).isoformat()


def discover(root):
    """Match run_templates.cmake's two-level glob, plus its registered extra."""
    projects = sorted((root / "templates").glob("*/*/zan.proj"))
    result = [(p.parent.relative_to(root / "templates").as_posix(), p) for p in projects]
    gallery = root / "examples/html_gallery/zan.proj"
    if not gallery.is_file():
        raise ValueError(f"required extra project does not exist: {gallery}")
    result.append(("html_gallery", gallery))
    return result


def exact_cmake_regex(name):
    # Escape CMake regex metacharacters; Python re.escape also escapes unrelated
    # punctuation and should not define another regex engine's input contract.
    return "^" + "".join("\\" + c if c in r"\.^$[]()|*+?" else c for c in name) + "$"


def stop_process_tree(process):
    if os.name == "nt":
        subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15)
    else:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()


def execute(args, selected, output):
    lock = threading.Lock()
    cancel = threading.Event()
    rows = [{"template": name, "status": "pending", "project": str(project)}
            for name, project in selected]
    report = {"started": timestamp(), "complete": False, "workers": args.workers,
              "selected": len(rows), "failed": 0, "rows": rows}

    def checkpoint(index=None, update=None):
        with lock:
            if index is not None:
                rows[index].update(update)
                event = dict(rows[index], time=timestamp())
                with (output / "journal.jsonl").open("a", encoding="utf-8") as journal:
                    journal.write(json.dumps(event, ensure_ascii=False) + "\n")
                    journal.flush()
            report["failed"] = sum(r["status"] in ("fail", "timeout", "cancelled") for r in rows)
            temporary = output / "results.json.tmp"
            temporary.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                                 encoding="utf-8")
            temporary.replace(output / "results.json")

    checkpoint()

    def run_one(index):
        name, project = selected[index]
        if cancel.is_set():
            checkpoint(index, {"status": "cancelled", "detail": "batch interrupted"})
            return
        # Independent WORK and batch index for each item, even when run in parallel.
        work = output / "projects" / name.replace("/", "_")
        work.mkdir(parents=True)
        log = work / "cmake.log"
        command = [
            args.cmake, f"-DZANC={args.zanc}",
            f"-DTEMPLATES={(args.root / 'templates').as_posix()}",
            f"-DEXTRA_PROJECTS={(args.root / 'examples/html_gallery/zan.proj').as_posix()}",
            f"-DSTDLIB={args.stdlib.as_posix()}", f"-DDRIVER_DIR={args.driver_dir.as_posix()}",
            f"-DWORK={work.as_posix()}", f"-DEXE_EXT={args.exe_ext}",
            f"-DFILTER={exact_cmake_regex(name)}", f"-DCOMPILE_TIMEOUT={args.timeout}",
            "-P", str(args.gate),
        ]
        checkpoint(index, {"status": "running", "started": timestamp(),
                           "work": str(work), "log": str(log), "command": command})
        print(f"START {name}", flush=True)
        began = time.monotonic()
        process = None
        try:
            with log.open("w", encoding="utf-8") as stream:
                process = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT,
                                           cwd=args.root, start_new_session=os.name != "nt")
                deadline = began + args.outer_timeout
                status = None
                while process.poll() is None:
                    if cancel.is_set():
                        status = "cancelled"
                        break
                    if time.monotonic() >= deadline:
                        status = "timeout"
                        break
                    cancel.wait(min(0.2, max(0, deadline - time.monotonic())))
                if status:
                    stop_process_tree(process)
                    detail = ("batch interrupted" if status == "cancelled" else
                              f"outer process timeout after {args.outer_timeout}s")
                    update = {"status": status, "detail": detail}
                else:
                    data = json.loads((work / "results.json").read_text(encoding="utf-8"))
                    item_rows = data.get("rows", [])
                    if len(item_rows) != 1 or item_rows[0].get("template") != name:
                        raise ValueError("CMake result must contain exactly the selected project")
                    item = item_rows[0]
                    artifact = Path(item.get("artifact", ""))
                    valid = (process.returncode == 0 and data.get("complete") is True
                             and item.get("status") == "pass"
                             and artifact.is_file() and artifact.stat().st_size > 0)
                    update = {"status": "pass" if valid else
                              "timeout" if item.get("status") == "timeout" else "fail",
                              "detail": item.get("result", "missing compiler result"),
                              "exit_code": process.returncode, "result": item}
                    if not valid and item.get("status") == "pass":
                        update["detail"] = "invalid pass: exit status, completion or artifact check failed"
        except Exception as error:
            if process is not None and process.poll() is None:
                stop_process_tree(process)
            update = {"status": "fail", "detail": f"{type(error).__name__}: {error}"}
        update.update(finished=timestamp(), seconds=round(time.monotonic() - began, 3))
        checkpoint(index, update)
        print(f"{update['status'].upper()} {name} ({update['seconds']}s)", flush=True)

    try:
        with ThreadPoolExecutor(max_workers=args.workers) as pool:
            futures = [pool.submit(run_one, i) for i in range(len(rows))]
            try:
                for future in as_completed(futures):
                    future.result()
            except BaseException:
                cancel.set()
                raise
    except KeyboardInterrupt:
        report["interrupted"] = True
    report["complete"] = all(r["status"] not in ("pending", "running") for r in rows)
    report["finished"] = timestamp()
    checkpoint()
    passed = sum(r["status"] == "pass" for r in rows)
    print(f"templates={len(rows)} passed={passed} failed={report['failed']} results={output / 'results.json'}",
          flush=True)
    return 0 if passed == len(rows) and not report.get("interrupted") else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--filter", default="", help="case-sensitive Python regex; zero matches fails")
    parser.add_argument("--workers", type=int, choices=(1, 2), default=2)
    parser.add_argument("--timeout", type=float, default=600, help="per-project compiler timeout in seconds")
    parser.add_argument("--outer-timeout", type=float, help="per-project CMake timeout (default: timeout + 60)")
    parser.add_argument("--output", type=Path, help="new results directory; must not already exist")
    parser.add_argument("--zanc", help="compiler executable (or a CMake command list for harness tests)")
    parser.add_argument("--stdlib", type=Path)
    parser.add_argument("--driver-dir", type=Path)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--gate", type=Path, default=ROOT / "tests/run_templates.cmake")
    parser.add_argument("--exe-ext", default=".exe" if os.name == "nt" else "")
    parser.add_argument("--list", action="store_true", help="print selected names without running or writing")
    args = parser.parse_args()
    args.root = args.root.resolve()
    args.gate = args.gate.resolve()
    args.stdlib = (args.stdlib or args.root / "stdlib").resolve()
    args.driver_dir = (args.driver_dir or args.root / "build").resolve()
    args.zanc = args.zanc or (args.root / "build" / ("zanc.exe" if os.name == "nt" else "zanc")).as_posix()
    args.outer_timeout = args.outer_timeout if args.outer_timeout is not None else args.timeout + 60
    if not all(math.isfinite(v) and v > 0 for v in (args.timeout, args.outer_timeout)):
        parser.error("timeouts must be finite positive numbers")
    try:
        pattern = re.compile(args.filter)
        selected = [(name, p) for name, p in discover(args.root) if pattern.search(name)]
        if not selected:
            raise ValueError(f"no projects matched --filter {args.filter!r}")
        if args.list:
            print("\n".join(name for name, _ in selected))
            return 0
        output = (args.output or args.root / "build/test-out/template-acceptance" /
                  (datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8])).resolve()
        output.mkdir(parents=True, exist_ok=False)
    except (ValueError, OSError, re.error) as error:
        parser.error(str(error))
    print(f"selected={len(selected)} workers={args.workers} results={output}", flush=True)
    return execute(args, selected, output)


if __name__ == "__main__":
    raise SystemExit(main())
