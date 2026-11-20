#!/usr/bin/env python3
"""Package and consume the sealed Windows x64 SDK ZIP."""
import argparse
from contextlib import ExitStack
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import zipfile

# Resolve the repository helper even when invoked with python -I.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import sdk_stage_manifest as stage


MANIFEST = "sdk-manifest.json"
RECEIPT = "sdk-consumption.json"
WINDOWS_DEVICES = re.compile(r"(?:CON|PRN|AUX|NUL|CONIN\$|CONOUT\$|(?:COM|LPT)[0-9¹²³])", re.I)


def archive_name(archive, manifest):
    name = archive.stem
    match = re.fullmatch(r"zan-ide-([0-9A-Za-z][0-9A-Za-z.+-]*)-win-x64[.]zip", archive.name)
    if not match or match[1] != manifest.get("version"):
        raise ValueError("Windows SDK archive name must match the manifest version: " + archive.name)
    return name


def member_names(manifest, root):
    payload = [MANIFEST, *(row["path"] for row in manifest["files"])]
    seen = {}
    files = set()
    for name in payload:
        if name == RECEIPT or stage.safe_relative(name) != name:
            raise ValueError("Invalid Windows SDK path: " + name)
        parts = name.split("/")
        for part in parts:
            if (part.endswith((".", " ")) or re.search(r'[\x00-\x1f<>"|?*]', part)
                    or WINDOWS_DEVICES.fullmatch(part.split(".", 1)[0].rstrip())):
                raise ValueError("Invalid Windows SDK path: " + name)
        for depth in range(1, len(parts) + 1):
            prefix = "/".join(parts[:depth])
            folded = prefix.casefold()
            is_file = depth == len(parts)
            previous = seen.setdefault(folded, (prefix, is_file))
            if previous != (prefix, is_file) or (is_file and name in files):
                raise ValueError("Conflicting Windows SDK paths: " + name)
        files.add(name)
    return {f"{root}/{name}": name for name in sorted(files)}


def checksum_file(archive):
    return archive.with_suffix(".zip.sha256")


def checksum_line(archive, record):
    return f"{record['sha256']}  {archive.name}"


def write_archive(sdk, archive):
    manifest = stage.verify_sdk(sdk)
    members = member_names(manifest, archive_name(archive, manifest))
    archive.parent.mkdir(parents=True, exist_ok=True)
    with ExitStack() as cleanup:
        with archive.open("xb") as stream:
            cleanup.callback(archive.unlink)
            with zipfile.ZipFile(stream, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
                for member, relative in members.items():
                    bundle.write(sdk / relative, member)
        record = stage.identity.required_identity(archive)
        checksum = checksum_file(archive)
        with checksum.open("xb") as stream:
            cleanup.callback(checksum.unlink)
            stream.write((checksum_line(archive, record) + "\n").encode("ascii"))
        cleanup.pop_all()


def extract_archive(sdk, archive, destination):
    manifest = stage.verify_sdk(sdk)
    root = archive_name(archive, manifest)
    members = member_names(manifest, root)
    record = stage.identity.required_identity(archive)
    if checksum_file(archive).read_text(encoding="ascii").strip() != checksum_line(archive, record):
        raise ValueError("Windows SDK archive checksum mismatch: " + archive.name)
    extracted = destination / root
    extracted.mkdir()
    with zipfile.ZipFile(archive) as bundle:
        entries = bundle.infolist()
        if len(entries) != len(members) or {entry.orig_filename for entry in entries} != members.keys():
            raise ValueError("Windows SDK ZIP entries differ from the staged manifest")
        sizes = {row["path"]: row["size"] for row in manifest["files"]}
        sizes[MANIFEST] = (sdk / MANIFEST).stat().st_size
        for entry in entries:
            relative = members[entry.filename]
            file_type = stat.S_IFMT(entry.external_attr >> 16)
            if file_type not in (0, stat.S_IFREG) or entry.file_size != sizes[relative]:
                raise ValueError("Invalid Windows SDK ZIP entry: " + entry.filename)
            target = extracted / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            with bundle.open(entry) as source, target.open("xb") as output:
                shutil.copyfileobj(source, output)
    if (extracted / MANIFEST).read_bytes() != (sdk / MANIFEST).read_bytes():
        raise ValueError("Windows SDK ZIP manifest differs from the staged manifest")
    return extracted, record


def consume_archive(sdk, archive, output):
    cmake = shutil.which("cmake")
    if not cmake:
        raise ValueError("SDK template acceptance requires CMake")
    output.mkdir(parents=True)
    system = Path(os.environ["SystemRoot"])
    temp = output / "tmp"
    temp.mkdir()
    env = {"SystemRoot": str(system), "TEMP": str(temp), "TMP": str(temp),
           "PATH": os.pathsep.join((str(system / "System32"), str(system), str(Path(cmake).parent)))}
    acceptance = output / "acceptance"
    with tempfile.TemporaryDirectory(prefix="extract-", dir=output) as destination:
        extracted, record = extract_archive(sdk, archive, Path(destination))
        command = [sys.executable, "-I", "-B", str(Path(stage.__file__).resolve()),
                   "consume", "--sdk", str(extracted), "--output", str(acceptance)]
        subprocess.run(command, check=True, env=env, cwd=output)
        receipt = stage.load(extracted / RECEIPT)
    if stage.identity.required_identity(archive) != record:
        raise ValueError("Windows SDK archive changed during consumption")
    receipt.update(phase="archive-consumed", archive=record)
    stage.identity.atomic_json(output / RECEIPT, receipt)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("write", "consume"))
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    sdk, archive = args.sdk.resolve(), args.archive.resolve()
    if args.mode == "write":
        write_archive(sdk, archive)
    else:
        if not args.output:
            parser.error("consume requires --output")
        consume_archive(sdk, archive, args.output.resolve())
    print(f"Windows SDK archive {args.mode} passed: {archive}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.SubprocessError, zipfile.BadZipFile) as error:
        sys.exit(f"Windows SDK archive failed: {error}")
