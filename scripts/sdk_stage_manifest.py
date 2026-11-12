#!/usr/bin/env python3
"""SDK inventory and consumption evidence, extending the observed-build identity.

No build, CTest, lock or scheduler acquisition happens in this helper. Run all
compilation/consumption under the caller's existing shared build gate.
"""
import argparse
import importlib.util
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys

_spec = importlib.util.spec_from_file_location("release_source_manifest", Path(__file__).with_name("release_source_manifest.py"))
identity = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(identity)
SCHEMA = "zan-sdk-stage-v1"
EXCLUDED_DIRS = {".build", "build", "_scratch", "bin", "obj", "logs", ".git", ".github", "__pycache__"}
EXCLUDED_FILES = {".gitignore", ".gitattributes", ".DS_Store", "Thumbs.db"}
EXCLUDED_SUFFIXES = {".log", ".tmp", ".bak", ".pdb", ".ilk", ".exp", ".pyc"}


GENERATED_DRIVERS = identity.SDK_NATIVE_OWNERS


def generated_native(path, repo):
    """Exact outputs of build_gui_driver.ps1; third-party DLLs stay inputs."""
    return str(Path(path).resolve()) in identity.sdk_native_outputs(repo)


def load(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def safe_relative(name):
    name = str(name).replace("\\", "/")
    path = Path(name)
    if path.is_absolute() or not name or any(p in ("", ".", "..") for p in name.split("/")) or ":" in name:
        raise ValueError("Unsafe SDK manifest path: " + name)
    return name


def files(root, filtered=False):
    root = Path(root)
    if not root.is_dir():
        raise ValueError("Required SDK directory missing: %s" % root)
    for parent, dirs, names in os.walk(root, followlinks=False):
        for name in list(dirs):
            child = Path(parent) / name
            if filtered and name in EXCLUDED_DIRS:
                dirs.remove(name)
            elif child.is_symlink() or (hasattr(child, "is_junction") and child.is_junction()):
                raise ValueError("Reparse/symlink input directory is not supported: %s" % child)
        for name in sorted(names):
            child = Path(parent) / name
            if filtered and (name in EXCLUDED_FILES or child.suffix in EXCLUDED_SUFFIXES):
                continue
            if child.is_symlink():
                raise ValueError("Symlink SDK payload is not supported: %s" % child)
            yield child


def inventory(root):
    return [{"path": p.relative_to(root).as_posix(), **identity.file_identity(p)}
            for p in sorted(files(root)) if p not in (root / "sdk-manifest.json", root / "sdk-consumption.json")]


def verify_build(repo, build, local):
    subprocess.run([sys.executable, "-I", "-B", str(Path(__file__).with_name("release_source_manifest.py")),
                    "verify", "--repo", str(repo), "--build", str(build)], check=True)
    record = identity.read_record(build / "test-out/release-build-identity.json")
    if not local:
        before = load(build / "test-out/release-source-before.json")
        after = load(build / "test-out/release-source-after.json")
        expected = {"schema": identity.SCHEMA, "phase": "gate-verified",
                    "identity_sha256": record["identity_sha256"], "inputs": record["inputs"],
                    "artifacts": record["artifacts"]}
        if before != expected or after != expected:
            raise ValueError("Release gate receipts do not bind this complete build identity")
    return record


def expand_plan(plan):
    repo = Path(plan.get("repo", Path(__file__).resolve().parents[1])).resolve()
    known = None
    mappings = list(plan.get("toolchain", []))
    mappings = [{"source": row["source"], "destination": "toolchain/" + safe_relative(row["destination"])} for row in mappings]
    for item in plan["copies"]:
        source = Path(item["source"]).resolve()
        destination = safe_relative(item["destination"])
        if item.get("tree"):
            if item.get("source_only") and known is None:
                known = {str((repo / os.fsdecode(p)).resolve()) for p in identity.git(repo, "ls-files", "--cached", "--others", "--exclude-standard", "-z").split(b"\0") if p}
            mappings += [{"source": str(p), "destination": destination + "/" + p.relative_to(source).as_posix()}
                         for p in files(source, filtered=True)
                         if not item.get("source_only") or str(p.resolve()) in known]
        else:
            mappings.append({"source": str(source), "destination": destination})
    result = {}
    for row in mappings:
        name = safe_relative(row["destination"])
        source = str(Path(row["source"]).resolve())
        # Explicit build outputs follow broad source trees and replace a stale
        # in-tree CLI (tools/zan-mcp.exe); only the final source is consumed.
        result[name] = source
    return [{"source": source, "destination": name} for name, source in sorted(result.items())]


def gallery_refs(gallery):
    entries = gallery.get("entries")
    if not isinstance(entries, list):
        raise ValueError("SDK gallery must contain an entries array")
    refs = {}
    for entry in entries:
        values = entry.get("files", [])
        if not isinstance(values, list):
            raise ValueError("SDK gallery files must be an array")
        for value in values:
            if not isinstance(value, str) or value != safe_relative(value):
                raise ValueError("Unsafe SDK gallery path: " + str(value))
            if any(part.endswith((".", " ")) for part in value.split("/")):
                raise ValueError("Aliased SDK gallery path: " + value)
            key = value.casefold()
            if key in refs and refs[key] != value:
                raise ValueError("Aliased SDK gallery path: " + value)
            refs[key] = value
    return list(refs.values())


def gallery_plan(plan, complete=False):
    mappings = expand_plan(plan)
    destinations = {}
    for row in mappings:
        key = row["destination"].casefold()
        if key in destinations and destinations[key]["destination"] != row["destination"]:
            raise ValueError("Aliased SDK destination: " + row["destination"])
        destinations[key] = row
    gallery = destinations.get("knowledge/gallery.json")
    if not gallery:
        return plan
    repo = Path(plan["repo"]).resolve()
    for name in gallery_refs(load(gallery["source"])):
        current = destinations.get(name.casefold())
        if current:
            if current["destination"] != name:
                raise ValueError("Aliased SDK gallery destination: " + name)
            identity.required_identity(current["source"])
            continue
        if not complete:
            raise ValueError("SDK gallery file is absent from payload mappings: " + name)
        source = repo / name
        if source.resolve() != source or not source.is_relative_to(repo):
            raise ValueError("SDK gallery source traverses an alias: " + name)
        identity.required_identity(source)
        plan["copies"].append({"source": str(source), "destination": name, "tree": False})
        destinations[name.casefold()] = {"source": str(source), "destination": name}
    return plan


def verify_gallery(sdk):
    path = sdk / "knowledge/gallery.json"
    if path.is_file():
        for name in gallery_refs(load(path)):
            target = sdk / name
            if target.resolve() != target or not target.is_relative_to(sdk):
                raise ValueError("SDK gallery source traverses an alias: " + name)
            identity.required_identity(target)


def preflight(repo, build, plan, local):
    record = verify_build(repo, build, local)
    covered = {}
    for item in record["inputs"]["source"]["entries"]:
        covered[str((repo / item["path"]).resolve())] = item
    for item in (record["artifacts"] + record["inputs"]["native_inputs"] + record["inputs"]["tools"]):
        covered[str(Path(item["path"]).resolve())] = item
    gallery_plan(plan)
    mappings = expand_plan(plan)
    for row in mappings:
        actual = identity.required_identity(row["source"])
        expected = covered.get(row["source"], {})
        if any(actual.get(k) != expected.get(k) for k in ("kind", "sha256", "size")):
            raise ValueError("SDK payload is not covered by observed build identity: " + row["source"])
        row["identity"] = {k: actual[k] for k in ("kind", "sha256", "size")}
    return {"schema": SCHEMA, "phase": "preflight", "build_identity_sha256": record["identity_sha256"],
            "validation": "local-build-only" if local else "release-gate-verified", "mappings": mappings}


def verify_sdk(sdk):
    manifest = load(sdk / "sdk-manifest.json")
    expected = manifest.get("manifest_sha256")
    if manifest.get("schema") != SCHEMA or identity.digest({k: v for k, v in manifest.items() if k != "manifest_sha256"}) != expected:
        raise ValueError("SDK manifest checksum/schema mismatch")
    if inventory(sdk) != manifest["files"]:
        raise ValueError("SDK payload changed, missing or unlisted files present")
    verify_gallery(sdk)
    return manifest


def consume(args):
    sdk = args.sdk.resolve()
    receipt = sdk / "sdk-consumption.json"
    receipt.unlink(missing_ok=True)
    manifest = verify_sdk(sdk)
    output = args.output.resolve()
    if output.exists():
        raise ValueError("Consumption output must be a new directory")
    root = Path(__file__).resolve().parents[1]
    command = [sys.executable, "-I", "-B", str(root / "tests/run_template_acceptance.py"),
               "--root", str(sdk), "--zanc", str(sdk / "toolchain/zanc.exe"),
               "--stdlib", str(sdk / "stdlib"), "--driver-dir", str(sdk / "toolchain"),
               "--output", str(output), "--filter", args.filter, "--workers", "1"]
    subprocess.run(command, check=True)
    results = load(output / "results.json")
    if not results.get("complete") or not results.get("selected") or any(row.get("status") != "pass" for row in results["rows"]):
        raise ValueError("SDK consumption did not execute all selected templates")
    runs = []
    for row in results["rows"]:
        if row["template"] != "console/console-args":
            continue
        artifact = Path(row["result"]["artifact"])
        token = "sdk-stage-consumption-probe"
        process = subprocess.run([str(artifact), token], input="", text=True, encoding="utf-8", errors="replace",
                                 capture_output=True, timeout=30, cwd=artifact.parent)
        log = output / "console-args-runtime.json"
        identity.atomic_json(log, {"exit_code": process.returncode, "stdout": process.stdout, "stderr": process.stderr})
        if process.returncode != 0 or token not in process.stdout:
            raise ValueError("Staged SDK console-args runtime did not preserve the supplied argument")
        runs.append({"template": row["template"], "artifact": identity.required_identity(artifact),
                     "log": identity.required_identity(log)})
    after = verify_sdk(sdk)
    if after != manifest:
        raise ValueError("SDK identity changed during consumption")
    identity.atomic_json(receipt, {"schema": SCHEMA, "phase": "templates-consumed",
        "manifest_sha256": manifest["manifest_sha256"], "build_identity_sha256": manifest["build_identity_sha256"],
        "validation": manifest["validation"], "command": command,
        "results": identity.required_identity(output / "results.json"), "selected": results["selected"], "runs": runs})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("gallery-plan", "native-inputs", "inputs", "copy-sources", "preflight", "seal", "verify", "consume"))
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build", type=Path)
    parser.add_argument("--plan", type=Path)
    parser.add_argument("--ticket", type=Path)
    parser.add_argument("--sdk", type=Path)
    parser.add_argument("--local-stage", action="store_true")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--filter", default="^console/console-args$")
    parser.add_argument("--native-dir", action="append", type=Path, default=[])
    parser.add_argument("--payload-native-dir", action="append", type=Path, default=[])
    parser.add_argument("--native-input", action="append", type=Path, default=[])
    args = parser.parse_args()
    if args.mode == "gallery-plan":
        if not args.plan:
            parser.error("gallery-plan requires --plan")
        identity.atomic_json(args.plan, gallery_plan(load(args.plan), complete=True))
    elif args.mode == "native-inputs":
        repo = args.repo.resolve()
        known = {str((repo / os.fsdecode(p)).resolve()) for p in identity.git(repo, "ls-files", "--cached", "--others", "--exclude-standard", "-z").split(b"\0") if p}
        paths = {str(p.resolve()) for directory in args.native_dir for p in files(directory.resolve(), filtered=True)
                 if str(p.resolve()) not in known and not generated_native(p, repo)}
        paths.update(str(p.resolve()) for directory in args.payload_native_dir for p in files(directory.resolve())
                     if str(p.resolve()) not in known and not generated_native(p, repo))
        paths.update(str(p.resolve()) for p in args.native_input)
        if any(generated_native(p, repo) for p in paths):
            raise ValueError("Project-generated native outputs must be sealed as artifacts, not captured as inputs")
        for path in paths:
            identity.required_identity(path)
        identity.atomic_json(args.output, {"native_inputs": sorted(paths)})
    elif args.mode == "copy-sources":
        plan = load(args.plan)
        plan["toolchain"] = []
        for row in expand_plan(plan):
            target = args.sdk / row["destination"]
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(row["source"], target)
    elif args.mode == "inputs":
        if not args.plan or not args.output or not args.build:
            parser.error("inputs requires --plan --output --build")
        repo, build = args.repo.resolve(), args.build.resolve()
        tracked = {str((repo / os.fsdecode(p)).resolve()) for p in identity.git(repo, "ls-files", "--cached", "--others", "--exclude-standard", "-z").split(b"\0") if p}
        native, artifacts = set(), set()
        for row in expand_plan(load(args.plan)):
            path = Path(row["source"]).resolve()
            if path.is_relative_to(build) or generated_native(path, repo):
                artifacts.add(str(path))
            elif str(path) not in tracked:
                native.add(str(path))
        identity.atomic_json(args.output, {"native_inputs": sorted(native), "artifacts": sorted(artifacts)})
    elif args.mode in ("preflight", "seal"):
        if not args.build or not args.plan or not args.ticket:
            parser.error("preflight/seal require --build --plan --ticket")
        ticket = preflight(args.repo.resolve(), args.build.resolve(), load(args.plan), args.local_stage)
        if args.mode == "preflight":
            identity.atomic_json(args.ticket, ticket)
        else:
            if ticket != load(args.ticket):
                raise ValueError("SDK inputs or build identity changed while staging")
            for row in ticket["mappings"]:
                actual = identity.file_identity(args.sdk / row["destination"])
                if actual != row["identity"]:
                    raise ValueError("SDK staged payload differs from observed source: " + row["destination"])
            verify_gallery(args.sdk.resolve())
            manifest = {"schema": SCHEMA, "phase": "staged", "validation": ticket["validation"],
                        "build_identity_sha256": ticket["build_identity_sha256"],
                        "version": (args.repo / "VERSION").read_text(encoding="utf-8-sig").strip(),
                        "files": inventory(args.sdk)}
            manifest["manifest_sha256"] = identity.digest(manifest)
            identity.atomic_json(args.sdk / "sdk-manifest.json", manifest)
    elif args.mode == "verify":
        verify_sdk(args.sdk.resolve())
    else:
        if not args.output:
            parser.error("consume requires --output")
        consume(args)
    print("SDK %s passed" % args.mode)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        print("SDK stage failed: %s" % error, file=sys.stderr)
        raise SystemExit(1)
