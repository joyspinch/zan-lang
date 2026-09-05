"""Artwork regression tests; authoring dependency: Pillow. No client install needed."""
import csv
import hashlib
import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib
from PIL import Image

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[3]
GAME = ROOT / 'templates/game/legend'

def load(name):
    spec = importlib.util.spec_from_file_location(name, GAME / 'tools' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

extract = load('extract_wzl')
prepare = load('prepare_navigation')

def fixture(depth=3, width=2, height=2, raw=bytes([2,0,0,0,1,2,0,0])):
    payload = zlib.compress(raw)
    wzl = bytes(64) + struct.pack('<4B4hI', depth,0,0,0,width,height,-3,7,len(payload)) + payload
    wzx = bytes(44) + struct.pack('<II', 1,64)
    return wzl,wzx

class ArtworkTests(unittest.TestCase):
    def test_navigation_distinct_complete_and_transparent(self):
        with (GAME/'data/navigation.csv').open(encoding='utf-8-sig', newline='') as f:
            rows = list(csv.DictReader(f))
        self.assertEqual(sorted(int(r['id']) for r in rows), list(range(31)))
        hashes = set()
        for row in rows:
            path = GAME/row['icon']
            self.assertTrue(path.resolve().is_relative_to((GAME/'assets').resolve()))
            hashes.add(hashlib.sha256(path.read_bytes()).hexdigest())
            with Image.open(path) as image:
                self.assertEqual(image.size, (128,128))
                self.assertEqual(image.mode, 'RGBA')
                self.assertEqual(image.getpixel((0,0))[3], 0)
                self.assertIsNotNone(image.getbbox())
        self.assertEqual(len(hashes), 31)

    def test_atlas_reproduces_committed_pixels(self):
        scratch = ROOT/'_scratch/legend'
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as temp:
            prepare.prepare(GAME/'assets/generated/navigation-atlas.png', Path(temp))
            for i in range(32):
                with Image.open(Path(temp)/f'nav-{i}.png') as actual, Image.open(GAME/f'assets/generated/navigation/nav-{i}.png') as expected:
                    self.assertEqual(actual.tobytes(), expected.tobytes())

    def test_palette_frame_orientation_padding_and_transparency(self):
        palette = [[0,0,0],[255,0,0],[0,255,0]] + [[0,0,0]]*253
        image, meta = extract.decode_frame(*fixture(), 0, palette)
        self.assertEqual(image.getpixel((0,0)), (255,0,0,255))
        self.assertEqual(image.getpixel((0,1)), (0,255,0,255))
        self.assertEqual(image.getpixel((1,1)), (0,0,0,0))
        self.assertEqual((meta['offset_x'],meta['offset_y']),(-3,7))

    def test_rgb565_frame(self):
        image, _ = extract.decode_frame(*fixture(5,1,2,struct.pack('<4H',0x07e0,0,0xf800,0)),0)
        self.assertEqual(image.getpixel((0,0)), (255,0,0,255))
        self.assertEqual(image.getpixel((0,1)), (0,255,0,255))

    def test_malformed_frames_rejected(self):
        wzl,wzx = fixture()
        cases = [(wzl[:63],wzx), (wzl,wzx[:47]), (wzl,wzx[:-1]),
                 (wzl[:-1],wzx), (wzl,bytes(44)+struct.pack('<II',1,0)),
                 (wzl,bytes(44)+struct.pack('<II',1,99999)),
                 fixture(9), fixture(width=0), fixture(raw=bytes(1000))]
        for pair in cases:
            with self.subTest(pair=tuple(len(v) for v in pair)):
                with self.assertRaises(ValueError):
                    extract.decode_frame(*pair,0)
        for index in (-1,1):
            with self.assertRaises(ValueError): extract.decode_frame(wzl,wzx,index)
        for palette in (None, [], [[True,0,0]]*256):
            with self.assertRaises(ValueError): extract.decode_frame(wzl,wzx,0,palette)

if __name__ == '__main__':
    unittest.main()
