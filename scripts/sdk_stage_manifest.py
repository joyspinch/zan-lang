#!/usr/bin/env python3
"""SDK inventory, POSIX archives, and consumption evidence.

No toolchain build, CTest, lock or scheduler acquisition happens in this helper. Run all
compilation/consumption under the caller's existing shared build gate.
"""
import argparse
import gzip
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import unicodedata

_spec = importlib.util.spec_from_file_location("release_source_manifest", Path(__file__).with_name("release_source_manifest.py"))
identity = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(identity)
SCHEMA = "zan-sdk-stage-v1"
EXCLUDED_DIRS = {".build", "build", "_scratch", "bin", "obj", "logs", ".git", ".github", "__pycache__"}
EXCLUDED_FILES = {".gitignore", ".gitattributes", ".DS_Store", "Thumbs.db"}
EXCLUDED_SUFFIXES = {".log", ".tmp", ".bak", ".swp", ".pdb", ".ilk", ".exp", ".pyc"}


POSIX_SOURCE_DIRS = ("stdlib", "templates", "examples")
POSIX_COMMANDS = ("zanc", "zan-mvc", "zan-lsp", "zan-dap", "zanfmt", "zandoc", "llvm-ar")
POSIX_RUNTIME = ("zanrt_timer.o", "zanrt_io.o", "zanrt_io_mt.o", "zanrt_sync.o", "zanrt_file.o",
                 "zanrt_decimal.o", "zanrt_crash.o", "zanrt_mem.o", "zan_embed_api.o", "zan_inflate.o",
                 "libzancrypto.a")
CONSOLE_TEMPLATE = "console/console-args"

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


def stage_posix_sources(repo, sdk):
    root = Path(os.fsdecode(identity.git(repo, "rev-parse", "--show-toplevel")).rstrip("\r\n")).resolve()
    if root != repo:
        raise ValueError("POSIX SDK source root must be the Git worktree root")
    # Use the same file scope as the observed release build. Keep tracked
    # scaffold files and native assets even if they match an ignore rule.
    paths = identity.git(repo, "ls-files", "--cached", "--others", "--exclude-standard", "-z",
                         "--", "VERSION", *POSIX_SOURCE_DIRS)
    canonical_paths = {}
    for raw in sorted(set(paths.split(b"\0")) - {b""}):
        relative = os.fsdecode(raw)
        if safe_relative(relative) != relative:
            raise ValueError("Invalid SDK source path: " + relative)
        check_posix_path_alias(relative, canonical_paths)
        source = repo / relative
        if not stat.S_ISREG(source.lstat().st_mode) or source.resolve(strict=True) != source:
            raise ValueError("SDK source must be a regular file without symlink ancestors: " + relative)
        target = sdk / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(source, target)


def check_posix_path_alias(relative, canonical_paths):
    # Canonical caseless comparison also covers paths on case-insensitive APFS.
    canonical = unicodedata.normalize("NFD", unicodedata.normalize("NFD", relative).casefold())
    while relative:
        original = canonical_paths.setdefault(canonical, relative)
        if original != relative:
            raise ValueError(f"Aliased SDK archive paths: {original!r} and {relative!r}")
        # Tar members can imply directories without listing them explicitly.
        relative = relative.rpartition("/")[0]
        canonical = canonical.rpartition("/")[0]


def posix_archive_metadata(archive):
    match = re.fullmatch(r"zan-ide-([0-9A-Za-z][0-9A-Za-z.+-]*)-(linux|macos)-(x64|arm64)[.]tar[.]gz",
                         archive.name)
    if not match:
        raise ValueError("Invalid POSIX SDK archive name: " + archive.name)
    return match[1], match[2]


def verify_posix_layout(sdk, version):
    for name in POSIX_SOURCE_DIRS:
        if not (sdk / name).is_dir():
            raise ValueError("Required SDK directory missing: " + name)
    for name in ("VERSION", "README.txt", *("toolchain/" + name for name in POSIX_COMMANDS + POSIX_RUNTIME)):
        path = sdk / name
        if not path.is_file() or not path.stat().st_size:
            raise ValueError("Required SDK file missing or empty: " + name)
    if "".join((sdk / "VERSION").read_text(encoding="utf-8").split()) != version:
        raise ValueError("SDK VERSION does not match the archive name")


