"""Check game pixels and probes produced by gameplay.ui."""
import json
import struct
import sys
from pathlib import Path

from PIL import Image, ImageChops


def pixels(path):
    raw = path.read_bytes()
    magic, x, y, width, height, stride = struct.unpack_from("<4s5i", raw)
    assert magic == b"ZPX1" and stride == 4, path
    return Image.frombytes("RGBA", (width, height), raw[24:], "raw", "BGRA"), (x, y)


def find(node, name):
    if node["name"] == name:
        return node
    for child in node.get("kids", []):
        result = find(child, name)
        if result is not None:
            return result
    return None


out = Path(sys.argv[1])
results = (out / "results.log").read_text(encoding="utf-8")
assert "DONE pass=19 fail=0" in results and "NOT FOUND" not in results, results
assert int((out / "sprite-size.txt").read_text()) > 0, "SVG did not rasterize"
assert (out / "pose-0.txt").read_text() != (out / "pose-1.txt").read_text(), "Dance clock did not advance"
root = json.loads((out / "initial-tree.json").read_text(encoding="utf-8"))["root"]
cell = find(root, "Cell40")
assert cell is not None
initial, origin = pixels(out / "initial.zpx")
dance, dance_origin = pixels(out / "dance.zpx")
assert initial.size == dance.size and origin == dance_origin
cx = cell["x"] + cell["w"] // 2 - origin[0]
cy = cell["y"] + cell["h"] // 2 - origin[1]
size = cell["w"] * 8 // 5
box = (cx - size, cy - size * 3 // 2, cx + size, cy + size // 2)
a = initial.crop(box).convert("RGB")
b = dance.crop(box).convert("RGB")
rgb = a.tobytes()
fur = sum(rgb[i] > 180 and 100 < rgb[i + 1] < 220 and rgb[i + 2] < 160 and rgb[i] > rgb[i + 1] + 20 for i in range(0, len(rgb), 3))
diff = ImageChops.difference(a, b).tobytes()
changed = sum(max(diff[i:i + 3]) > 8 for i in range(0, len(diff), 3))
assert fur > 100, f"Cat artwork missing: {fur} fur pixels"
assert changed > 40, f"Idle cat did not visibly dance: {changed} changed pixels"
layout = out / "layout.log"
assert not layout.exists() or not layout.read_text(encoding="utf-8").strip(), "Layout diagnostics were not empty"
print(f"PASS dancing SVG cat: {fur} fur pixels, {changed} animated pixels, 19 game assertions, clean layout")
