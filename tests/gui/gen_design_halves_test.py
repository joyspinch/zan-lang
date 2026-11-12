"""Regress component discovery after GUI components move into separate packages."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "gen_design_halves", ROOT / "scripts" / "gen_design_halves.py")
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class PackageRegistrationTests(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / "_scratch"
        scratch.mkdir(exist_ok=True)
        self.fixture = tempfile.TemporaryDirectory(prefix="design-registry-", dir=scratch)
        self.addCleanup(self.fixture.cleanup)
        self.repo = Path(self.fixture.name)
        self.project = self.repo / "src" / "ide" / "src"
        self.project.mkdir(parents=True)
        self.patch_root = patch.object(generator, "ROOT", str(self.repo))
        self.patch_src = patch.object(generator, "SRC", str(self.project))
        self.patch_root.start()
        self.patch_src.start()
        self.addCleanup(self.patch_root.stop)
        self.addCleanup(self.patch_src.stop)

    def component(self, store, package, name, namespace, layout="src", manifest=True):
        package_dir = store / package
        source = package_dir / layout if layout else package_dir
        source.mkdir(parents=True, exist_ok=True)
        if manifest:
            (package_dir / "zan.pkg").write_text(f'name = "{package}"\n', encoding="utf-8")
        path = source / f"{name}.zan"
        path.write_text(
            f"namespace {namespace};\npublic class {name} {{\n"
            "    public static void Install() {}\n}\n", encoding="utf-8")
        return str(path)

    def test_split_packages_local_precedence_and_layouts(self):
        local = self.project.parent / "packages"
        preferred = self.component(local, "A.Local", "MovedView", "Local")
        self.component(self.repo / "packages", "A.Split", "MovedView", "Repo")
        self.component(self.repo / ".zan-packages", "B.Installed", "StoredView", "Stored", "stdlib")
        self.component(self.repo / "packages", "C.Flat", "FlatView", "Flat", "")
        self.component(self.repo / "packages", "D.Unmanifested", "IgnoredView", "Ignored", manifest=False)
        # A user-global package must never affect generated runtime registrations.
        fake_home = self.repo / "home"
        self.component(fake_home / ".zan-packages", "Global", "GlobalView", "Global")
        with patch("os.path.expanduser", return_value=str(fake_home)):
            self.assertEqual(generator.class_file("MovedView"), preferred)
            lines = generator.registry_lines(
                {"MovedView", "StoredView", "FlatView", "IgnoredView", "GlobalView", "MissingView"}, set())
        self.assertEqual(lines, [
            "        Flat.FlatView.Install();",
            "        Local.MovedView.Install();",
            "        Stored.StoredView.Install();",
        ])

    def test_registry_only_preserves_design_halves_and_filters_unreferenced(self):
        self.component(self.repo / "packages", "Future.Split", "MovedView", "Future")
        self.component(self.repo / "packages", "Unused", "UnusedView", "Unused")
        (self.project / "Panel.html").write_text(
            '<body><zan-moved-view id="Moved"></zan-moved-view></body>', encoding="utf-8")
        half = self.project / "Panel.design.zan"
        half.write_text("owned design half\n", encoding="utf-8")
        with patch("sys.argv", ["gen_design_halves.py", "--registry-only"]):
            self.assertEqual(generator.main(), 0)
        self.assertEqual(half.read_text(encoding="utf-8"), "owned design half\n")
        registry = (self.project / "IdeDesignedRegistry.zan").read_text(encoding="utf-8")
        self.assertIn("Future.MovedView.Install();", registry)
        self.assertNotIn("UnusedView", registry)

    def test_real_ide_workplace_components_are_registered(self):
        with patch.object(generator, "ROOT", str(ROOT)), patch.object(
                generator, "SRC", str(ROOT / "src" / "ide_zan" / "src")):
            lines = generator.registry_lines({"ConsoleView", "ChatView"}, set())
        self.assertEqual(lines, [
            "        Gui.Component.ChatView.Install();",
            "        Gui.Component.ConsoleView.Install();",
        ])


if __name__ == "__main__":
    unittest.main()