def posix_checksum(archive, record):
    return f"{record['sha256']}  {archive.name}\n".encode("ascii")


def write_posix_archive(sdk, archive):
    version, _ = posix_archive_metadata(archive)
    if sdk.name != archive.name.removesuffix(".tar.gz") or sdk.is_symlink():
        raise ValueError("SDK directory must match the archive name")
    verify_posix_layout(sdk, version)
    epoch = os.environ.get("SOURCE_DATE_EPOCH", "0")
    if not re.fullmatch(r"[0-9]+", epoch):
        raise ValueError("SOURCE_DATE_EPOCH must be a nonnegative integer")
    timestamp = int(epoch)
    paths = sorted(sdk.rglob("*"), key=lambda path: path.relative_to(sdk).as_posix())
    canonical_paths = {}
    with archive.open("xb") as stream, gzip.GzipFile(filename="", fileobj=stream, mode="wb",
                                                     mtime=0, compresslevel=9) as compressed:
        with tarfile.open(fileobj=compressed, mode="w|", format=tarfile.PAX_FORMAT) as tar:
            for path in (sdk, *paths):
                relative = path.relative_to(sdk.parent).as_posix()
                if safe_relative(relative) != relative:
                    raise ValueError("Invalid SDK archive member: " + relative)
                check_posix_path_alias(relative, canonical_paths)
                info = tarfile.TarInfo(relative)
                info.mtime = timestamp
                status = path.lstat()
                if stat.S_ISDIR(status.st_mode):
                    info.type, info.mode = tarfile.DIRTYPE, 0o755
                    tar.addfile(info)
                elif stat.S_ISREG(status.st_mode):
                    command = path.parent == sdk / "toolchain" and path.name in POSIX_COMMANDS
                    executable = status.st_mode & 0o111
                    if command and os.name == "posix" and not executable:
                        raise ValueError("SDK command is not executable: " + relative)
                    info.size = status.st_size
                    info.mode = 0o755 if command or executable else 0o644
                    with path.open("rb") as payload:
                        tar.addfile(info, payload)
                else:
                    raise ValueError("Unsupported SDK archive member: " + relative)
    archive.with_suffix(".gz.sha256").write_bytes(posix_checksum(archive, identity.file_identity(archive)))


def extract_posix_archive(archive, destination):
    version, _ = posix_archive_metadata(archive)
    record = identity.required_identity(archive)
    if archive.with_suffix(".gz.sha256").read_bytes() != posix_checksum(archive, record):
        raise ValueError("SDK archive checksum mismatch: " + archive.name)
    name = archive.name.removesuffix(".tar.gz")
    with tarfile.open(archive, "r:gz") as tar:
        members = tar.getmembers()
        seen = set()
        canonical_paths = {}
        for member in members:
            relative = member.name
            if (safe_relative(relative) != relative or relative.split("/", 1)[0] != name
                    or relative in seen or member.type not in (tarfile.REGTYPE, tarfile.DIRTYPE)
                    or (relative == name and not member.isdir())):
                raise ValueError("Invalid SDK archive member: " + relative)
            seen.add(relative)
            check_posix_path_alias(relative, canonical_paths)
        if name not in seen:
            raise ValueError("SDK archive root directory missing: " + name)
        for command in POSIX_COMMANDS:
            path = f"{name}/toolchain/{command}"
            if path in seen and not tar.getmember(path).mode & 0o111:
                raise ValueError("SDK command is not executable: " + path)
        tar.extractall(destination, members=members, filter="data")
    sdk = destination / name
    verify_posix_layout(sdk, version)
    return sdk, record


