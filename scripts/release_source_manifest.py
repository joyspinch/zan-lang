#!/usr/bin/env python3
"""Bind an explicitly observed build to non-ignored Git worktree bytes.

capture runs under the build lock AFTER configure and BEFORE build; seal runs
only after that build succeeds. verify runs before/after release tests. This is
an observational identity, not proof of a hermetic build: ignored dependencies
are covered ONLY by named native inputs and configured compiler/linker/build
program files. It does not traverse ignored toolchain/build trees.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile

SCHEMA = "release-build-identity-v1"
SCOPE = "git-tracked-plus-nonignored-untracked; ignored dependencies only as explicitly named native/tool files"


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, ensure_ascii=True,
                                     separators=(",", ":")).encode()).hexdigest()


def git(repo, *args):
    result = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, check=True)
    return result.stdout


def file_identity(path):
    path = Path(path)
    if not path.exists() and not path.is_symlink():
        return {"kind": "missing"}
    before = path.stat()
    if not stat.S_ISREG(before.st_mode):
        raise ValueError("Expected a regular input file: %s" % path)
    sha = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            sha.update(block)
    after = path.stat()
    if (before.st_size, before.st_mtime_ns, before.st_ino) != (
            after.st_size, after.st_mtime_ns, after.st_ino):
        raise ValueError("Input changed while hashing: %s" % path)
    result = {"kind": "file", "sha256": sha.hexdigest(), "size": after.st_size}
    if path.is_symlink():
        result.update(kind="symlink", target=os.readlink(path), resolved=str(path.resolve()))
    return result


def required_identity(path):
    identity = file_identity(path)
    if identity["kind"] == "missing":
        raise ValueError("Required input is missing: %s" % path)
    return {"path": str(Path(path).absolute()), **identity}


SDK_NATIVE_OWNERS = {
    "packages/Zan.Gui/src/Gui/drivers/win-x64": "zan_gui",
    "packages/Zan.Drawing/src/System/Drawing/Imaging/drivers/win-x64": "zan_image",
    "packages/Zan.Desktop/src/System/Media/drivers/win-x64": "zan_audio",
    "packages/Zan.Game/src/Game/Graphics/drivers/win-x64": "zan_game",
}


def sdk_native_outputs(repo):
    # Exact products of scripts/build_gui_driver.ps1, never caller-selected
    # source exclusions. Third-party payloads (e.g. WebView2Loader) stay inputs.
    return sorted(str((repo / parent / name).resolve())
                  for parent, driver in SDK_NATIVE_OWNERS.items()
                  for name in (driver + ".dll", "lib" + driver + ".dll.a", driver + ".bundle", driver + ".def",
                               "static/lib" + driver + ".a", "static/" + driver + ".libs"))


def source_snapshot(repo, generated=()):
    actual = Path(os.fsdecode(git(repo, "rev-parse", "--show-toplevel")).strip()).resolve()
    if actual != repo:
        raise ValueError("Source root must be the Git worktree root")
    paths = sorted(set(git(repo, "ls-files", "--cached", "--others", "--exclude-standard", "-z").split(b"\0")) - {b""})
    entries = []
    excluded = set(generated)
    for raw in paths:
        name = os.fsdecode(raw)
        if str((repo / name).resolve()) in excluded:
            continue
        entries.append({"path": name, **file_identity(repo / name)})
    exclusions = [":(exclude)" + Path(p).relative_to(repo).as_posix() for p in generated]
    return {"root": str(repo), "revision": git(repo, "rev-parse", "HEAD").decode().strip(),
            "dirty": bool(git(repo, "status", "--porcelain=v1", "-z", "--untracked-files=all", "--", ".", *exclusions)),
            "scope": SCOPE, "source_manifest_sha256": digest(entries), "entries": entries}


def cache_values(build):
    cache = build / "CMakeCache.txt"
    values = {}
    for line in cache.read_text(encoding="utf-8").splitlines():
        if line and not line.startswith(("#", "//")) and ":" in line and "=" in line:
            key, value = line.split("=", 1)
            values[key.split(":", 1)[0]] = value
    return values


def inputs(repo, build, native, generated=()):
    values = cache_values(build)
    if Path(values.get("CMAKE_HOME_DIRECTORY", "")).resolve() != repo:
        raise ValueError("CMAKE_HOME_DIRECTORY does not identify the source worktree")
    tools = []
    for key in ("CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER", "CMAKE_LINKER", "CMAKE_MAKE_PROGRAM"):
        if values.get(key):
            path = Path(values[key])
            if not path.is_absolute():
                resolved = shutil.which(str(path))
                if not resolved:
                    raise ValueError("Cannot resolve configured tool: %s" % values[key])
                path = Path(resolved)
            tools.append({"cache_key": key, **required_identity(path)})
    if not any(t["cache_key"] == "CMAKE_C_COMPILER" for t in tools):
        raise ValueError("CMAKE_C_COMPILER identity is required")
    return {"source": source_snapshot(repo, generated), "cache": required_identity(build / "CMakeCache.txt"),
            "tools": tools, "native_inputs": [required_identity(p) for p in native]}


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", newline="\n",
                                         dir=path.parent, delete=False) as stream:
            temporary = Path(stream.name)
            json.dump(value, stream, sort_keys=True, ensure_ascii=True, indent=2)
            stream.write("\n")
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def read_record(path):
    record = json.loads(path.read_text(encoding="utf-8"))
    if record.get("schema") != SCHEMA:
        raise ValueError("Unsupported build identity schema")
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("capture", "seal", "verify"))
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--native-input", action="append", type=Path, default=[])
    parser.add_argument("--artifact", action="append", type=Path, default=[])
    parser.add_argument("--native-input-list", type=Path)
    parser.add_argument("--artifact-list", type=Path)
    parser.add_argument("--receipt", type=Path)
    parser.add_argument("--sdk-native-outputs", action="store_true",
                        help="Capture the fixed repo-defined native driver outputs as artifacts, including tracked ones")
    args = parser.parse_args()
    repo, build = args.repo.resolve(), args.build.resolve()
    pending = build / "test-out/release-build-pending.json"
    identity = build / "test-out/release-build-identity.json"
    # Invalidate old evidence before reading external path lists: malformed or
    # missing transport files are failed invocations, not usable old builds.
    if args.mode in ("capture", "seal"):
        identity.unlink(missing_ok=True)
    if args.mode == "capture":
        pending.unlink(missing_ok=True)
    for flag, destination in ((args.native_input_list, args.native_input), (args.artifact_list, args.artifact)):
        if flag:
            values = json.loads(flag.read_text(encoding="utf-8-sig"))
            if not isinstance(values, list) or any(not isinstance(p, str) or not p for p in values):
                raise ValueError("Identity path list must be a JSON array of nonempty file paths")
            destination.extend(Path(p) for p in values)
    if args.mode == "capture":
        native = sorted({str(p.resolve()) for p in args.native_input})
        generated = sdk_native_outputs(repo) if args.sdk_native_outputs else []
        if set(native) & set(generated):
            raise ValueError("Generated SDK outputs cannot also be native inputs")
        record = {"schema": SCHEMA, "phase": "before-build", "build": str(build),
                  "native_paths": native, "generated_outputs": generated,
                  "inputs": inputs(repo, build, native, generated)}
        atomic_json(pending, record)
    else:
        record = read_record(pending if args.mode == "seal" else identity)
        expected_phase = "before-build" if args.mode == "seal" else "after-successful-build"
        if record.get("phase") != expected_phase or record.get("build") != str(build):
            raise ValueError("Build identity phase or build directory mismatch")
        generated = record.get("generated_outputs", [])
        if generated and generated != sdk_native_outputs(repo):
            raise ValueError("Generated SDK output declaration differs from the fixed repository rules")
        if args.sdk_native_outputs and generated != sdk_native_outputs(repo):
            raise ValueError("Generated SDK outputs were not declared before build")
        current = inputs(repo, build, record["native_paths"], generated)
        if current != record["inputs"]:
            raise ValueError("Release source/cache/tool/native input drift; fresh observed build required")
        zanc = build / ("zanc.exe" if os.name == "nt" else "zanc")
        if args.mode == "seal":
            artifacts = sorted({str(zanc), *generated, *(str(p.resolve()) for p in args.artifact)})
            record.update(phase="after-successful-build",
                          artifacts=[required_identity(p) for p in artifacts])
            record["identity_sha256"] = digest(record)
            atomic_json(identity, record)
            pending.unlink(missing_ok=True)
        else:
            expected_hash = record.get("identity_sha256")
            if digest({k: v for k, v in record.items() if k != "identity_sha256"}) != expected_hash:
                raise ValueError("Build identity checksum mismatch")
            artifacts = [required_identity(p["path"]) for p in record["artifacts"]]
            if str(zanc) not in [p["path"] for p in artifacts] or artifacts != record["artifacts"]:
                raise ValueError("Release build artifact drift or missing zanc identity")
            if args.receipt:
                atomic_json(args.receipt, {"schema": SCHEMA, "phase": "gate-verified",
                            "identity_sha256": expected_hash, "inputs": current, "artifacts": artifacts})
    print("release source identity %s passed" % args.mode)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print("release provenance failed: %s" % error, file=sys.stderr)
        raise SystemExit(1)
