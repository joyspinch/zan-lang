#!/usr/bin/env python3
"""Stage actual Zig musl link inputs and publish a complete runtime target."""
import argparse
import json
from pathlib import Path
import re
import shutil
import signal
import sys
import tempfile


REQUIRED = (
    "crt1.o", "crti.o", "crtn.o", "libc.a", "libc++.a",
    "libc++abi.a", "libunwind.a", "libcompiler_rt.a",
)
LINK_COMMAND = re.compile(
    r'^\s*(?:ld\.lld(?:\.exe)?|"[^"\r\n]*[/\\]ld\.lld(?:\.exe)?"'
    r'|[^\s"]*[/\\]ld\.lld(?:\.exe)?)\s+'
)


def selected_inputs(trace, cache_root):
    commands = [line for line in trace.splitlines() if LINK_COMMAND.match(line)]
    if len(commands) != 1:
        raise ValueError("expected exactly one Zig linker invocation")
    # Zig's -v output is raw argv joined by spaces, not shell-escaped text.
    # Anchor every input to the exact root reported by `zig env` and the
    # content-addressed o/<hex>/filename layout, rather than tokenizing paths.
    root = str(cache_root).rstrip("/\\")
    if not root or not Path(cache_root).is_absolute():
        raise ValueError("expected an absolute Zig global cache directory")
    roots = {root, root.replace("\\", "/"), root.replace("/", "\\")}
    pattern = re.compile(
        r'(?:^|[\s"])(?P<path>(?:' + "|".join(re.escape(p) for p in sorted(roots))
        + r')[/\\]o[/\\][0-9a-fA-F]+[/\\](?P<name>'
        + "|".join(re.escape(name) for name in REQUIRED) + r'))(?=$|[\s"])'
    )
    selected = {}
    for match in pattern.finditer(commands[0]):
        name = match.group("name")
        if name in selected:
            raise ValueError("duplicate target runtime input: " + name)
        source = Path(match.group("path"))
        if not source.is_file():
            raise ValueError("missing absolute target runtime input: " + str(source))
        selected[name] = source
    missing = set(REQUIRED) - selected.keys()
    if missing:
        raise ValueError("Zig did not supply runtime inputs: " + ", ".join(sorted(missing)))
    return selected


def publish_target(staged, target):
    """Overlay built artifacts in a candidate, then rename with failure rollback."""
    staged, target = Path(staged), Path(target)
    artifacts = sorted(p for p in staged.iterdir() if p.suffix in (".o", ".a"))
    if not artifacts or any(not p.is_file() or p.is_symlink() for p in artifacts):
        raise ValueError("expected regular staged runtime objects and archives")
    if target.is_symlink() or (target.exists() and not target.is_dir()):
        raise ValueError("runtime target must be a directory, not a file or symlink")
    target.parent.mkdir(parents=True, exist_ok=True)
    candidate = Path(tempfile.mkdtemp(prefix="." + target.name + ".install.", dir=target.parent))
    backup = None
    installed = False
    try:
        # Only the installation target is copied: preserve optional third-party
        # libraries and user files, without copying any source/runtime trees.
        if target.exists():
            shutil.copytree(target, candidate, symlinks=True, dirs_exist_ok=True)
        for artifact in artifacts:
            destination = candidate / artifact.name
            # Do not follow a preexisting artifact symlink into a user's file.
            if destination.is_symlink() or destination.is_file():
                destination.unlink()
            elif destination.exists():
                raise ValueError("runtime artifact conflicts with existing directory: " + str(destination))
            shutil.copy2(artifact, destination)
        if target.exists():
            backup = Path(tempfile.mkdtemp(prefix="." + target.name + ".backup.", dir=target.parent))
            backup.rmdir()
            # A hard process kill cannot execute rollback; retain an identifiable
            # sibling backup in that case so the previous target is recoverable.
            print("Runtime install recovery backup: " + str(backup), file=sys.stderr, flush=True)
            target.rename(backup)
        candidate.rename(target)
        installed = True
    except BaseException:
        if backup is not None and backup.exists():
            try:
                if target.exists():
                    target.rename(candidate)
                backup.rename(target)
            except BaseException as error:
                raise RuntimeError("runtime rollback failed; previous target retained at " + str(backup)) from error
        raise
    finally:
        if candidate.exists():
            shutil.rmtree(candidate)
        if installed and backup is not None and backup.exists():
            shutil.rmtree(backup)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path, help="link trace, or staged directory with --publish")
    parser.add_argument("output", type=Path)
    parser.add_argument("--publish", action="store_true", help="install staged .o/.a artifacts with rollback")
    cache = parser.add_mutually_exclusive_group()
    cache.add_argument("--cache-root", type=Path)
    cache.add_argument("--zig-env", type=Path, help="JSON output of the same Zig executable's env command")
    options = parser.parse_args()
    if options.publish:
        # Convert ordinary termination signals into exceptions while swapping,
        # so publish_target restores the backup before the shell cleans staging.
        def interrupted(signum, frame):
            signal.signal(signum, signal.SIG_IGN)
            raise SystemExit(128 + signum)
        for name in ("SIGINT", "SIGTERM", "SIGHUP"):
            if hasattr(signal, name):
                signal.signal(getattr(signal, name), interrupted)
        publish_target(options.trace, options.output)
        return
    cache_root = options.cache_root
    if options.zig_env:
        cache_root = json.loads(options.zig_env.read_text(encoding="utf-8"))["global_cache_dir"]
    if cache_root is None:
        parser.error("staging requires --cache-root or --zig-env")
    selected = selected_inputs(options.trace.read_text(encoding="utf-8"), cache_root)
    options.output.mkdir(parents=True, exist_ok=True)
    # Validate the entire selection before publishing any of it. The caller
    # builds into its own staging directory and publishes only after success.
    for name, source in selected.items():
        shutil.copyfile(source, options.output / name)
    (options.output / "zig-sysroot-inputs.json").write_text(
        json.dumps({name: str(source) for name, source in selected.items()}, indent=2) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
