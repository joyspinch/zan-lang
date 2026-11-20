"""Persistent SDK staging contracts. Synthetic byte fixtures never compile Zan."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("sdk_stage", ROOT / "scripts/sdk_stage_manifest.py")
SDK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SDK)
WINDOWS_SPEC = importlib.util.spec_from_file_location("sdk_windows_archive", ROOT / "scripts/sdk_windows_archive.py")
WINDOWS_ARCHIVE = importlib.util.module_from_spec(WINDOWS_SPEC)
WINDOWS_SPEC.loader.exec_module(WINDOWS_ARCHIVE)
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
        names = ["zanc.exe", "zanc-selfhost.exe", "zan-mvc.exe", "zan-lsp.exe", "zan-dap.exe", "zanfmt.exe", "zandoc.exe",
                 "ld.exe", "ld.lld.exe", "llvm-ar.exe", "llvm-nm.exe", "opt.exe", "llc.exe",
                 "zanrt_timer.obj", "zanrt_io.obj", "zanrt_io_mt.obj", "zanrt_sync.obj", "zanrt_file.obj", "zanrt_decimal.obj",
                 "zan_embed_api.obj", "zan_inflate.obj", "zanrt_crash.obj", "libzancrypto.a"]
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
        dest = self.base / "stage"
        marker = self.write(dest / "marker", "preserve")
        for name in ("zan-lsp.exe", "zanc-selfhost.exe", "llvm-nm.exe", "opt.exe", "llc.exe"):
            with self.subTest(name=name):
                (build / name).unlink()
                result = self.call(POWERSHELL, "-NoProfile", "-File", ROOT / "scripts/stage_dev_toolchain.ps1",
                                   "-Build", build, "-Dest", dest, "-IncludeSelfhost", success=False)
                self.assertIn(name, result.stdout + result.stderr)
                self.assertEqual(marker.read_text(), "preserve")
                self.assertFalse((dest / "zanc.exe").exists())
                self.write(build / name, "current " + name)

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
        inputs = self.base / "inputs.json"
        self.call(POWERSHELL, "-NoProfile", "-File", ROOT / "scripts/stage_dev_toolchain.ps1", "-Build", build,
                  "-Dest", dest, "-IncludeSelfhost", "-InputList", inputs, env=env)
        self.assertEqual(old.read_bytes(), (build / "zan-lsp.exe").read_bytes())
        mappings = {row["destination"]: Path(row["source"]) for row in SDK.load(inputs)}
        for name in ("zanc-selfhost.exe", "llvm-nm.exe", "opt.exe", "llc.exe"):
            with self.subTest(name=name):
                self.assertEqual(mappings[name].resolve(), (build / name).resolve())
                self.assertEqual((dest / name).read_bytes(), (build / name).read_bytes())

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

    def windows_zip_fixture(self):
        sdk = self.base / "sdk"
        self.write(sdk / "templates/console/.gitignore", "*.exe")
        self.write(sdk / "toolchain/debugger/bin/gdb.exe", "debugger fixture")
        manifest = {"schema": SDK.SCHEMA, "phase": "staged", "version": "1.2.3", "validation": "local-build-only",
                    "build_identity_sha256": "a" * 64, "files": SDK.inventory(sdk)}
        manifest["manifest_sha256"] = SDK.identity.digest(manifest)
        SDK.identity.atomic_json(sdk / "sdk-manifest.json", manifest)
        self.write(sdk / "sdk-consumption.json", "previous acceptance")
        return sdk, self.base / "out/zan-ide-1.2.3-win-x64.zip"

    def test_windows_zip_round_trip_keeps_exact_payload(self):
        sdk, archive = self.windows_zip_fixture()
        WINDOWS_ARCHIVE.write_archive(sdk, archive)
        root = archive.stem
        with zipfile.ZipFile(archive) as bundle:
            self.assertCountEqual(bundle.namelist(), [f"{root}/{name}" for name in (
                "sdk-manifest.json", "templates/console/.gitignore", "toolchain/debugger/bin/gdb.exe")])
        output = self.base / "consumed"

        def consume(command, *, check, env, cwd):
            extracted = Path(command[command.index("--sdk") + 1])
            self.assertEqual(extracted.name, root)
            self.assertEqual(extracted.parent.parent, output)
            self.assertEqual(SDK.verify_sdk(extracted), SDK.verify_sdk(sdk))
            self.assertEqual(command[1:3], ["-I", "-B"])
            self.assertEqual(command[4], "consume")
            self.assertEqual(cwd, output)
            self.assertTrue(check)
            self.assertNotIn("ZAN_CLANG", env)
            SDK.identity.atomic_json(extracted / "sdk-consumption.json", {"phase": "templates-consumed"})

        with mock.patch.dict(os.environ, {"SystemRoot": str(self.base / "Windows"), "ZAN_CLANG": "developer-clang"}), \
                mock.patch.object(WINDOWS_ARCHIVE.shutil, "which", return_value=str(self.base / "cmake.exe")), \
                mock.patch.object(WINDOWS_ARCHIVE.subprocess, "run", side_effect=consume) as consumer:
            WINDOWS_ARCHIVE.consume_archive(sdk, archive, output)
            consumer.assert_called_once()
        receipt = SDK.load(output / "sdk-consumption.json")
        self.assertEqual(receipt["phase"], "archive-consumed")
        self.assertEqual(receipt["archive"], SDK.identity.required_identity(archive))
        self.assertFalse(list(output.glob("extract-*")))
        self.assertEqual((sdk / "sdk-consumption.json").read_text(), "previous acceptance")

    def test_windows_zip_rejects_checksum_and_inventory_mismatches(self):
        sdk, archive = self.windows_zip_fixture()
        WINDOWS_ARCHIVE.write_archive(sdk, archive)
        checksum = WINDOWS_ARCHIVE.checksum_file(archive)
        with zipfile.ZipFile(archive) as bundle:
            entries = {name: bundle.read(name) for name in bundle.namelist()}
        gdb = archive.stem + "/toolchain/debugger/bin/gdb.exe"
        for case, payload, message in (
                ("checksum", entries, "checksum mismatch"),
                ("extra", {**entries, archive.stem + "/unlisted.dll": b"extra"}, "entries differ"),
                ("missing", {name: data for name, data in entries.items() if name != gdb}, "entries differ"),
                ("wrong-size", {**entries, gdb: b"short"}, "Invalid Windows SDK ZIP entry"),
                ("wrong-hash", {**entries, gdb: b"X" * len(entries[gdb])}, "payload changed")):
            with self.subTest(case=case):
                with zipfile.ZipFile(archive, "w") as bundle:
                    for name, data in payload.items():
                        bundle.writestr(name, data)
                record = SDK.identity.required_identity(archive)
                checksum.write_text("incorrect" if case == "checksum" else WINDOWS_ARCHIVE.checksum_line(archive, record),
                                    encoding="ascii")

                def consume(command, **kwargs):
                    SDK.verify_sdk(Path(command[command.index("--sdk") + 1]))

                output = self.base / case
                with mock.patch.dict(os.environ, {"SystemRoot": str(self.base / "Windows")}), \
                        mock.patch.object(WINDOWS_ARCHIVE.shutil, "which", return_value=str(self.base / "cmake.exe")), \
                        mock.patch.object(WINDOWS_ARCHIVE.subprocess, "run", side_effect=consume) as consumer:
                    with self.assertRaisesRegex(ValueError, message):
                        WINDOWS_ARCHIVE.consume_archive(sdk, archive, output)
                self.assertEqual(consumer.call_count, int(case == "wrong-hash"))
                self.assertFalse((output / "sdk-consumption.json").exists())
                self.assertFalse(list(output.glob("extract-*")))

    def test_windows_zip_rejects_path_collisions_and_reserved_names(self):
        for names in (("Toolchain/a", "toolchain/b"), ("toolchain/bin", "toolchain/bin/file"),
                      ("templates/file", "templates/file"), ("SDK-MANIFEST.JSON",),
                      ("toolchain/COM1.txt",), ("templates/name. ",), ("templates/file:stream",),
                      ("templates\\file",)):
            with self.subTest(names=names), self.assertRaisesRegex(ValueError, "Windows SDK path|Unsafe SDK manifest path"):
                WINDOWS_ARCHIVE.member_names({"files": [{"path": name} for name in names]}, "sdk")

    def test_windows_zip_preserves_existing_archive_or_checksum_on_conflict(self):
        sdk, archive = self.windows_zip_fixture()
        checksum = self.write(WINDOWS_ARCHIVE.checksum_file(archive), "old checksum")
        with self.assertRaises(FileExistsError):
            WINDOWS_ARCHIVE.write_archive(sdk, archive)
        self.assertFalse(archive.exists())
        self.assertEqual(checksum.read_text(), "old checksum")
        self.write(archive, "old archive")
        with self.assertRaises(FileExistsError):
            WINDOWS_ARCHIVE.write_archive(sdk, archive)
        self.assertEqual(archive.read_text(), "old archive")

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

    def test_consumption_receipt_requires_restricted_selfhost_run(self):
        sdk = self.base / "sdk"
        compiler = self.write(sdk / "toolchain/zanc-selfhost.exe")
        seed = self.write(sdk / "toolchain/zanc.exe", "C bootstrap seed")
        self.write(sdk / "bootstrap/selfhost_sources.txt", "selfhost/main\n")
        bootstrap_source = self.write(sdk / "bootstrap/selfhost/main.zan")
        template = self.write(sdk / "templates" / SDK.CONSOLE_TEMPLATE / "src/main.zan")
        stdlib_args = ["--stdlib-path", str(sdk / "stdlib")]
        gen2 = compiler.with_name("bootstrap-gen2.exe")
        gen3 = compiler.with_name("bootstrap-gen3.exe")
        manifest = {"schema": SDK.SCHEMA, "phase": "staged", "validation": "local-build-only",
                    "version": "1.2.3", "build_identity_sha256": "a" * 64, "files": SDK.inventory(sdk)}
        manifest["manifest_sha256"] = SDK.identity.digest(manifest)
        SDK.identity.atomic_json(sdk / "sdk-manifest.json", manifest)
        receipt = sdk / "sdk-consumption.json"
        system = self.base / "Windows"
        token = "sdk-selfhost-consumption-probe"

        def run(command, **options):
            if command[0] == sys.executable:
                output = Path(command[command.index("--output") + 1])
                SDK.identity.atomic_json(output / "results.json", {"complete": True, "selected": 1,
                                         "rows": [{"template": "console/test", "status": "pass"}]})
                return subprocess.CompletedProcess(command, 0)
            work, env = options["cwd"], options["env"]
            self.assertEqual(work, args.output / "selfhost")
            self.assertEqual(env["PATH"].split(os.pathsep), [str(system / "System32"), str(system)])
            self.assertNotIn("ZAN_CLANG", env)
            self.assertFalse(seed.exists())
            artifact = work / "probe.exe"
            if command[1:] == ["--version"]:
                self.assertIn(Path(command[0]), (gen2, gen3))
                return subprocess.CompletedProcess(command, 0, "zanc 1.2.3 (gen1)\n", "")
            if command[0] in (str(compiler), str(gen2)):
                target = gen2 if command[0] == str(compiler) else gen3
                response = work / "bootstrap sources.rsp"
                self.assertEqual(command[1:], ["@" + str(response), *stdlib_args, "-o", str(target)])
                self.assertEqual(response.read_text(encoding="utf-8"), f'"{bootstrap_source.as_posix()}"\n')
                self.write(target)
                return subprocess.CompletedProcess(command, 0, "", "")
            if command[0] == str(gen3):
                self.assertEqual(command[1:], [str(template), *stdlib_args, "-o", str(artifact)])
                self.write(artifact)
                return subprocess.CompletedProcess(command, 0, "", "")
            self.assertEqual(command, [str(artifact), token])
            return subprocess.CompletedProcess(command, 0, runtime_stdout, "")

        with mock.patch.dict(os.environ, {"SystemRoot": str(system), "ZAN_CLANG": "developer-clang.exe"}), \
                mock.patch.object(SDK.subprocess, "run", side_effect=run):
            args = SimpleNamespace(sdk=sdk, output=self.base / "pass", filter="^console/test$")
            runtime_stdout = "  参数[0] = " + token + "\n"
            SDK.consume(args)
            evidence = SDK.load(receipt)["selfhost"]
            self.assertEqual(SDK.load(evidence["runtime_log"]["path"])["stdout"], runtime_stdout)
            self.assertEqual(Path(evidence["compile_log"]["path"]).parent, args.output / "selfhost")
            self.assertEqual(Path(evidence["artifact"]["path"]).parent, args.output / "selfhost")
            self.assertEqual([row["artifact"]["path"] for row in evidence["bootstrap"]], [str(gen2), str(gen3)])
            self.assertEqual(seed.read_text(), "C bootstrap seed")
            self.assertFalse(gen2.exists())
            self.assertFalse(gen3.exists())

            args.output = self.base / "fail"
            runtime_stdout = "wrong output"
            with self.assertRaisesRegex(ValueError, "unexpected output"):
                SDK.consume(args)
            self.assertFalse(receipt.exists())

    def posix_payload(self, sdk, reverse=False):
        files = {"VERSION": "1.2.3\n", "README.txt": "SDK\n", "stdlib/System/Test.zan": "stdlib fixture",
                 "templates/console/console-args/zan.proj": "entry = src/main.zan\n",
                 "examples/html_gallery/zan.proj": "gallery fixture"}
        files.update({"toolchain/" + name: name for name in SDK.POSIX_COMMANDS + SDK.POSIX_RUNTIME})
        for name in reversed(files) if reverse else files:
            path = self.write(sdk / name, files[name])
            mode = 0o711 if name.removeprefix("toolchain/") in SDK.POSIX_COMMANDS else 0o600
            path.chmod(mode if reverse else mode | 0o044)
            timestamp = 1700000000 if reverse else 1650000000
            os.utime(path, (timestamp, timestamp))
        (sdk / "templates/empty").mkdir()

    def test_posix_archive_is_reproducible_and_keeps_sdk_layout(self):
        name = "zan-ide-1.2.3-linux-x64"
        archives = []
        for index in (0, 1):
            sdk = self.base / str(index) / name
            self.posix_payload(sdk, reverse=bool(index))
            archive = sdk.with_name(sdk.name + ".tar.gz")
            with mock.patch.dict(os.environ, {"SOURCE_DATE_EPOCH": "12345"}):
                SDK.write_posix_archive(sdk, archive)
            archives.append(archive)
        first, second = archives
        self.assertEqual(first.read_bytes(), second.read_bytes())
        self.assertEqual(first.with_suffix(".gz.sha256").read_bytes(), second.with_suffix(".gz.sha256").read_bytes())
        self.assertEqual(first.read_bytes()[:10], bytes.fromhex("1f8b08000000000002ff"))
        with tarfile.open(first) as tar:
            self.assertEqual(tar.getnames(), sorted(tar.getnames()))
            self.assertTrue(all((m.uid, m.gid, m.uname, m.gname, m.mtime) == (0, 0, "", "", 12345)
                                for m in tar.getmembers()))
            self.assertEqual(tar.getmember(name + "/toolchain/zanc").mode, 0o755)
            self.assertEqual(tar.getmember(name + "/stdlib/System/Test.zan").mode, 0o644)
        sdk, record = SDK.extract_posix_archive(first, self.base / "unpacked")
        self.assertEqual(sdk.name, name)
        self.assertTrue((sdk / "templates/empty").is_dir())
        self.assertEqual((sdk / "stdlib/System/Test.zan").read_text(), "stdlib fixture")
        self.assertEqual(record["sha256"], SDK.identity.file_identity(first)["sha256"])

    def test_posix_archive_rejects_bad_checksum_incomplete_payload_and_unsafe_member(self):
        name = "zan-ide-1.2.3-macos-arm64"
        sdk = self.base / name
        archive = self.base / (name + ".tar.gz")
        self.posix_payload(sdk)
        runtime = sdk / "toolchain/zan_embed_api.o"
        runtime.unlink()
        with self.assertRaisesRegex(ValueError, "zan_embed_api[.]o"):
            SDK.write_posix_archive(sdk, archive)
        self.assertFalse(archive.exists())
        self.write(runtime)
        compiler = sdk / "toolchain/zanc"
        compiler.unlink()
        with self.assertRaisesRegex(ValueError, "toolchain/zanc"):
            SDK.write_posix_archive(sdk, archive)
        self.write(compiler).chmod(0o755)
        SDK.write_posix_archive(sdk, archive)
        checksum = archive.with_suffix(".gz.sha256")
        checksum.write_bytes(b"wrong checksum\n")
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            SDK.extract_posix_archive(archive, self.base / "bad-checksum")
        with tarfile.open(archive, "w:gz") as tar:
            entry = tarfile.TarInfo(name + "/../escaped")
            tar.addfile(entry)
        checksum.write_bytes(SDK.posix_checksum(archive, SDK.identity.file_identity(archive)))
        with self.assertRaisesRegex(ValueError, "Unsafe SDK manifest path"):
            SDK.extract_posix_archive(archive, self.base / "bad-path")
        self.assertFalse((self.base / "bad-path/escaped").exists())

    def test_posix_archive_rejects_case_and_unicode_path_aliases(self):
        name = "zan-ide-1.2.3-macos-arm64"
        archive = self.base / (name + ".tar.gz")
        output = self.base / "unpacked"
        for first, second in (("toolchain/zanc", "toolchain/ZANC"),
                              ("examples/caf\u00e9.zan", "examples/cafe\u0301.zan"),
                              ("templates/Console/one", "templates/console/two")):
            with self.subTest(first=first, second=second):
                with tarfile.open(archive, "w:gz") as tar:
                    root = tarfile.TarInfo(name)
                    root.type = tarfile.DIRTYPE
                    tar.addfile(root)
                    for path in (first, second):
                        tar.addfile(tarfile.TarInfo(f"{name}/{path}"))
                checksum = archive.with_suffix(".gz.sha256")
                checksum.write_bytes(SDK.posix_checksum(archive, SDK.identity.file_identity(archive)))
                with self.assertRaisesRegex(ValueError, "Aliased SDK archive paths"):
                    SDK.extract_posix_archive(archive, output)
                self.assertFalse(output.exists())

    def test_macos_archive_audits_every_command_before_running_probes(self):
        sdk = self.base / "zan-ide-1.2.3-macos-arm64"
        self.posix_payload(sdk)
        archive = sdk.with_name(sdk.name + ".tar.gz")
        SDK.write_posix_archive(sdk, archive)
        output = self.base / "consumption"
        version = " (compatibility version 1.0.0, current version 1.0.0)"
        zstd = "/opt/homebrew/opt/zstd/lib/libzstd.1.dylib"
        llvm = "@rpath/libLLVM.dylib"

        def run(command, **options):
            self.assertEqual(command[:4], ["/usr/bin/otool", "-arch", "all", "-L"])
            self.assertEqual(options["env"]["PATH"], "/usr/bin:/bin")
            self.assertNotIn("DYLD_LIBRARY_PATH", options["env"])
            binary = Path(command[4])
            self.assertEqual(binary.parents[2].parent, output)
            system = "/usr/lib/libSystem.B.dylib" + version
            lines = [f"{binary}:", f"\t{system}"]
            if binary.name == "llvm-ar":
                lines = [f"{binary} (architecture arm64):", f"\t{system}",
                         f"{binary} (architecture x86_64):", f"\t{llvm}{version}"]
            if binary.name == "zanc":
                lines.append(f"\t{zstd}{version}")
            return subprocess.CompletedProcess(command, 0, "\n".join(lines) + "\n", "")

        with mock.patch.object(SDK.subprocess, "run", side_effect=run), \
                mock.patch.dict(os.environ, {"DYLD_LIBRARY_PATH": "/opt/homebrew/lib"}), \
                self.assertRaisesRegex(ValueError, "non-system dylibs") as failure:
            SDK.consume_archive(archive, output)
        self.assertEqual(str(failure.exception).splitlines()[1:],
                         [f"  toolchain/llvm-ar -> {llvm}", f"  toolchain/zanc -> {zstd}"])
        self.assertEqual({log.stem for log in (output / "macho-dependencies").glob("*.json")},
                         set(SDK.POSIX_COMMANDS))
        self.assertFalse((output / "acceptance").exists())
        self.assertFalse((output / "sdk-consumption.json").exists())

    def test_macos_relocation_stages_and_signs_transitive_libraries(self):
        sdk = self.base / "sdk"
        self.posix_payload(sdk)
        archiver = self.write(self.base / "llvm/bin/llvm-ar")
        llvm = self.write(self.base / "llvm/lib/libLLVM.dylib", "LLVM runtime")
        zstd = self.write(self.base / "zstd/lib/libzstd.1.dylib", "zstd runtime")
        self.write(self.base / "llvm/LICENSE.TXT", "LLVM license")
        self.write(self.base / "zstd/LICENSE", "zstd license")
        system = "/usr/lib/libSystem.B.dylib"
        llvm_id = "@rpath/libLLVM.dylib"
        dependencies = {"llvm-ar": [llvm_id], llvm.name: [llvm_id, zstd.as_posix()], zstd.name: [zstd.as_posix()]}
        original = {file: file.read_bytes() for file in (archiver, llvm, zstd)}
        signed, verified = set(), set()
        compatibility = " (compatibility version 1.0.0, current version 1.0.0)"

        def run(command, **options):
            binary = Path(command[-1])
            self.assertTrue(binary.is_relative_to(sdk / "toolchain"))
            self.assertNotIn("DYLD_LIBRARY_PATH", options["env"])
            if command[0] == "/usr/bin/otool":
                if command[3] == "-l":
                    stdout = ("Load command 0\n          cmd LC_RPATH\n      cmdsize 40\n"
                              "         path @loader_path/../lib (offset 12)\n")
                else:
                    names = [system, *dependencies.get(binary.name, [])]
                    stdout = f"{binary}:\n" + "".join(f"\t{name}{compatibility}\n" for name in names)
                return subprocess.CompletedProcess(command, 0, stdout, "")
            if command[0] == "/usr/bin/install_name_tool":
                changes = iter(command[1:-1])
                for flag in changes:
                    if flag == "-change":
                        old, new = next(changes), next(changes)
                        dependencies[binary.name].remove(old)
                        dependencies[binary.name].append(new)
                    elif flag == "-id":
                        dependencies[binary.name][0] = next(changes)
                    else:
                        self.assertEqual(flag, "-delete_rpath")
                        self.assertEqual(next(changes), "@loader_path/../lib")
            else:
                self.assertEqual(command[0], "/usr/bin/codesign")
                if command[1:4] == ["--force", "--sign", "-"]:
                    signed.add(binary.name)
                else:
                    self.assertEqual(command[1:-1], ["--verify", "--strict"])
                    self.assertIn(binary.name, signed)
                    verified.add(binary.name)
            return subprocess.CompletedProcess(command, 0)

        with mock.patch.object(SDK.subprocess, "run", side_effect=run):
            SDK.relocate_macos_commands(sdk, self.base / "build", archiver, [llvm.parent, zstd.parent])
            lib = sdk / "toolchain/lib"
            self.assertEqual({file.name: file.read_bytes() for file in lib.glob("*.dylib")},
                             {llvm.name: original[llvm], zstd.name: original[zstd]})
            self.assertEqual((lib / "licenses/llvm/LICENSE.TXT").read_text(), "LLVM license")
            self.assertEqual((lib / "licenses/zstd/LICENSE").read_text(), "zstd license")
            self.assertEqual(dependencies["llvm-ar"], ["@loader_path/lib/libLLVM.dylib"])
            self.assertEqual(dependencies[llvm.name], ["@loader_path/libLLVM.dylib", "@loader_path/libzstd.1.dylib"])
            self.assertEqual(signed, {"llvm-ar", llvm.name, zstd.name})
            self.assertEqual(verified, signed)
            self.assertEqual({file: file.read_bytes() for file in original}, original)
            foreign = self.write(self.base / "unapproved/libForeign.dylib")
            with self.assertRaisesRegex(ValueError, "configured library directories"):
                SDK.resolve_macos_library(foreign.as_posix(), llvm, {llvm.parent, zstd.parent})
            dependencies[zstd.name].append("@loader_path/missing.dylib")
            with self.assertRaisesRegex(ValueError, "libzstd[.]1[.]dylib -> @loader_path/missing[.]dylib"):
                SDK.audit_macos_commands(sdk, self.base / "audit", {})

    @unittest.skipUnless(os.name == "posix" and shutil.which("cmake"), "POSIX shell and CMake contract")
    def test_posix_shell_packaging_and_archive_consumption(self):
        repo = self.base / "fixture"
        self.posix_payload(repo)
        scripts = repo / "scripts"
        scripts.mkdir()
        for name in ("package_bundle.sh", "sdk_stage_manifest.py", "release_source_manifest.py"):
            shutil.copyfile(ROOT / "scripts" / name, scripts / name)
        build = repo / "build"
        (repo / "toolchain").rename(build)
        self.write(build / "linux-riscv64/sysroot-marker")
        template = repo / "templates/console/console-args"
        self.write(template / "src/main.zan", "template {{NAME}}\n")
        self.write(repo / ".gitignore", "/build/\n/out/\n*.db\n")
        self.write(template / ".gitignore", "/scratch/\n")
        self.write(template / "seed.db", "tracked template asset")
        self.write(template / "work.db", "ignored local database")
        self.write(template / "scratch/upload.bin", "ignored local upload")
        self.call("git", "-C", repo, "init", "-q")
        self.call("git", "-C", repo, "add", ".gitignore", "VERSION", "stdlib", "templates")
        self.call("git", "-C", repo, "add", "-f", "templates/console/console-args/seed.db")
        self.write(build / "llvm-ar", r"""#!/bin/sh
