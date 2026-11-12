"""Static release identity-chain contract; no CI, compiler or CTest execution."""
from pathlib import Path
import re
import sys
import unittest

WORKFLOW = Path(__file__).resolve().parents[1] / ".github/workflows/release.yml"


def validate(text):
    jobs = {}
    starts = list(re.finditer(r"^  ([A-Za-z][\w-]*):\s*$", text, re.M))
    for index, match in enumerate(starts):
        jobs[match[1]] = text[match.end():starts[index + 1].start() if index + 1 < len(starts) else len(text)]
    gate_jobs = []
    for name, body in jobs.items():
        if "-P scripts/release_quality_gate.cmake" not in body:
            continue
        gate_jobs.append(name)
        build_pattern = r"cmake --build ([\w./-]+) -j"
        builds = list(re.finditer(build_pattern, body))
        if len(builds) != 1:
            raise ValueError(name + ": expected one observed build")
        build = builds[0]
        directory = build[1]
        gate = re.search(r"cmake -DBUILD_DIR=([\w./-]+) -P scripts/release_quality_gate.cmake", body)
        if not gate or gate[1] != directory:
            raise ValueError(name + ": gate/build directory mismatch")
        if name == "windows":
            capture = re.search(r"release_build_identity.ps1 -Action Capture -Build ([\w./-]+) -NativeInput \$releaseNative", body)
            seal = re.search(r"release_build_identity.ps1 -Action Seal -Build ([\w./-]+)", body)
            if not capture or not seal or capture[1] != directory or seal[1] != directory:
                raise ValueError(name + ": missing capture/seal or build mismatch")
            if "if ($LASTEXITCODE -ne 0) { throw 'Release build failed; identity remains unsealed' }" not in body[build.end():seal.start()]:
                raise ValueError(name + ": unsuccessful build can reach seal")
            if "Get-Command $tool -ErrorAction Stop" not in body or "-Artifact @(" not in body:
                raise ValueError(name + ": named tools/artifacts missing")
        else:
            capture = re.search(r"release_source_manifest.py capture --repo \"\$PWD\" --build ([\w./-]+) \"\$\{release_native\[@\]\}\"", body)
            seal = re.search(r"release_source_manifest.py seal --repo \"\$PWD\" --build ([\w./-]+)", body)
            if not capture or not seal or capture[1] != directory or seal[1] != directory:
                raise ValueError(name + ": missing capture/seal or build mismatch")
            # GitHub's default bash uses -e; keep an explicit marker so this
            # invariant survives a later shell override.
            if "set -euo pipefail" not in body[:capture.start()]:
                raise ValueError(name + ": build failure must stop before seal")
            if "--native-input" not in body or "--artifact build/zan-lsp" not in body:
                raise ValueError(name + ": named inputs/artifacts missing")
        if not capture.start() < build.start() < seal.start() < gate.start():
            raise ValueError(name + ": expected capture -> build -> seal -> gate")
        if 'toolchain/$target/$file"' not in body or "linux-musl" not in body or "macos/x64" not in body:
            raise ValueError(name + ": explicit runtime single-file inputs missing")
    if set(gate_jobs) != {"linux", "macos", "windows"}:
        raise ValueError("Expected exactly three platform identity chains")
    if "New-Item -ItemType Directory -Force llvm-dist" in text:
        raise ValueError("SDK extraction must not add ignored-toolchain-sized untracked source trees")
    return gate_jobs


class ReleaseWorkflowIdentityTests(unittest.TestCase):
    def test_three_platform_chains(self):
        self.assertEqual(set(validate(WORKFLOW.read_text(encoding="utf-8"))), {"linux", "macos", "windows"})

    def test_missing_or_mismatched_observation_is_rejected(self):
        original = WORKFLOW.read_text(encoding="utf-8")
        mutations = [
            original.replace("release_source_manifest.py capture", "release_source_manifest.py omitted", 1),
            original.replace("release_source_manifest.py seal", "release_source_manifest.py omitted", 1),
            original.replace("-Action Capture -Build build", "-Action Capture -Build other", 1),
            original.replace("-Action Seal -Build build", "-Action Seal -Build other", 1),
            original.replace("-DBUILD_DIR=build -P scripts/release_quality_gate.cmake", "-DBUILD_DIR=other -P scripts/release_quality_gate.cmake", 1),
            original.replace("if ($LASTEXITCODE -ne 0) { throw 'Release build failed; identity remains unsealed' }", "# no failure guard"),
            original.replace("set -euo pipefail", "# no failure guard", 1),
        ]
        for text in mutations:
            with self.subTest(mutation=mutations.index(text)), self.assertRaises(ValueError):
                validate(text)


if __name__ == "__main__":
    unittest.main()