def run_sdk_probe_step(command, work, env, log, timeout):
    process = subprocess.run(command, input="", text=True, encoding="utf-8", errors="replace",
                             capture_output=True, timeout=timeout, cwd=work, env=env)
    identity.atomic_json(log, {"command": command, "exit_code": process.returncode,
                               "stdout": process.stdout, "stderr": process.stderr})
    if process.returncode != 0:
        raise ValueError(f"SDK probe failed ({process.returncode}); see {log}")
    return process.stdout


def consume_selfhost(sdk, output, version):
    work = output / "selfhost"
    work.mkdir()
    source = sdk / "templates" / CONSOLE_TEMPLATE / "src/main.zan"
    artifact = work / "probe.exe"
    token = "sdk-selfhost-consumption-probe"
    stdlib_args = ["--stdlib-path", str(sdk / "stdlib")]

    system = Path(os.environ["SystemRoot"])
    env = {"SystemRoot": str(system), "PATH": os.pathsep.join((str(system / "System32"), str(system))),
           "TEMP": str(work), "TMP": str(work)}
    toolchain = sdk / "toolchain"
    compiler = toolchain / "zanc-selfhost.exe"
    seed = toolchain / "zanc.exe"
    disabled_seed = toolchain / "zanc.disabled"
    bootstrap = sdk / "bootstrap"
    entries = (bootstrap / "selfhost_sources.txt").read_text(encoding="utf-8").splitlines()
    sources = [bootstrap / safe_relative(entry.strip() + ".zan") for entry in entries if entry.strip()]
    if not sources or len(set(sources)) != len(sources) or any(not path.is_file() for path in sources):
        raise ValueError("Incomplete SDK bootstrap sources")
    generations = [toolchain / "bootstrap-gen2.exe", toolchain / "bootstrap-gen3.exe"]
    if any(path.exists() for path in [disabled_seed, *generations]):
        raise ValueError("SDK contains reserved bootstrap output names")

    response = work / "bootstrap sources.rsp"
    response.write_text("".join(f'"{source.as_posix()}"\n' for source in sources), encoding="utf-8")
    evidence = []
    seed.rename(disabled_seed)
    try:
        for number, binary in enumerate(generations, 2):
            log = work / f"gen{number}-compile.json"
            command = [str(compiler), "@" + str(response), *stdlib_args, "-o", str(binary)]
            run_sdk_probe_step(command, work, env, log, 600)
            version_log = work / f"gen{number}-version.json"
            reported_version = run_sdk_probe_step([str(binary), "--version"], work, env, version_log, 30)
            if reported_version.strip() != f"zanc {version} (gen1)":
                raise ValueError(f"Unexpected bootstrap compiler identity; see {version_log}")
            evidence.append({"artifact": identity.required_identity(binary),
                             "compile_log": identity.required_identity(log),
                             "version_log": identity.required_identity(version_log)})
            compiler = binary
        compile_log = work / "compile.json"
        command = [str(compiler), str(source), *stdlib_args, "-o", str(artifact)]
        run_sdk_probe_step(command, work, env, compile_log, 120)
        executable = identity.required_identity(artifact)
        if not executable["size"]:
            raise ValueError("Staged self-hosted compiler produced an empty executable")
        runtime_log = work / "runtime.json"
        stdout = run_sdk_probe_step([str(artifact), token], work, env, runtime_log, 30)
        if not any(line.rstrip().endswith("[0] = " + token) for line in stdout.splitlines()):
            raise ValueError(f"Staged self-hosted compiler probe produced unexpected output; see {runtime_log}")
    finally:
        disabled_seed.rename(seed)
        for binary in generations:
            binary.unlink(missing_ok=True)
    return {"artifact": executable, "compile_log": identity.required_identity(compile_log),
            "runtime_log": identity.required_identity(runtime_log), "bootstrap": evidence}


