"""Read-only extraction of a selected WZL/WZX sprite (8-bit palette / RGB565).
No game executable is run. PAK/GEEPAK3 is explicitly NOT supported.
Pillow required; --palette is a 256-entry RGB JSON array for 8-bit packages.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib
from PIL import Image

MAX_PIXELS = 4096 * 4096

def decode_frame(wzl, wzx, frame, palette=None):
    if len(wzx) < 48 or len(wzl) < 64:
        raise ValueError('Truncated WZL/WZX header')
    count = struct.unpack_from('<I', wzx, 44)[0]
    if count > (len(wzx)-48)//4:
        raise ValueError('Truncated WZX offsets')
    if frame < 0 or frame >= count:
        raise ValueError('Frame index outside WZX')
    offset = struct.unpack_from('<I', wzx, 48+frame*4)[0]
    if offset in (0, 0xffffffff):
        raise ValueError('Empty frame')
    if offset < 64 or offset+16 > len(wzl):
        raise ValueError('Frame offset outside WZL')
    depth, _, _, _, w, h, x, y, size = struct.unpack_from('<4B4hI', wzl, offset)
    if depth not in (3,5):
        raise ValueError(f'Unsupported pixel depth {depth}')
    if w <= 0 or h <= 0 or w*h > MAX_PIXELS:
        raise ValueError('Invalid sprite dimensions')
    if size == 0 or offset+16+size > len(wzl):
        raise ValueError('Truncated compressed frame')
    stride = (w*(2 if depth == 5 else 1)+3)//4*4
    expected = stride*h
    inflater = zlib.decompressobj()
    raw = inflater.decompress(wzl[offset+16:offset+16+size], expected+1)
    if len(raw) != expected or not inflater.eof or inflater.unused_data or inflater.unconsumed_tail:
        raise ValueError('Invalid decompressed frame length or stream')
    if depth == 5:
        image = Image.frombytes('RGB',(w,h),raw,'raw','BGR;16',stride,-1).convert('RGBA')
    else:
        if (not isinstance(palette,list) or len(palette) != 256 or
            any(not isinstance(rgb,list) or len(rgb) != 3 or
                any(type(v) is not int or not 0 <= v <= 255 for v in rgb) for rgb in palette)):
            raise ValueError('8-bit frame needs 256 RGB palette entries')
        image = Image.frombytes('P',(w,h),raw,'raw','P',stride,-1)
        image.putpalette([v for rgb in palette for v in rgb]); image = image.convert('RGBA')
    image.putdata([(r,g,b,0 if (r,g,b)==(0,0,0) else 255) for r,g,b,a in image.getdata()])
    return image, {'frame':frame,'width':w,'height':h,'offset_x':x,'offset_y':y,'pixel_depth':depth}

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--library',type=Path,required=True)
    p.add_argument('--frame',type=int,required=True)
    p.add_argument('--palette',type=Path)
    p.add_argument('--output',type=Path,required=True)
    args = p.parse_args()
    if args.library.suffix.lower() != '.wzl':
        p.error('Only WZL/WZX supported; PAK is not decoded by this tool')
    if args.output.suffix.lower() != '.png':
        p.error('Output must be a PNG, never overwrite the source package')
    wzl = args.library.read_bytes(); wzx = args.library.with_suffix('.wzx').read_bytes()
    palette = json.loads(args.palette.read_text(encoding='utf-8')) if args.palette else None
    image, meta = decode_frame(wzl,wzx,args.frame,palette)
    meta.update(library=args.library.name, source_sha256=hashlib.sha256(wzl).hexdigest(),
                index_sha256=hashlib.sha256(wzx).hexdigest())
    if palette is not None:
        meta['palette_sha256'] = hashlib.sha256(args.palette.read_bytes()).hexdigest()
    args.output.parent.mkdir(parents=True,exist_ok=True)
    image.save(args.output)
    args.output.with_suffix('.json').write_text(json.dumps(meta,indent=2),encoding='utf-8')
    print(f'Extracted frame {args.frame}: {image.width}x{image.height} -> {args.output}')
