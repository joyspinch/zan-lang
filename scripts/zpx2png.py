#!/usr/bin/env python3
import struct, zlib, sys
# ZPX1: 24B header + little-endian u32 per pixel, memory order B,G,R,A.
def zpx2png(src, dst):
    d = open(src,'rb').read()
    assert d[:4]==b'ZPX1'
    x,y,w,h,bpp = struct.unpack_from('<5I', d, 4)
    px = d[24:24+w*h*4]
    out = bytearray(w*h*4)
    out[0::4] = px[2::4]  # R
    out[1::4] = px[1::4]  # G
    out[2::4] = px[0::4]  # B
    out[3::4] = px[3::4]  # A
    raw = b''.join(b'\x00'+bytes(out[r*w*4:(r+1)*w*4]) for r in range(h))
    def chunk(t,data):
        c=struct.pack('>I',len(data))+t+data
        return c+struct.pack('>I',zlib.crc32(t+data)&0xffffffff)
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw,6))+chunk(b'IEND',b'')
    open(dst,'wb').write(png)
    print(dst, w, 'x', h)
zpx2png(sys.argv[1], sys.argv[2])
