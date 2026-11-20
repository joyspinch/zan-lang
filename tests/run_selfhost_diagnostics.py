"""Exercise the diagnostic CLI of an already built selfhost compiler."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


JSONL = "--diagnostics-format=jsonl"
TEXT = "--diagnostics-format=text"
FORMAT = "zan-diagnostic-v1"


def run(gen1, *args):
    return subprocess.run([gen1, *args], capture_output=True, timeout=45, check=False)


def records(result):
    assert FORMAT.encode() not in result.stdout, result.stdout
    diagnostics = []
    for line in result.stderr.splitlines():
        if line.startswith(b"{"):
            record = json.loads(line.decode("utf-8"))
            assert record["format"] == FORMAT, record
            diagnostics.append(record)
    return diagnostics


def diagnostic(severity, source, line, col, code, message):
    file = str(source).encode("utf-8", "surrogateescape").decode("utf-8", "replace")
    if os.name == "nt":
        file = file.replace("\\", "/").lower()
    return dict(format=FORMAT, severity=severity, file=file, line=line,
                col=col, code=code, message=message)


def check(gen1, work):
    for flag in ("--help", "-h"):
        help_result = run(gen1, flag)
        assert help_result.returncode == 0, help_result
        options = {line.split()[0] for stream in (help_result.stdout, help_result.stderr)
                   for line in stream.splitlines() if line.split()}
        assert {JSONL.encode(), TEXT.encode()} <= options, help_result

    invalid = run(gen1, JSONL, "--diagnostics-format=unknown")
    assert invalid.returncode == 1 and not records(invalid), invalid
    assert b"--diagnostics-format must be 'text' or 'jsonl'" in invalid.stderr, invalid

    stdlib = work / "stdlib"
    stdlib.mkdir()
    warning_source = work / "Warning.zan"
    warning_source.write_bytes(b"delegate int Callback();\n"
                               b"class Capture {\n"
                               b"    int value;\n"
                               b"    Callback Get() { return () => this.value; }\n"
                               b"}\n")
    main_source = work / "诊断 test.zan"
    main_source.write_bytes(b"class Program { static void Main() {} }\n")
    output = work / "diagnostic.ll"
    args = (str(warning_source), str(main_source), "--stdlib-path", str(stdlib), "-o", str(output))
    warning = diagnostic("warning", warning_source, 4, 22, "W2072",
                         "returned closure captures 'this' and escapes method scope; "
                         "ensure the caller does not retain it indefinitely to prevent ARC memory leaks")

    result = run(gen1, *args, TEXT, JSONL)
    assert result.returncode == 0 and output.is_file(), result
    assert records(result) == [warning], result
    assert b"[W2072]" not in result.stdout + result.stderr, result
    assert b"\x1b" not in result.stdout + result.stderr, result

    output.unlink()
    denied = run(gen1, *args, JSONL, "--deny-warnings")
    assert denied.returncode == 1 and not output.exists(), denied
    assert records(denied) == [dict(warning, severity="error")], denied
    assert b"warnings denied by --deny-warnings" in denied.stdout + denied.stderr, denied

    # Put a multibyte comment before the string so the byte column differs
    # from its character column. The escape error is emitted during IR generation.
    prefix = "    /* 中🙂 */ string value = "
    main_source.write_bytes(("class Program { static void Main() {\n"
                             f'{prefix}"\\q";\n'
                             "} }\n").encode("utf-8"))
    expected_error = diagnostic("error", main_source, 2, len(prefix.encode("utf-8")) + 1,
                                "E0017", "invalid escape sequence '\\q'")
    rejected = run(gen1, *args, JSONL)
    assert rejected.returncode == 1 and not output.exists(), rejected
    assert records(rejected) == [warning, expected_error], rejected
    assert b"[E0017]" not in rejected.stdout + rejected.stderr, rejected
    assert b"\x1b" not in rejected.stdout + rejected.stderr, rejected
    assert b"1 error(s), 1 warning(s)" in rejected.stdout + rejected.stderr, rejected

    for flags in ((), (JSONL, TEXT)):
        text = run(gen1, *args, *flags)
        assert text.returncode == 1 and not output.exists() and not records(text), text
        assert b"[W2072] returned closure" in text.stdout, text
        assert b"[E0017] invalid escape sequence" in text.stdout and b" | " in text.stdout, text

    # Lexical diagnostics also reach the metadata export's early failure path.
    # POSIX allows an ill-formed UTF-8 filename with JSON escape characters.
    metadata_source = main_source if os.name == "nt" else work / 'bad "\\\t\udcff.zan'
    metadata_source.write_bytes(b"class Program { static void Main() { char value = '\\q'; } }\n")
    metadata = work / "diagnostic.json"
    result = run(gen1, str(metadata_source), "--stdlib-path", str(stdlib), "--gen-meta", str(metadata), JSONL)
    assert result.returncode == 1 and not metadata.exists(), result
    assert records(result) == [diagnostic("error", metadata_source, 1, 51, "E0017",
                                         "invalid escape sequence '\\q'")], result

    # Selfhost has diagnostics that carry no numeric code.
    uncoded_source = Path(__file__).resolve().parent / "selfhost" / "driver_warning.zan"
    result = run(gen1, str(uncoded_source), "--stdlib-path", str(stdlib), "-o", str(output), JSONL)
    assert result.returncode == 0 and output.is_file(), result
    uncoded, = records(result)
    assert uncoded["severity"] == "warning" and uncoded["code"] is None, uncoded
    assert "potential strong reference cycle" in uncoded["message"], uncoded


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(f"Usage: {Path(sys.argv[0]).name} <path-to-gen1>")
    scratch = Path(__file__).resolve().parents[1] / "_scratch"
    scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="selfhost-diagnostics-", dir=scratch) as temp:
        check(str(Path(sys.argv[1]).resolve()), Path(temp))
    print("selfhost diagnostics wire contract passed")