def consume_templates(sdk, output, compiler, selection, env=None, cmake=None):
    root = Path(__file__).resolve().parents[1]
    command = [sys.executable, "-I", "-B", str(root / "tests/run_template_acceptance.py"),
               "--root", str(sdk), "--zanc", str(compiler), "--stdlib", str(sdk / "stdlib"),
               "--driver-dir", str(sdk / "toolchain"), "--output", str(output), "--filter", selection,
               "--workers", "1"]
    if cmake:
        command.extend(("--cmake", cmake))
    subprocess.run(command, check=True, env=env)
    results = load(output / "results.json")
    rows = results["rows"]
    if (not results.get("complete") or not rows or results["selected"] != len(rows)
            or any(row.get("status") != "pass" for row in rows)):
        raise ValueError("SDK consumption did not execute all selected templates")
    runs = []
    for row in rows:
        if row["template"] != CONSOLE_TEMPLATE:
            continue
        artifact = Path(row["result"]["artifact"])
        token = "sdk-stage-consumption-probe"
        log = output / "console-args-runtime.json"
        stdout = run_sdk_probe_step([str(artifact), token], artifact.parent, env, log, 30)
        if token not in stdout:
            raise ValueError("SDK console-args runtime did not preserve the supplied argument; see " + str(log))
        runs.append({"template": row["template"], "artifact": identity.required_identity(artifact),
                     "log": identity.required_identity(log)})
    return command, results["selected"], runs


def macos_install_names(binary, output, env):
    log = output / (binary.name + ".json")
    stdout = run_sdk_probe_step(["/usr/bin/otool", "-arch", "all", "-L", str(binary)],
                                output, env, log, 30)
    lines = stdout.splitlines()
    heading = re.compile(re.escape(str(binary)) + r"(?: \(architecture [^)]+\))?:")
    if not lines or not heading.fullmatch(lines[0]):
        raise ValueError(f"Missing Mach-O dependency listing for {binary.name}; see {log}")
    names = set()
    for line in lines:
        if heading.fullmatch(line):
            continue
        dependency, marker, _ = line.strip().rpartition(" (compatibility version ")
        if not marker or not dependency:
            raise ValueError(f"Unrecognized otool output for {binary.name}; see {log}")
        names.add(dependency)
    return sorted(names)


def macos_system_library(dependency):
    return dependency.startswith(("/usr/lib/", "/System/Library/")) and ".." not in dependency.split("/")


def resolve_macos_library(dependency, original, library_dirs):
    name = dependency.rsplit("/", 1)[-1]
    if not re.fullmatch(r"[A-Za-z0-9_.+-]+[.]dylib", name):
        raise ValueError(f"Unsupported macOS SDK dylib install name: {dependency}")
    if Path(dependency).is_absolute():
        candidates = [Path(dependency)]
    elif dependency == "@rpath/" + name:
        candidates = [directory / name for directory in library_dirs]
    elif dependency.startswith("@loader_path/"):
        candidates = [original.parent / dependency.removeprefix("@loader_path/")]
    else:
        candidates = []
    sources = {candidate.resolve() for candidate in candidates if candidate.is_file()}
    if len(sources) == 1:
        source = sources.pop()
        if any(source.is_relative_to(directory) for directory in library_dirs):
            return name, source
    raise ValueError("macOS SDK dylib must resolve uniquely within the configured library directories: "
                     f"{original.name} -> {dependency}")


