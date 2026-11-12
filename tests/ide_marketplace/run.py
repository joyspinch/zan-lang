"""Parent-owned serial runner: compile production marketplace models and run regressions.
Usage: python -I tests/ide_marketplace/run.py [--compiler build/zanc.exe]
The default entry holds scripts/build_gate.ps1's shared 'build' lease for the whole run.
A parent already holding that SAME lease must pass --gate-held to avoid nesting.
No GUI; OpenSSL supplies independent ephemeral RSA fixtures. No package/SDK copies.
"""
from __future__ import annotations

import argparse
import base64
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def run(command: list[str], cwd: Path, log: Path,
        timeout: int = 180) -> subprocess.CompletedProcess[str]:
    with log.open("a", encoding="utf-8") as stream:
        stream.write("command=" + json.dumps(command, ensure_ascii=False) + "\n")
        stream.flush()
        try:
            result = subprocess.run(command, cwd=cwd, text=True, encoding="utf-8", errors="replace",
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
        except subprocess.TimeoutExpired as error:
            output = error.stdout or b""
            if isinstance(output, bytes):
                output = output.decode("utf-8", errors="replace")
            stream.write(output + "\nTIMEOUT\n")
            raise
        except Exception as error:
            stream.write(f"launch_error={error}\n")
            raise
        stream.write(result.stdout)
        stream.write(f"\nexit_code={result.returncode}\n")
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}): {command[0]}; log: {log}\n{result.stdout}")
    return result


def powershell_literal(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def under_shared_gate(root: Path, compiler: Path) -> int:
    host = (shutil.which("powershell.exe") or shutil.which("pwsh.exe")
            or shutil.which("pwsh") or shutil.which("powershell"))
    if not host:
        raise SystemExit("PowerShell is required to acquire the SDK shared build gate; no build was started.")
    # Pass exact argv as PowerShell single-quoted literals, never through a shell.
    # The outer gate owns the lease until compilation AND execution have finished.
    child = [sys.executable, "-I", str(Path(__file__).resolve()), "--gate-held",
             "--compiler", str(compiler)]
    command = "& " + " ".join(powershell_literal(arg) for arg in child)
    environment = dict(os.environ)
    environment["ZAN_IDE_MARKETPLACE_GATE_COMMAND"] = command
    gate = root / "scripts" / "build_gate.ps1"
    script = ("& " + powershell_literal(str(gate))
              + " -Name 'build' -Command $env:ZAN_IDE_MARKETPLACE_GATE_COMMAND; exit $LASTEXITCODE")
    # Inherit output: scheduler queue/lease messages remain visible while waiting.
    return subprocess.run([host, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script],
                          cwd=root, env=environment).returncode


def main() -> None:
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, default=root / "build" / "zanc.exe")
    parser.add_argument("--gate-held", action="store_true",
                        help="only for a parent already holding scripts/build_gate.ps1 -Name build")
    options = parser.parse_args()
    compiler = options.compiler.resolve()
    if not options.gate_held:
        raise SystemExit(under_shared_gate(root, compiler))
    openssl = shutil.which("openssl")
    output_root = root / "build" / "test-out"
    output_root.mkdir(parents=True, exist_ok=True)
    models = root / "src" / "ide_zan" / "src" / "models" / "marketplace"
    sources = [root / "tests" / "ide_marketplace" / "MarketplaceProtocolTests.zan"]
    sources += [models / (name + ".zan") for name in (
        "MarketplacePackage", "MarketplacePlan", "MarketplaceVersion",
        "MarketplaceResponse", "MarketplaceProtocol")]
    work = Path(tempfile.mkdtemp(prefix="ide-marketplace-", dir=output_root))
    artifacts = work / "artifacts"
    artifacts.mkdir()
    log = work / "run.log"
    log.write_text("status=started\nshared_build_gate=held_by_parent\n", encoding="utf-8")
    print(f"marketplace evidence: {work}", flush=True)
    public_keys: list[Path] = []
    private_keys: list[Path] = []
    failure: str | None = None
    try:
        if not openssl:
            raise RuntimeError("SKIP: OpenSSL required for independent RSA fixtures (not validated).")
        if not compiler.is_file():
            raise RuntimeError(f"compiler missing: {compiler}")
        for name in ("trusted", "other"):
            private = artifacts / (name + ".private.pem")
            public = work / (name + ".public.pem")
            private_keys.append(private)  # Track before generation, including partial failures.
            run([openssl, "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048",
                 "-out", str(private)], root, log)
            run([openssl, "pkey", "-in", str(private), "-pubout", "-out", str(public)], root, log)
            public_keys.append(public)
        message = work / "message.bin"
        message.write_bytes(b"widget\n1.2.3\n" + b"0123456789abcdef" * 4)
        raw_signature = work / "signature.bin"
        run([openssl, "dgst", "-sha256", "-sign", str(private_keys[0]),
             "-out", str(raw_signature), str(message)], root, log)
        signature = work / "signature.base64"
        signature.write_bytes(base64.b64encode(raw_signature.read_bytes()))
        for private in private_keys:
            private.unlink(missing_ok=True)
        executable = artifacts / ("marketplace-tests.exe" if compiler.suffix == ".exe" else "marketplace-tests")
        result = run([str(compiler), *map(str, sources), "--auto-stdlib", "--stdlib-path",
                      str(root / "stdlib"), "--package-project", str(root), "-o", str(executable)], root, log)
        if result.stdout:
            print(result.stdout, end="")
        result = run([str(executable), str(public_keys[0]), str(signature), str(public_keys[1])], root, log)
        print(result.stdout, end="")
    except BaseException as error:
        failure = f"{type(error).__name__}: {error}"
        raise
    finally:
        cleanup_errors: list[str] = []
        for private in private_keys:
            try:
                private.unlink(missing_ok=True)
            except OSError as error:
                cleanup_errors.append(f"private key cleanup failed: {error}")
        try:
            shutil.rmtree(artifacts)
        except OSError as error:
            cleanup_errors.append(f"artifact cleanup failed: {error}")
        with log.open("a", encoding="utf-8") as stream:
            if failure:
                stream.write("failure=" + failure + "\n")
            for error in cleanup_errors:
                stream.write(error + "\n")
            stream.write("private_keys_removed=" + str(not any(p.exists() for p in private_keys)) + "\n")
            stream.write("status=" + ("failed" if failure or cleanup_errors else "passed") + "\n")
        if cleanup_errors:
            raise RuntimeError("; ".join(cleanup_errors) + f"; log: {log}")


if __name__ == "__main__":
    main()
