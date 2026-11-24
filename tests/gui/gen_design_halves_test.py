"""Regress IDE runtime design-half generation for __ZanControlCatalog."""
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


class DesignHalfGenerationTests(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / "_scratch"
        scratch.mkdir(exist_ok=True)
        self.fixture = tempfile.TemporaryDirectory(prefix="design-halves-", dir=scratch)
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

    def test_emits_public_partial_without_guikind_and_skips_ideform(self):
        (self.project / "IdeForm.html").write_text("<body></body>", encoding="utf-8")
        (self.project / "SampleView.html").write_text(
            '<body><button id="SaveBtn"></button><list-view id="Rows" data-of="RowItem"></list-view></body>',
            encoding="utf-8")
        (self.project / "SampleView.zan").write_text(
            "using Gui;\npartial class SampleView {\n    void OnInit() {}\n}\n",
            encoding="utf-8")

        with patch("sys.argv", ["gen_design_halves.py"]):
            self.assertEqual(generator.main(), 0)

        self.assertFalse((self.project / "IdeForm.design.zan").exists())
        self.assertFalse((self.project / "IdeDesignedRegistry.zan").exists())

        half = (self.project / "SampleView.design.zan").read_text(encoding="utf-8")
        self.assertNotIn('[GuiKind', half)
        self.assertIn("public partial class SampleView : DesignedControl {", half)
        self.assertIn("public override Control CreateCloneShell() { return new SampleView(); }", half)
        self.assertIn("public SampleView() {", half)
        self.assertIn('this.Design("SampleView", "ide/SampleView.html");', half)
        self.assertIn("this.OnInit();", half)
        self.assertIn("public Button SaveBtn {", half)
        self.assertIn("public ListView<RowItem> Rows {", half)


if __name__ == "__main__":
    unittest.main()
