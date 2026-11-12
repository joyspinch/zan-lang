"""Persistent SDK staging contracts. Synthetic byte fixtures never compile Zan."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("sdk_stage", ROOT / "scripts/sdk_stage_manifest.py")
SDK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SDK)
POWERSHELL = shutil.which("powershell") or shutil.which("pwsh")


class SdkStageTests(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / "_scratch"
        scratch.mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="sdk-contract-", dir=scratch)
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)

    def write(self, path, text="fixture"):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return path

    def call(self, *args, success=True, env=None):
        result = subprocess.run([str(x) for x in args], capture_output=True, text=True, env=env)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def stage_fixture(self):
        build = self.base / "build"
        names = ["zanc.exe", "zan-lsp.exe", "zan-dap.exe", "zanfmt.exe", "zandoc.exe", "ld.exe", "ld.lld.exe", "llvm-ar.exe",
                 "zanrt_timer.obj", "zanrt_io.obj", "zanrt_io_mt.obj", "zanrt_sync.obj", "zanrt_file.obj", "zanrt_decimal.obj",
                 "zan_embed_api.obj", "zan_inflate.obj", "libzancrypto.a"]
        names += ["mingw/lib/" + n for n in ("crt2.o", "libmingw32.a", "libkernel32.a")]
        names += ["linux-musl/" + n for n in ("libc.a", "crt1.o", "crti.o", "crtn.o", "zanrt_timer.o", "zanrt_io.o",
                  "zanrt_io_mt.o", "zanrt_sync.o", "zanrt_file.o", "zanrt_decimal.o", "zan_embed_api.o", "zan_inflate.o", "libzancrypto.a")]
        for name in names:
            self.write(build / name, "current " + name)
        for driver in ("zan_gui", "zan_image", "zan_audio", "zan_game"):
            self.write(build / (driver + ".bundle"), driver + ".dll\n")
            self.write(build / (driver + ".dll"), "synthetic native DLL")
        return build

    @unittest.skipUnless(POWERSHELL and os.name == "nt", "Windows PowerShell staging contract")
    def test_missing_cli_fails_before_touching_previous_stage(self):
        build = self.stage_fixture()
        (build / "zan-lsp.exe").unlink()
        dest = self.base / "stage"
        marker = self.write(dest / "marker", "preserve")
        result = self.call(POWERSHELL, "-NoProfile", "-File", ROOT / "scripts/stage_dev_toolchain.ps1",
                           "-Build", build, "-Dest", dest, success=False)
        self.assertIn("zan-lsp.exe", result.stdout + result.stderr)
        self.assertEqual(marker.read_text(), "preserve")
        self.assertFalse((dest / "zanc.exe").exists())

    @unittest.skipUnless(POWERSHELL and os.name == "nt", "Windows PowerShell staging contract")
    def test_future_mtime_stale_cli_is_replaced_by_content(self):
        build = self.stage_fixture()
        dest = self.base / "stage"
        old = self.write(dest / "zan-lsp.exe", "stale")
        future = 2208988800
        os.utime(old, (future, future))
        # Reproduce a newer host's incompatible Utility module inherited by
        # Windows PowerShell through Python; staging must use its own host module.
        module = self.base / "foreign-modules/Microsoft.PowerShell.Utility"
        self.write(module / "Microsoft.PowerShell.Utility.psd1",
                   "@{ ModuleVersion='99.0'; PowerShellVersion='99.0'; RootModule='Utility.psm1'; FunctionsToExport=@('Get-FileHash') }")
        self.write(module / "Utility.psm1", "function Get-FileHash { throw 'Foreign Utility must not be loaded' }")
        env = dict(os.environ, PSModulePath=str(module.parent))
        self.call(POWERSHELL, "-NoProfile", "-File", ROOT / "scripts/stage_dev_toolchain.ps1", "-Build", build, "-Dest", dest, env=env)
        self.assertEqual(old.read_bytes(), (build / "zan-lsp.exe").read_bytes())

    @unittest.skipUnless(POWERSHELL and os.name == "nt", "Windows PowerShell path contract")
    def test_publish_rejects_shared_output_before_cleaning(self):
        for dest in (ROOT, ROOT / "dist", ROOT / "build", ROOT / "stdlib", ROOT / "_scratch"):
            with self.subTest(dest=dest):
                result = self.call(POWERSHELL, "-NoProfile", "-File", ROOT / "scripts/publish_ide.ps1",
                                   "-SkipBuild", "-NoBump", "-PreflightOnly", "-OutDir", dest, success=False)
                self.assertIn("dedicated child", result.stdout + result.stderr)

    @unittest.skipUnless(POWERSHELL and os.name == "nt", "Windows junction path contract")
    def test_stage_rejects_junction_and_keeps_its_target(self):
        build = self.stage_fixture()
        target = self.base / "target"
        marker = self.write(target / "marker", "preserve")
        junction = self.base / "junction"
        self.call("cmd", "/c", "mklink", "/J", junction, target)
        # Remove the junction itself before TemporaryDirectory traverses it.
        self.addCleanup(lambda: os.rmdir(junction) if junction.exists() else None)
        self.call(POWERSHELL, "-NoProfile", "-File", ROOT / "scripts/stage_dev_toolchain.ps1",
                  "-Build", build, "-Dest", junction, success=False)
        self.assertEqual(marker.read_text(), "preserve")
        self.assertTrue(junction.exists())

    @unittest.skipUnless(POWERSHELL and os.name == "nt", "Windows bundle payload contract")
    def test_unconditional_bundle_payload_is_required_before_staging(self):
        build = self.stage_fixture()
        self.write(build / "zan_gui.bundle", "zan_gui.dll\nmissing-required.dll\n")
        dest = self.base / "stage"
        marker = self.write(dest / "marker", "preserve")
        result = self.call(POWERSHELL, "-NoProfile", "-File", ROOT / "scripts/stage_dev_toolchain.ps1",
                           "-Build", build, "-Dest", dest, success=False)
        self.assertIn("missing-required.dll", result.stdout + result.stderr)
        self.assertEqual(marker.read_text(), "preserve")
        self.assertFalse((dest / "zanc.exe").exists())

    def test_generated_owner_native_is_artifact_and_third_party_remains_input(self):
        repo = self.base
        owner = repo / "packages/Zan.Gui/src/Gui/drivers/win-x64"
        for name in ("static/libzan_gui.a", "static/zan_gui.libs", "zan_gui.dll", "libzan_gui.dll.a", "zan_gui.bundle", "zan_gui.def"):
            self.assertTrue(SDK.generated_native(owner / name, repo), name)
        for name in ("WebView2Loader.dll", "libcrypto.dll", "static/libfreetype.a"):
            self.assertFalse(SDK.generated_native(owner / name, repo), name)

    def gallery_fixture(self):
        source = self.base / "gallery-repo"
        self.write(source / "tests/gui/form_grid.html", "<data-grid></data-grid>")
        self.write(source / "tests/gui/form_grid_test.zan", "fixture")
        gallery = source / "knowledge/gallery.json"
        SDK.identity.atomic_json(gallery, {"entries": [{"files": ["tests/gui/form_grid.html", "tests/gui/form_grid_test.zan"]}]})
        plan = {"repo": str(source.resolve()), "toolchain": [], "copies": [
            {"source": str(gallery), "destination": "knowledge/gallery.json"},
            {"source": str(source / "tests/gui/form_grid.html"), "destination": "tests/gui/form_grid.html"}]}
        return source, gallery, plan

    def test_gallery_completion_adds_only_missing_exact_payload(self):
        source, gallery, plan = self.gallery_fixture()
        SDK.gallery_plan(plan, complete=True)
        self.assertEqual(len(plan["copies"]), 3)
        self.assertEqual(plan["copies"][-1]["destination"], "tests/gui/form_grid_test.zan")
        SDK.gallery_plan(plan, complete=True)
        self.assertEqual(len(plan["copies"]), 3)
        SDK.gallery_plan(plan)
        SDK.verify_gallery(source.resolve())

    def test_gallery_missing_source_or_mapping_is_rejected(self):
        source, gallery, plan = self.gallery_fixture()
        with self.assertRaisesRegex(ValueError, "absent from payload mappings"):
            SDK.gallery_plan(plan)
        (source / "tests/gui/form_grid_test.zan").unlink()
        with self.assertRaisesRegex(ValueError, "Required input is missing"):
            SDK.gallery_plan(plan, complete=True)
        with self.assertRaisesRegex(ValueError, "Required input is missing"):
            SDK.verify_gallery(source.resolve())

    def test_gallery_unsafe_paths_are_rejected(self):
        for name in ("../outside.zan", "/absolute.zan", "C:/absolute.zan", "tests//file.zan", "tests/./file.zan", "tests/file.zan:stream", "tests\\file.zan"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                SDK.gallery_refs({"entries": [{"files": [name]}]})

    def test_gallery_path_aliases_are_rejected(self):
        for names in (["tests/gui/form_grid.html", "TESTS/gui/form_grid.html"], ["tests/file.zan."], ["tests/file.zan "]):
            with self.subTest(names=names), self.assertRaises(ValueError):
                SDK.gallery_refs({"entries": [{"files": names}]})
        source, gallery, plan = self.gallery_fixture()
        plan["copies"][1]["destination"] = "TESTS/gui/form_grid.html"
        with self.assertRaisesRegex(ValueError, "Aliased SDK gallery destination"):
            SDK.gallery_plan(plan, complete=True)

    def observed_fixture(self):
        repo = self.base / "repo"
        repo.mkdir()
        self.call("git", "-C", repo, "init", "-q")
        self.write(repo / ".gitignore", "build/\nsdk/\n")
        self.write(repo / "VERSION", "1.2.3\n")
        source = self.write(repo / "templates/console/source.zan", "synthetic source\n")
        self.call("git", "-C", repo, "add", ".gitignore", "VERSION", "templates")
        self.call("git", "-C", repo, "-c", "user.name=SDK contract", "-c", "user.email=sdk@example.invalid", "commit", "-qm", "fixture")
        build = repo / "build"
        self.write(build / "CMakeCache.txt", "CMAKE_HOME_DIRECTORY:INTERNAL=%s\nCMAKE_C_COMPILER:FILEPATH=%s\n" % (repo.as_posix(), Path(sys.executable).as_posix()))
        zanc = self.write(build / ("zanc.exe" if os.name == "nt" else "zanc"), "synthetic compiler bytes")
        helper = ROOT / "scripts/release_source_manifest.py"
        self.call(sys.executable, "-I", "-B", helper, "capture", "--repo", repo, "--build", build)
        self.call(sys.executable, "-I", "-B", helper, "seal", "--repo", repo, "--build", build)
        plan = {"repo": str(repo), "toolchain": [{"source": str(zanc), "destination": "zanc.exe"}],
                "copies": [{"source": str(source), "destination": "templates/console/source.zan"}]}
        return repo, build, plan

    def test_bad_identity_lists_invalidate_previously_valid_sealed_build(self):
        repo, build, plan = self.observed_fixture()
        helper = ROOT / "scripts/release_source_manifest.py"
        common = [sys.executable, "-I", "-B", helper]
        tail = ["--repo", repo, "--build", build]
        identity = build / "test-out/release-build-identity.json"
        pending = build / "test-out/release-build-pending.json"
        listing = build / "bad-list.json"
        for mode in ("capture", "seal"):
            for invalid in ("missing", "malformed", "shape"):
                with self.subTest(mode=mode, invalid=invalid):
                    self.call(*common, "capture", *tail)
                    self.call(*common, "seal", *tail)
                    self.call(*common, "verify", *tail)
                    self.assertTrue(identity.is_file())
                    self.write(pending, "old pending must not survive failed capture")
                    if invalid == "missing":
                        listing.unlink(missing_ok=True)
                    elif invalid == "malformed":
                        self.write(listing, "{bad JSON")
                    else:
                        SDK.identity.atomic_json(listing, {"wrong": "shape"})
                    self.call(*common, mode, *tail, "--native-input-list", listing, success=False)
                    self.assertFalse(identity.exists())
                    if mode == "capture":
                        self.assertFalse(pending.exists())

    def test_identity_json_path_lists_preserve_every_file_check(self):
        repo, build, plan = self.observed_fixture()
        native = self.write(build / "native.bin", "native input")
        paths = build / "native-paths.json"
        SDK.identity.atomic_json(paths, [str(native)] * 2000)
        helper = ROOT / "scripts/release_source_manifest.py"
        common = [sys.executable, "-I", "-B", helper]
        tail = ["--repo", repo, "--build", build]
        self.call(*common, "capture", *tail, "--native-input-list", paths)
        artifact = self.write(build / "additional-artifact.bin", "built output")
        artifacts = build / "artifact-paths.json"
        SDK.identity.atomic_json(artifacts, [str(artifact)] * 2000)
        self.call(*common, "seal", *tail, "--artifact-list", artifacts)
        record = SDK.load(build / "test-out/release-build-identity.json")
        self.assertEqual(record["native_paths"], [str(native.resolve())])
        self.assertIn(str(artifact.resolve()), [p["path"] for p in record["artifacts"]])
        artifact.unlink()
        self.call(*common, "verify", *tail, success=False)
        self.call(*common, "capture", *tail, "--native-input-list", paths)
        self.call(*common, "seal", *tail, "--artifact-list", artifacts, success=False)
        native.write_text("drift")
        self.call(*common, "verify", *tail, success=False)
        SDK.identity.atomic_json(paths, {"not": "an array"})
        result = self.call(*common, "capture", *tail, "--native-input-list", paths, success=False)
        self.assertIn("JSON array", result.stderr)

    def test_declared_tracked_native_rebuild_is_artifact_but_real_source_drift_fails(self):
        repo, build, plan = self.observed_fixture()
        outputs = SDK.identity.sdk_native_outputs(repo)
        for path in outputs:
            self.write(Path(path), "old generated native")
        self.call("git", "-C", repo, "add", "packages")
        self.call("git", "-C", repo, "-c", "user.name=SDK contract", "-c", "user.email=sdk@example.invalid", "commit", "-qm", "tracked generated fixtures")
        helper = ROOT / "scripts/release_source_manifest.py"
        common = [sys.executable, "-I", "-B", helper]
        tail = ["--repo", repo, "--build", build]
        self.call(*common, "capture", *tail, "--sdk-native-outputs")
        for path in outputs:
            Path(path).write_text("fresh rebuilt native")
        self.call(*common, "seal", *tail)
        record = SDK.load(build / "test-out/release-build-identity.json")
        self.assertEqual(record["generated_outputs"], outputs)
        self.assertTrue(set(outputs).issubset({a["path"] for a in record["artifacts"]}))
        self.call(*common, "verify", *tail)
        Path(outputs[0]).write_text("tampered after seal")
        self.call(*common, "verify", *tail, success=False)
        self.call(*common, "capture", *tail, "--sdk-native-outputs")
        self.write(repo / "templates/console/source.zan", "real source drift")
        result = self.call(*common, "seal", *tail, success=False)
        self.assertIn("input drift", result.stderr)
        self.assertFalse((build / "test-out/release-build-identity.json").exists())

    def test_preflight_requires_identity_and_exact_receipts(self):
        repo, build, plan = self.observed_fixture()
        local = SDK.preflight(repo, build, plan, True)
        self.assertEqual(local["validation"], "local-build-only")
        with self.assertRaises(FileNotFoundError):
            SDK.preflight(repo, build, plan, False)
        for phase in ("before", "after"):
            self.call(sys.executable, "-I", "-B", ROOT / "scripts/release_source_manifest.py", "verify",
                      "--repo", repo, "--build", build, "--receipt", build / ("test-out/release-source-%s.json" % phase))
        self.assertEqual(SDK.preflight(repo, build, plan, False)["validation"], "release-gate-verified")
        receipt = build / "test-out/release-source-after.json"
        value = SDK.load(receipt)
        value["identity_sha256"] = "0" * 64
        SDK.identity.atomic_json(receipt, value)
        with self.assertRaisesRegex(ValueError, "receipts"):
            SDK.preflight(repo, build, plan, False)

    def test_unobserved_payload_and_changed_build_artifact_are_rejected(self):
        repo, build, plan = self.observed_fixture()
        unknown = self.write(build / "zan-lsp.exe", "unobserved CLI")
        plan["toolchain"].append({"source": str(unknown), "destination": "zan-lsp.exe"})
        with self.assertRaisesRegex(ValueError, "not covered"):
            SDK.preflight(repo, build, plan, True)
        unknown.unlink()
        plan["toolchain"].pop()
        Path(plan["toolchain"][0]["source"]).write_text("changed compiler")
        with self.assertRaises(subprocess.CalledProcessError):
            SDK.preflight(repo, build, plan, True)

    def test_sdk_inventory_preserves_debugger_bin_and_detects_tampering(self):
        sdk = self.base / "sdk"
        gdb = self.write(sdk / "toolchain/debugger/bin/gdb.exe", "debugger fixture")
        manifest = {"schema": SDK.SCHEMA, "phase": "staged", "validation": "local-build-only",
                    "build_identity_sha256": "a" * 64, "files": SDK.inventory(sdk)}
        manifest["manifest_sha256"] = SDK.identity.digest(manifest)
        SDK.identity.atomic_json(sdk / "sdk-manifest.json", manifest)
        self.assertEqual(SDK.verify_sdk(sdk), manifest)
        gdb.write_text("changed debugger")
        with self.assertRaisesRegex(ValueError, "payload changed"):
            SDK.verify_sdk(sdk)
        gdb.write_text("debugger fixture")
        self.write(sdk / "unlisted.dll")
        with self.assertRaisesRegex(ValueError, "unlisted"):
            SDK.verify_sdk(sdk)

    def test_windows_workflow_prepares_before_seal_and_packages_platform_root(self):
        text = (ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
        windows = text[text.index("  windows:"):text.index("  release:")]
        self.assertLess(windows.index("-Action Capture"), windows.index("-PrepareOnly -NoBump"))
        self.assertLess(windows.index("-PrepareOnly -NoBump"), windows.index("-Action Seal"))
        self.assertLess(windows.index("-Action Seal"), windows.index("-P scripts/release_quality_gate.cmake"))
        self.assertIn("publish_ide.ps1 -SkipBuild -NoBump", windows)
        self.assertIn("sdk_stage_manifest.py consume --sdk dist/win-x64", windows)
        self.assertIn("$platform = Join-Path $PWD 'dist/win-x64'", windows)
        self.assertNotIn("Rename-Item dist $name", windows)

    def test_skip_build_has_no_compilation_calls_after_preparation(self):
        text = (ROOT / "scripts/publish_ide.ps1").read_text(encoding="utf-8-sig")
        preparation = text.index("if (-not $SkipBuild) {")
        end = text.index("# ---- complete preflight", preparation)
        self.assertIn("gen_knowledge.ps1", text[preparation:end])
        self.assertIn("Start-Process", text[preparation:end])
        for prohibited in ("gen_knowledge.ps1", "Start-Process", "build_ide.ps1"):
            self.assertNotIn(prohibited, text[end:])
        self.assertNotIn("PUBLISH_WARN", text)
        self.assertNotIn("STAGE_WARN", (ROOT / "scripts/stage_dev_toolchain.ps1").read_text(encoding="utf-8-sig"))


if __name__ == "__main__":
    unittest.main()