set -eu
case "$1" in
  --format=gnu) test "$2" = rcsD; test -s "$4"; printf 'fixture indexed archive\n' > "$3" ;;
  t) test -s "$2"; printf 'fixture.o\n' ;;
  *) exit 1 ;;
esac
""").chmod(0o755)
        compiler = r"""#!/bin/sh
set -eu
sdk=$(cd "$(dirname "$0")/.." && pwd)
test -f "$sdk/stdlib/System/Test.zan"
test -f "$sdk/toolchain/zanrt_timer.o"
test "$PATH" = /usr/bin:/bin
test -z "${ZAN_LLVM_AR:-}${LLVM_AR:-}${LD_LIBRARY_PATH:-}${DYLD_LIBRARY_PATH:-}${ZAN_CLANG:-}"
test -d "$HOME"
if [ "$2" = --auto-stdlib ]; then
    test "$4" = -o
    "$sdk/toolchain/llvm-ar" --format=gnu rcsD "$5" "$1"
else
    test "$2" = -o
    test "$4" = --stdlib-path
    test "$5" = "$sdk/stdlib"
    grep -qx 'template TplProbe' "$1"
    printf '%s\n' '#!/bin/sh' 'echo "$1"' > "$3"
    chmod +x "$3"
fi
"""
        self.write(build / "zanc", compiler).chmod(0o755)
        env = dict(os.environ, LLVM_AR="developer-llvm-ar", ZAN_CLANG="developer-clang",
                   LD_LIBRARY_PATH="/developer/lib", DYLD_LIBRARY_PATH="/developer/lib", SOURCE_DATE_EPOCH="0")
        pack = ("bash", scripts / "package_bundle.sh", "linux", "x64")
        self.call(*pack, env=env)
        dedicated_build = repo / "existing-build"
        build.rename(dedicated_build)
        build = dedicated_build
        pack += (build,)
        archive = repo / "out/zan-ide-1.2.3-linux-x64.tar.gz"
        initial = archive.read_bytes()
        root = archive.name.removesuffix(".tar.gz")
        with tarfile.open(archive) as tar:
            names = set(tar.getnames())
            self.assertIn(f"{root}/toolchain/linux-riscv64/sysroot-marker", names)
            self.assertIn(f"{root}/examples/html_gallery/zan.proj", names)
            self.assertIn(f"{root}/templates/console/console-args/.gitignore", names)
            self.assertIn(f"{root}/templates/console/console-args/seed.db", names)
            self.assertNotIn(f"{root}/templates/console/console-args/work.db", names)
            self.assertNotIn(f"{root}/templates/console/console-args/scratch/upload.bin", names)
            self.assertNotIn(f"{root}/.gitignore", names)
        self.write(template / "work.db", "changed ignored database")
        self.call(*pack, env=env)
        self.assertEqual(archive.read_bytes(), initial)
        (build / "zanrt_sync.o").unlink()
        result = self.call(*pack, env=env, success=False)
        self.assertIn("zanrt_sync.o", result.stderr)
        self.assertEqual(archive.read_bytes(), initial)
        (build / "zanrt_sync.o").write_text("zanrt_sync.o")
        self.write(build / "zanc", compiler.replace('echo "$1"', 'echo wrong-output')).chmod(0o755)
        passed_archive_dir = self.base / "passing-sdk"
        (repo / "out").rename(passed_archive_dir)
        self.call(*pack, env=env)
        shutil.rmtree(build)
        self.call(sys.executable, "-I", "-B", ROOT / "scripts/sdk_stage_manifest.py", "consume-archive",
                  "--archive", passed_archive_dir / archive.name, "--output", self.base / "passed", env=env)
        receipt = SDK.load(self.base / "passed/sdk-consumption.json")
        self.assertEqual(receipt["phase"], "archive-consumed")
        self.assertEqual(receipt["archive"]["sha256"], SDK.identity.file_identity(passed_archive_dir / archive.name)["sha256"])
        self.assertEqual(receipt["selected"], 1)
        self.assertTrue(SDK.load(receipt["runs"][0]["log"]["path"])["stdout"].strip())
        self.assertEqual(SDK.load(receipt["static_library"]["members_log"]["path"])["stdout"].strip(), "fixture.o")
        self.assertFalse(list((self.base / "passed").glob("extract-*")))
        result = self.call(sys.executable, "-I", "-B", ROOT / "scripts/sdk_stage_manifest.py", "consume-archive",
                           "--archive", archive, "--output", self.base / "failed", env=env, success=False)
        self.assertIn("did not preserve the supplied argument", result.stderr)
        self.assertFalse((self.base / "failed/sdk-consumption.json").exists())

    def test_posix_workflows_consume_the_uploaded_archive(self):
        text = (ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
        for platform, next_job in (("linux", "macos"), ("macos", "windows")):
            with self.subTest(platform=platform):
                job = text[text.index(f"  {platform}:\n"):text.index(f"  {next_job}:\n")]
                pack = job.index(f"bash scripts/package_bundle.sh {platform}")
                consume = job.index("sdk_stage_manifest.py consume-archive")
                upload = job.index(f"name: zan-ide-{platform}-")
                self.assertLess(job.index("-P scripts/release_quality_gate.cmake"), pack)
                self.assertLess(pack, consume)
                self.assertLess(consume, upload)
                self.assertIn("SDK_ARCHIVE: ${{ steps.bundle.outputs.archive }}", job)
                self.assertIn('--archive "$SDK_ARCHIVE"', job)
                self.assertIn("${{ steps.bundle.outputs.archive }}\n", job[upload:])
                self.assertIn("${{ steps.bundle.outputs.archive }}.sha256", job[upload:])
                self.assertNotIn("if: always()", job[job.rindex("      - uses:", 0, upload):upload])

    def test_windows_workflow_prepares_before_seal_and_packages_platform_root(self):
        text = (ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
        windows = text[text.index("  windows:"):text.index("  release:")]
        self.assertIn("foreach ($key in @('NM', 'OPT', 'LLC'))", windows)
        self.assertIn("ZAN_LLVM_${key}_EXECUTABLE", windows)
        capture = windows.index("-Action Capture")
        build = windows.index("cmake --build build -j")
        shared_drivers = windows.index("& ./scripts/build_gui_driver.ps1\n")
        static_game = windows.index("& ./scripts/build_gui_driver.ps1 -Static -Drivers zan_game")
        prepare = windows.index("-PrepareOnly -NoBump")
        seal = windows.index("-Action Seal")
        self.assertLess(windows.index("$releaseNative += $llvmTool"), capture)
        self.assertLess(capture, build)
        self.assertLess(build, shared_drivers)
        self.assertLess(shared_drivers, static_game)
        self.assertLess(static_game, prepare)
        self.assertLess(prepare, seal)
        self.assertLess(windows.index("-Action Seal"), windows.index("-P scripts/release_quality_gate.cmake"))
        publish = windows.index("publish_ide.ps1 -SkipBuild -NoBump")
        package = windows.index("sdk_windows_archive.py write --sdk dist/win-x64")
        consume = windows.index("sdk_windows_archive.py consume --sdk dist/win-x64")
        upload = windows.index("name: zan-ide-win-x64")
        self.assertLess(publish, package)
        self.assertLess(package, consume)
        self.assertLess(consume, upload)
        self.assertIn("SDK_ARCHIVE: ${{ steps.bundle.outputs.archive }}", windows)
        self.assertIn("--archive $env:SDK_ARCHIVE", windows)
        self.assertIn("${{ steps.bundle.outputs.archive }}.sha256", windows[upload:])
        self.assertNotIn("if: always()", windows[windows.rindex("      - uses:", 0, upload):upload])

    def test_skip_build_has_no_compilation_calls_after_preparation(self):
        text = (ROOT / "scripts/publish_ide.ps1").read_text(encoding="utf-8-sig")
        preparation = text.index("if (-not $SkipBuild) {")
        end = text.index("# ---- complete preflight", preparation)
        self.assertIn("gen_knowledge.ps1", text[preparation:end])
        self.assertIn("Start-Process", text[preparation:end])
        self.assertIn("--target selfhost_compiler", text[preparation:end])
        for prohibited in ("gen_knowledge.ps1", "Start-Process", "build_ide.ps1", "--target selfhost_compiler"):
            self.assertNotIn(prohibited, text[end:])
        self.assertEqual(text[end:].count("-IncludeSelfhost"), 2)
        self.assertNotIn("PUBLISH_WARN", text)
        self.assertNotIn("STAGE_WARN", (ROOT / "scripts/stage_dev_toolchain.ps1").read_text(encoding="utf-8-sig"))


if __name__ == "__main__":
    unittest.main()
