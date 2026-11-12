"""Synthetic release provenance and gate regression; never builds or runs CTest."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

REQUIRED = ["selfhost_fixed_point", "selfhost_diff", "selfhost_production_diff",
            "selfhost_gen1", "selfhost_ir_golden", "selfhost_exception_ownership"]


def fake_ctest(arguments):
    build = Path(arguments[arguments.index("--test-dir") + 1])
    state = json.loads((build / "fake-state.json").read_text())
    if "--show-only=json-v1" in arguments:
        names = [n for n in REQUIRED if n != state.get("missing")] + ["sentinel"]
        print(json.dumps({"tests": [{"name": n, "properties": [
            {"name": "LABELS", "value": ["full"]}]} for n in names]}))
        return 0
    if state.get("drift"):
        Path(state["drift"]).write_text("changed during fake test")
    if state.get("swap_identity"):
        target = build / "test-out/release-build-identity.json"
        record = json.loads(target.read_text())
        artifact = Path(record["artifacts"][0]["path"])
        artifact.write_bytes(b"different synthetic accepted build")
        record["artifacts"][0].update(sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(),
                                       size=artifact.stat().st_size)
        record.pop("identity_sha256")
        record["identity_sha256"] = hashlib.sha256(json.dumps(
            record, sort_keys=True, ensure_ascii=True, separators=(",", ":")).encode()).hexdigest()
        target.write_text(json.dumps(record))
    report = Path(arguments[arguments.index("--output-junit") + 1])
    report.write_text('<testsuite skipped="1"><testcase><skipped/></testcase></testsuite>'
                      if state.get("skip") else '<testsuite skipped="0"><testcase/></testsuite>')
    return 1 if state.get("fail") else 0


def run(args, success=True, contains=None):
    result = subprocess.run([str(a) for a in args], capture_output=True, text=True, timeout=60)
    combined = result.stdout + result.stderr
    if (result.returncode == 0) != success or (contains and contains not in combined):
        raise AssertionError("Unexpected command result: %s\n%s" % (args, combined))
    return result


def main():
    if "--fake-ctest" in sys.argv:
        return fake_ctest(sys.argv[sys.argv.index("--fake-ctest") + 1:])
    parser = argparse.ArgumentParser()
    parser.add_argument("--gate", type=Path, required=True)
    parser.add_argument("--test-dir", type=Path, required=True)
    parser.add_argument("--cmake", required=True)
    options = parser.parse_args()
    # Every protocol entry also checks the real three-platform CI observation
    # chains; this launches only static tests, never a workflow or external CI.
    run([sys.executable, "-I", "-B", Path(__file__).with_name("test_release_workflow_identity.py")])
    tool = options.gate.resolve().parent / "release_source_manifest.py"
    options.test_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="release-source-fixture-", dir=options.test_dir) as directory:
        root = Path(directory)
        repo = root / "source with spaces"
        build = repo / "build"
        build.mkdir(parents=True)
        source = repo / "source.zan"
        source.write_text("tracked original")
        deleted = repo / "deleted.zan"
        deleted.write_text("will be deleted")
        (repo / ".gitignore").write_text("build/\nignored/\n")
        run(["git", "init", repo])
        run(["git", "-C", repo, "add", "source.zan", "deleted.zan", ".gitignore"])
        run(["git", "-C", repo, "-c", "user.name=fixture", "-c", "user.email=fixture@example.invalid",
             "commit", "-m", "synthetic fixture"])
        # Dirty tracked content, missing tracked content and untracked source
        # must all be represented even though HEAD remains unchanged.
        source.write_text("dirty original")
        deleted.unlink()
        extra = repo / "new source.zan"
        extra.write_text("untracked original")
        native = build / "named-native.bin"
        native.write_bytes(b"synthetic native input")
        compiler = build / "fake-compiler.bin"
        compiler.write_bytes(b"synthetic compiler tool")
        artifact = build / ("zanc.exe" if os.name == "nt" else "zanc")
        artifact.write_bytes(b"synthetic artifact; not a real compiler")
        cache = build / "CMakeCache.txt"
        original_cache = ("ZAN_REQUIRE_CROSS_RUNTIME:BOOL=ON\nCMAKE_HOME_DIRECTORY:INTERNAL=%s\n"
                          "CMAKE_C_COMPILER:FILEPATH=%s\n" % (repo.as_posix(), compiler.as_posix()))
        cache.write_text(original_cache)
        command = [sys.executable, "-I", "-B", tool]
        common = ["--repo", repo, "--build", build]
        def capture():
            run(command + ["capture"] + common + ["--native-input", native])
        def seal():
            run(command + ["seal"] + common)
        def verify(success=True):
            return run(command + ["verify"] + common, success)
        capture()
        pending = json.loads((build / "test-out/release-build-pending.json").read_text())
        entries = {e["path"]: e for e in pending["inputs"]["source"]["entries"]}
        assert entries["deleted.zan"]["kind"] == "missing"
        assert entries["source.zan"]["sha256"] == hashlib.sha256(source.read_bytes()).hexdigest()
        assert "new source.zan" in entries and pending["inputs"]["source"]["dirty"]
        seal()
        verify()
        identity_path = build / "test-out/release-build-identity.json"
        # Ignored unrelated files do not create a false claim of exhaustive
        # dependency coverage and are deliberately not recursively scanned.
        ignored = repo / "ignored"
        ignored.mkdir()
        (ignored / "unrelated.bin").write_bytes(b"not an observed build input")
        verify()
        mutations = [(source, b"dirty changed"), (extra, b"untracked changed"),
                     (native, b"native changed"), (compiler, b"tool changed"),
                     (artifact, b"artifact changed"), (cache, original_cache.encode() + b"# changed\n")]
        for path, replacement in mutations:
            original = path.read_bytes()
            path.write_bytes(replacement)
            verify(False)
            path.write_bytes(original)
        added = repo / "another.zan"
        added.write_text("added")
        verify(False)
        added.unlink()
        extra.unlink()
        verify(False)
        extra.write_text("untracked original")
        # Failure during seal invalidates prior successful evidence.
        capture()
        assert not identity_path.exists()
        source.write_text("changed during synthetic build")
        run(command + ["seal"] + common, False, "input drift")
        assert not identity_path.exists()
        source.write_text("dirty original")
        capture()
        seal()
        state = build / "fake-state.json"
        state.write_text("{}")
        gate_command = [options.cmake, "-DBUILD_DIR=" + str(build), "-DSOURCE_ROOT=" + str(repo),
                        "-DPYTHON_COMMAND=" + sys.executable, "-DCTEST_COMMAND=" + sys.executable,
                        "-DCTEST_ARGUMENTS=-I;-B;" + str(Path(__file__).resolve()) + ";--fake-ctest",
                        "-P", options.gate]
        run(gate_command, contains="Release quality gate passed")
        after_receipt = build / "test-out/release-source-after.json"
        assert after_receipt.exists()
        for required in ("selfhost_diff", "selfhost_production_diff", "selfhost_exception_ownership"):
            state.write_text(json.dumps({"missing": required}))
            run(gate_command, False, "Required release tests are missing")
            assert not after_receipt.exists()
        for key, expected in (("fail", "Full release gate failed"), ("skip", "Release tests were skipped")):
            state.write_text(json.dumps({key: True}))
            run(gate_command, False, expected)
        state.write_text(json.dumps({"drift": str(source)}))
        run(gate_command, False, "provenance unavailable or changed")
        assert not after_receipt.exists()
        source.write_text("dirty original")
        state.write_text(json.dumps({"swap_identity": True}))
        run(gate_command, False, "identity changed during validation")
        assert not after_receipt.exists()
        state.write_text("{}")
        identity_path.unlink()
        run(gate_command, False, "provenance unavailable or changed")
        capture()
        seal()
        cache.write_text(original_cache.replace("BOOL=ON", "BOOL=OFF"))
        run(gate_command, False, "ZAN_REQUIRE_CROSS_RUNTIME=ON")
        cache.write_text(original_cache)
        # Wrong configured root must be rejected before capture can be sealed.
        cache.write_text(original_cache.replace(repo.as_posix(), root.as_posix()))
        run(command + ["capture"] + common, False, "CMAKE_HOME_DIRECTORY")
        assert not identity_path.exists()
    print("release quality gate regression passed (synthetic provenance and fake processes only)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