def relocate_macos_commands(sdk, build, archiver, library_dirs):
    toolchain = sdk / "toolchain"
    lib = toolchain / "lib"
    library_dirs = {directory.resolve(strict=True) for directory in library_dirs}
    images = [(archiver.resolve() if name == "llvm-ar" else (build / name).resolve(), toolchain / name)
              for name in POSIX_COMMANDS]
    staged_libraries = {}
    used_library_dirs = set()
    env = {"PATH": "/usr/bin:/bin", "LC_ALL": "C"}
    with tempfile.TemporaryDirectory(prefix="macho-", dir=sdk.parent) as temporary:
        logs = Path(temporary)
        for original, binary in images:
            library = binary.parent == lib
            relative_prefix = "@loader_path/" if library else "@loader_path/lib/"
            changes = []
            for dependency in macos_install_names(binary, logs, env):
                if macos_system_library(dependency):
                    continue
                name, source = resolve_macos_library(dependency, original, library_dirs)
                if library and source == original:  # otool -L includes LC_ID_DYLIB.
                    continue
                if source not in staged_libraries:
                    target = lib / name
                    lib.mkdir(exist_ok=True)
                    if target.exists():
                        raise ValueError(f"Conflicting macOS SDK dylib name: {name}")
                    shutil.copy(source, target)
                    staged_libraries[source] = target
                    used_library_dirs.update(directory for directory in library_dirs if source.is_relative_to(directory))
                    images.append((source, target))
                changes.extend(("-change", dependency, relative_prefix + staged_libraries[source].name))
            if library:
                changes.extend(("-id", relative_prefix + binary.name))
            if changes:
                # Reclaim the original search-path load commands for longer
                # loader-relative names. The staged closure uses no rpaths.
                load_log = logs / (binary.name + ".load.json")
                loads = run_sdk_probe_step(["/usr/bin/otool", "-arch", "all", "-l", str(binary)],
                                           logs, env, load_log, 30)
                rpaths = re.findall(r"(?m)^\s*cmd LC_RPATH\s*\n\s*cmdsize \d+\s*\n\s*path (.+) \(offset \d+\)$", loads)
                for rpath in sorted(set(rpaths)):
                    changes.extend(("-delete_rpath", rpath))
                binary.chmod(binary.stat().st_mode | stat.S_IWUSR)
                subprocess.run(["/usr/bin/install_name_tool", *changes, str(binary)], env=env, check=True, timeout=30)
                subprocess.run(["/usr/bin/codesign", "--force", "--sign", "-", str(binary)],
                               env=env, check=True, timeout=60)
                subprocess.run(["/usr/bin/codesign", "--verify", "--strict", str(binary)],
                               env=env, check=True, timeout=60)
        audit_macos_commands(sdk, logs / "audit", env)
    for directory in sorted(used_library_dirs):
        prefix = directory.parent
        notices = [path for path in prefix.iterdir()
                   if path.is_file() and path.name.upper().startswith(("LICENSE", "COPYING", "NOTICE"))]
        if not notices:
            raise ValueError(f"macOS SDK dependency notices missing from {prefix}")
        # Homebrew's cellar path ends in the version, beneath the formula name.
        package = prefix.parent.name if prefix.name[:1].isdigit() else prefix.name
        target = lib / "licenses" / package
        target.mkdir(parents=True)
        for notice in notices:
            shutil.copy(notice, target / notice.name)


def audit_macos_commands(sdk, output, env):
    output.mkdir()
    toolchain = sdk / "toolchain"
    lib = toolchain / "lib"
    images = [toolchain / name for name in POSIX_COMMANDS]
    seen = set(images)
    external = set()
    for binary in images:
        prefix = "@loader_path/" if binary.parent == lib else "@loader_path/lib/"
        for dependency in macos_install_names(binary, output, env):
            if macos_system_library(dependency):
                continue
            name = dependency.removeprefix(prefix)
            target = lib / name
            if (dependency == prefix + name and re.fullmatch(r"[A-Za-z0-9_.+-]+[.]dylib", name)
                    and target.is_file() and target.resolve() == target):
                if target not in seen:
                    seen.add(target)
                    images.append(target)
            else:
                external.add(f"{binary.relative_to(sdk).as_posix()} -> {dependency}")
    if external:
        raise ValueError("macOS SDK commands reference unbundled non-system dylibs:\n  " + "\n  ".join(sorted(external)))


