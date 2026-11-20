"""Check the C compiler diagnostic wire format against an already built zanc."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile


JSONL = "--diagnostics-format=jsonl"
FORMAT = "zan-diagnostic-v1"


def run(zanc, *args):
    return subprocess.run([zanc, *args], capture_output=True, timeout=30, check=False)


def records(result):
    diagnostics = []
    for line in result.stderr.splitlines():
        if line.startswith(b"{"):
            record = json.loads(line.decode("utf-8"))
            assert record["format"] == FORMAT, record
            diagnostics.append(record)
    assert FORMAT.encode() not in result.stdout, result.stdout
    return diagnostics


def check(zanc, work):
    help_result = run(zanc, "--help")
    assert help_result.returncode == 0, help_result.stderr
    assert any(line.split()[:1] == [JSONL.encode()]
               for stream in (help_result.stdout, help_result.stderr)
               for line in stream.splitlines()), help_result.stderr

    invalid = run(zanc, "--diagnostics-format=unknown")
    assert invalid.returncode != 0 and b"--diagnostics-format" in invalid.stderr, invalid
    assert not records(invalid), invalid.stderr

    source = work / "诊断 test.zan"
    payload = '"quoted" C:\\work\t中\U0001f642\x01'
    warning = f"#warning {payload}"
    first_error = "#error first"
    source.write_bytes(("#pragma warning disable W0003\n"
                        "#warning hidden\n"
                        "#pragma warning restore W0003\n"
                        f"{warning}\n{first_error}\n#error second\n").encode("utf-8"))

    # Keep namespace scanning enabled with an empty stdlib to exercise the
    # real parse and its dependency-discovery fallback without building anything.
    stdlib = work / "stdlib"
    stdlib.mkdir()
    compile_args = (str(source), "--stdlib-path", str(stdlib), "--no-packages")
    expected_warning = {
        "format": FORMAT, "severity": "warning", "file": str(source),
        "line": 4, "col": len(warning.encode("utf-8")) + 1,
        "code": "W0003", "message": warning,
    }
    expected_first_error = {
        "format": FORMAT, "severity": "error", "file": str(source),
        "line": 5, "col": len(first_error) + 1,
        "code": "E0002", "message": first_error,
    }
    expected_second_error = dict(expected_first_error, line=6, col=len("#error second") + 1,
                                 message="#error second")

    result = run(zanc, *compile_args, JSONL)
    assert result.returncode == 1, result
    assert records(result) == [expected_warning, expected_first_error, expected_second_error], result.stderr
    assert b"2 error(s), 1 warning(s)" in result.stderr, result.stderr
    assert b"\x1b" not in result.stderr and b"[W0003]" not in result.stderr, result.stderr

    for flags in ((), (JSONL, "--diagnostics-format=text")):
        text = run(zanc, *compile_args, *flags)
        assert text.returncode == 1 and not records(text), text
        assert b"[W0003] #warning" in text.stderr, text.stderr
        assert b"[E0002] #error first" in text.stderr, text.stderr

    for flag in ("--warn-level=none", "--suppress=W0003"):
        suppressed = run(zanc, *compile_args, JSONL, flag)
        assert suppressed.returncode == 1, suppressed
        assert records(suppressed) == [expected_first_error, expected_second_error], suppressed.stderr

    # Warning promotion precedes suppression, including #pragma suppression.
    for flag in ("--deny-warnings", "--warn-level=error"):
        denied = run(zanc, *compile_args, JSONL, flag, "--suppress=W0003")
        assert denied.returncode == 1, denied
        hidden = dict(expected_warning, severity="error", line=2,
                      col=len("#warning hidden") + 1, message="#warning hidden")
        promoted = dict(expected_warning, severity="error")
        assert records(denied) == [hidden, promoted, expected_first_error, expected_second_error], denied.stderr

    limited = run(zanc, *compile_args, JSONL, "-ferror-limit=1")
    assert limited.returncode == 1, limited
    warning_record, error, limit = records(limited)
    assert warning_record == expected_warning and error == expected_first_error, limited.stderr
    assert limit == {
        "format": FORMAT, "severity": "note", "file": None, "line": 0, "col": 0,
        "code": None, "message": "too many errors, stopping after 1 "
        "(further errors are suppressed; use -ferror-limit=0 for all)",
    }, limit
    assert b"1 further error(s) were suppressed" in limited.stderr, limited.stderr

    source.write_bytes(b"`" * 16 + b"\n")
    cascaded = run(zanc, *compile_args, JSONL, "-ferror-limit=0")
    assert cascaded.returncode == 1, cascaded
    diagnostics = records(cascaded)
    assert sum(d["severity"] == "error" and d["line"] == 1 for d in diagnostics) == 8, diagnostics
    assert [d for d in diagnostics if d["severity"] == "note"] == [{
        "format": FORMAT, "severity": "note", "file": str(source), "line": 1,
        "col": 0, "code": None,
        "message": "line 1: more than 8 errors, rest of the line suppressed",
    }], diagnostics

    # Precompiled inputs are skipped by the parser and do not allocate file IDs.
    primary = work / "primary.zan"
    primary.write_text("class Program { static void Main() {} }\n", encoding="utf-8")
    compiled = work / "precompiled.zcomp"
    compiled.touch()
    source.write_bytes(b"#error invalid UTF-8: \xff\n")
    malformed = run(zanc, str(primary), str(compiled), *compile_args, JSONL)
    assert malformed.returncode == 1, malformed
    error, = records(malformed)
    assert error["file"] == str(source), error
    assert error["message"] == "#error invalid UTF-8: \ufffd", error


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(f"Usage: {Path(sys.argv[0]).name} <path-to-zanc>")
    scratch = Path(__file__).resolve().parents[1] / "_scratch"
    scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="zanc-diagnostics-", dir=scratch) as temp:
        check(str(Path(sys.argv[1]).resolve()), Path(temp))
    print("zanc diagnostics wire contract passed")
