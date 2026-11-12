"""Direct-source IDE lifecycle regressions under the shared build gate.
python -I tests/ide_marketplace/lifecycle.py
Real delayed HTTP, independent ephemeral signature, isolated project install.
"""
from __future__ import annotations
import argparse
import base64
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def invoke(argv: list[str], log: Path, env: dict[str, str] | None = None,
           timeout: int = 180) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(argv, cwd=ROOT, env=env, capture_output=True,
                            text=True, encoding="utf-8", errors="replace", timeout=timeout)
    with log.open("a", encoding="utf-8") as stream:
        # Signature/key contents and environment are deliberately absent.
        stream.write(result.stdout + result.stderr + f"\nexit_code={result.returncode}\n")
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}); see {log}")
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gate-held", action="store_true")
    parser.add_argument("--case", action="append", choices=("idle", "active-update", "active-catalog", "active-archive",
        "install", "active-install", "reject-hash", "reject-signature"))
    options = parser.parse_args()
    if not options.gate_held:
        host = shutil.which("pwsh") or shutil.which("powershell.exe")
        if not host:
            raise RuntimeError("PowerShell shared build gate required")
        quote = lambda value: "'" + str(value).replace("'", "''") + "'"
        forwarded = [sys.executable, "-I", Path(__file__).resolve(), "--gate-held"]
        for mode in options.case or []:
            forwarded.extend(("--case", mode))
        command = "& " + " ".join(quote(value) for value in forwarded)
        env = dict(os.environ, ZAN_MARKETPLACE_LIFECYCLE_COMMAND=command)
        gate = "& " + quote(ROOT / "scripts/build_gate.ps1")
        subprocess.run([host, "-NoProfile", "-File", str(ROOT / "scripts/build_gate.ps1"), "-Status"], check=True)
        raise SystemExit(subprocess.run([host, "-NoProfile", "-Command", gate +
            " -Command $env:ZAN_MARKETPLACE_LIFECYCLE_COMMAND; exit $LASTEXITCODE"], env=env, cwd=ROOT).returncode)
    work = Path(tempfile.mkdtemp(prefix="ide-marketplace-lifecycle-", dir=ROOT / "build/test-out"))
    print(f"lifecycle evidence: {work}", flush=True)
    compiler = ROOT / "build/zanc.exe"
    relative = ["tests/ide_marketplace/MarketplaceLifecycleTests.zan",
        "src/ide_zan/src/services/MarketplaceInstall.zan", "src/ide_zan/src/services/IdeUpdate.zan",
        "src/ide_zan/src/services/MarketplaceClient.zan", "src/ide_zan/src/services/IdeFileSync.zan",
        "src/ide_zan/src/services/IdeUpdateInfo.zan", "src/ide_zan/src/services/IdeVersion.zan",
        "src/ide_zan/src/models/update/IdeSyncPlan.zan",
        "packages/Zan.Diagnostics/src/System/Diagnostics/ProcessHost.zan",
        "packages/Zan.Globalization/src/System/Globalization/Lang.zan"]
    relative += [f"src/ide_zan/src/models/marketplace/{name}.zan" for name in
                 ("MarketplacePackage", "MarketplacePlan", "MarketplaceVersion", "MarketplaceProtocol", "MarketplaceResponse")]
    sources = [ROOT / value for value in relative]
    identities = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in sources + [compiler]}
    executable = work / "lifecycle.exe"
    log = work / "compile.log"
    private = work / "fixture-private.pem"
    public = work / "fixture-public.pem"
    result = {"status": "started", "sourceCopied": False, "sourceHashes": identities, "cases": []}
    try:
        invoke([str(compiler), *map(str, sources), "--auto-stdlib", "--package-project", str(ROOT),
                "-o", str(executable)], log)
        openssl = shutil.which("openssl")
        if not openssl:
            raise RuntimeError("OpenSSL fixture generator required")
        invoke([openssl, "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", str(private)], work / "fixture.log")
        invoke([openssl, "pkey", "-in", str(private), "-pubout", "-out", str(public)], work / "fixture.log")
        artifact = work / "artifact.zip"
        with zipfile.ZipFile(artifact, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("zan.pkg", 'name = "Zan.LifecycleProbe"\nversion = "1.0.0"\n')
            archive.writestr("src/LifecycleProbe/Marker.zan", 'namespace LifecycleProbe;\npublic class Marker { public static int Value() { return 42; } }\n')
        payload = artifact.read_bytes()
        with zipfile.ZipFile(artifact) as archive:
            inventory = [{"path": name, "size": len(archive.read(name)),
                          "sha256": hashlib.sha256(archive.read(name)).hexdigest()}
                         for name in archive.namelist()]
        digest = hashlib.sha256(payload).hexdigest()
        message = work / "message.bin"
        message.write_bytes(("lifecycle-probe\n1.0.0\n" + digest).encode("ascii"))
        signature = work / "signature.bin"
        invoke([openssl, "dgst", "-sha256", "-sign", str(private), "-out", str(signature), str(message)], work / "fixture.log")
        signature_text = base64.b64encode(signature.read_bytes()).decode("ascii")
        manifest = {"appName": "zan-channel/ide/win-x64", "version": "1.0.0", "releaseDate": "2026-11-12",
            "packageUrl": "", "packageSize": len(payload), "packageMd5": "", "packageSha256": digest,
            "changelog": "", "files": inventory, "signature": ""}
        manifest_path = work / "update-manifest.json"
        manifest_path.write_text(json.dumps(manifest, separators=(",", ":")), encoding="utf-8")
        prepare_env = dict(os.environ, LIFECYCLE_MANIFEST=str(manifest_path), LIFECYCLE_ROOT=str(work))
        invoke([str(executable), "prepare-manifest"], work / "fixture.log", prepare_env, 30)
        invoke([openssl, "dgst", "-sha256", "-sign", str(private), "-out", str(signature),
                str(work / "manifest-payload.bin")], work / "fixture.log")
        manifest["signature"] = signature.read_bytes().hex()
        manifest_path.write_text(json.dumps(manifest, separators=(",", ":")), encoding="utf-8")
        private.unlink()
        for mode in options.case or ("idle", "active-update", "active-catalog", "active-archive", "install", "active-install", "reject-hash", "reject-signature"):
            case = work / mode
            case.mkdir()
            (case / "project").mkdir()
            (case / "project/zan.proj").write_text('name=LifecycleConsumer\ntype=console\n', encoding="utf-8")
            requests: list[str] = []
            class Handler(BaseHTTPRequestHandler):
                def do_GET(self) -> None:
                    requests.append(self.path)
                    (case / "request-seen").write_text(self.path, encoding="utf-8")
                    if mode in ("active-update", "active-catalog", "active-archive", "active-install"):
                        time.sleep(0.4)
                    body = payload if self.path == "/artifact.zip" or self.path.startswith("/api/channel/file?") else b'{"success":true,"data":{"available":false}}'
                    self.send_response(200)
                    self.send_header("Content-Type", "application/octet-stream"
                                     if self.path == "/artifact.zip" or self.path.startswith("/api/channel/file?")
                                     else "application/json")
                    self.send_header("Content-Length", str(len(body)))
                    self.send_header("Connection", "close")
                    self.end_headers()
                    self.wfile.write(body)
                def log_message(self, *args: object) -> None:
                    pass
            server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            env = dict(os.environ, ZAN_MALL_URL=f"http://127.0.0.1:{server.server_port}",
                ZAN_MALL_SIGNER_PUBLIC_KEY_FILE=str(public), ZAN_UPDATE_PUBLIC_KEY_FILE=str(public),
                LIFECYCLE_MANIFEST=str(manifest_path), LIFECYCLE_ROOT=str(case),
                LIFECYCLE_COMPILER=str(compiler), LIFECYCLE_STDLIB=str(ROOT / "stdlib"),
                LIFECYCLE_SHA256=digest, LIFECYCLE_SIGNATURE=signature_text)
            try:
                completed = invoke([str(executable), mode], case / "run.log", env, 30)
                if mode == "active-update" and (len(requests) != 1 or "channel=ide" not in requests[0]):
                    raise RuntimeError("close scheduled a chained channel request")
                if mode == "idle" and requests:
                    raise RuntimeError("idle close created a request")
                if "runtime error:" in completed.stdout + completed.stderr:
                    raise RuntimeError("native runtime reported an error despite exit0")
                result["cases"].append({"name": mode, "exitCode": 0, "requests": len(requests)})
                print(f"PASS {mode}: requests={len(requests)}", flush=True)
            finally:
                server.shutdown()
                server.server_close()
                thread.join(timeout=3)
        if any(hashlib.sha256(Path(path).read_bytes()).hexdigest() != sha for path, sha in identities.items()):
            raise RuntimeError("source/compiler identity changed during regression")
        result["status"] = "passed"
    except BaseException as error:
        result["status"] = "failed"
        result["error"] = str(error)
        raise
    finally:
        private.unlink(missing_ok=True)
        result["privateKeyRemoved"] = not private.exists()
        (work / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