def consume_archive(archive, output):
    _, platform = posix_archive_metadata(archive)
    output.mkdir(parents=True, exist_ok=False)
    home, temp = output / "home", output / "tmp"
    home.mkdir()
    temp.mkdir()
    env = {"HOME": str(home), "TMPDIR": str(temp), "PATH": "/usr/bin:/bin", "LC_ALL": "C"}
    acceptance = output / "acceptance"
    with tempfile.TemporaryDirectory(prefix="extract-", dir=output) as extracted:
        sdk, record = extract_posix_archive(archive, Path(extracted))
        if platform == "macos":
            audit_macos_commands(sdk, output / "macho-dependencies", env)
        cmake = shutil.which("cmake")
        if not cmake:
            raise ValueError("SDK template acceptance requires CMake")
        compiler = sdk / "toolchain/zanc"
        command, selected, runs = consume_templates(sdk, acceptance, compiler, "^console/console-args$", env, cmake)
        if selected != 1 or len(runs) != 1:
            raise ValueError("SDK console-args template did not compile and run")
        work = output / "static-library"
        work.mkdir()
        source, artifact = work / "library.zan", work / "libsdk-probe.a"
        shutil.copyfile(Path(__file__).resolve().parents[1] / "tests/emit_lib/lib.zan", source)
        archiver = sdk / "toolchain/llvm-ar"
        compile_log, members_log = work / "compile.json", work / "members.json"
        # Exercise zanc's default discovery of llvm-ar beside the compiler.
        run_sdk_probe_step([str(compiler), str(source), "--auto-stdlib", "--no-packages", "-o", str(artifact)],
                           work, env, compile_log, 120)
        members = run_sdk_probe_step([str(archiver), "t", str(artifact)], work, env, members_log, 30)
        if not members.strip():
            raise ValueError("SDK static library has no members; see " + str(members_log))
    identity.atomic_json(output / "sdk-consumption.json", {"schema": SCHEMA, "phase": "archive-consumed",
        "archive": record, "command": command, "selected": selected, "runs": runs,
        "results": identity.required_identity(acceptance / "results.json"),
        "static_library": {"artifact": identity.required_identity(artifact),
                           "compile_log": identity.required_identity(compile_log),
                           "members_log": identity.required_identity(members_log)}})


def consume(args):
    sdk = args.sdk.resolve()
    receipt = sdk / "sdk-consumption.json"
    receipt.unlink(missing_ok=True)
    manifest = verify_sdk(sdk)
    output = args.output.resolve()
    if output.exists():
        raise ValueError("Consumption output must be a new directory")
    command, selected, runs = consume_templates(sdk, output, sdk / "toolchain/zanc.exe", args.filter)
    selfhost = consume_selfhost(sdk, output, manifest["version"])
    after = verify_sdk(sdk)
    if after != manifest:
        raise ValueError("SDK identity changed during consumption")
    identity.atomic_json(receipt, {"schema": SCHEMA, "phase": "templates-consumed",
        "manifest_sha256": manifest["manifest_sha256"], "build_identity_sha256": manifest["build_identity_sha256"],
        "validation": manifest["validation"], "command": command,
        "results": identity.required_identity(output / "results.json"), "selected": selected,
        "runs": runs, "selfhost": selfhost})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("gallery-plan", "native-inputs", "inputs", "copy-sources",
                                         "preflight", "seal", "verify", "consume", "stage-posix-sources",
                                         "relocate-macos", "write-archive", "consume-archive"))
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build", type=Path)
    parser.add_argument("--plan", type=Path)
    parser.add_argument("--ticket", type=Path)
    parser.add_argument("--sdk", type=Path)
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--archiver", type=Path)
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
    elif args.mode == "stage-posix-sources":
        if not args.sdk:
            parser.error("stage-posix-sources requires --sdk")
        stage_posix_sources(args.repo.resolve(), args.sdk.absolute())
    elif args.mode == "relocate-macos":
        if sys.platform != "darwin" or not args.sdk or not args.build or not args.archiver:
            parser.error("relocate-macos requires macOS and --sdk, --build, --archiver")
        relocate_macos_commands(args.sdk.resolve(), args.build.resolve(), args.archiver, args.native_dir)
    elif args.mode == "write-archive":
        if not args.sdk or not args.archive:
            parser.error("write-archive requires --sdk --archive")
        write_posix_archive(args.sdk.absolute(), args.archive.absolute())
    elif args.mode == "consume-archive":
        if not args.archive or not args.output:
            parser.error("consume-archive requires --archive --output")
        consume_archive(args.archive.absolute(), args.output.resolve())
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
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError, tarfile.TarError) as error:
        print("SDK stage failed: %s" % error, file=sys.stderr)
        raise SystemExit(1)
