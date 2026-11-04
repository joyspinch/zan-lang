"""extract_wzl decoder regression tests; authoring dependency: Pillow."""
import importlib.util
from pathlib import Path
import struct
import sys
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


def fixture(depth=3, width=2, height=2, raw=bytes([2,0,0,0,1,2,0,0])):
    payload = zlib.compress(raw)
    wzl = bytes(64) + struct.pack('<4B4hI', depth,0,0,0,width,height,-3,7,len(payload)) + payload
    wzx = bytes(44) + struct.pack('<II', 1,64)
    return wzl,wzx


class ExtractWzlTests(unittest.TestCase):
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
