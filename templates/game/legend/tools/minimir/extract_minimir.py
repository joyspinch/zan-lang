"""迷你传奇 (D:/game/迷你传奇) data/*.dll 资源包提取器。

静态逆向 game.exe 得到的完整算法，见同目录 README.md。自包含：RC4 状态
内嵌于下方 PACK_KEY/PACK_TABLE（来自运行中进程堆里的包对象，与路径无关，
10 个包同一密钥），不依赖任何外部状态文件。

用法（Python 3，无第三方依赖）：
    python extract_minimir.py                     # 提取到 <repo>/_scratch/minimir/extract_full
    python extract_minimir.py --out DIR           # 自定输出目录
    python extract_minimir.py --game DIR          # 自定游戏目录（默认 D:/game/迷你传奇）

输出：OUT/<pack>/<name>.<ext>。item/show/ui/boss/skill 条目有名；map/sound/
car/city 条目名为空，按出现顺序编号。
"""

import argparse
import os
import struct
import zlib

# 运行中进程包对象 +0x14 处的 32 字节密钥（ASCII）
PACK_KEY = b"23ea0c7fdc0c73ca7803f78896383e53"
# 包对象 +0x35 处的 256 字节 RC4 S 盒（KSA 后的初始状态，i=j=0）
PACK_TABLE = bytes.fromhex(
    "b99342aba9d4c576d357e0ce6e9772117a0d1eead01644264e45d858637fc449"
    "a0e54fbf5e836b2bebb20e189c19b81caa00663905c13412419e36ae14796743"
    "5aa4edb47ed7f0ca031092f9c8bb5585cce3a77309a34d522cbd3033b7f52e07"
    "cb06232269960c9a27cd91f361fd78be8251b61f9df7048e7dfc3bcffaec5487"
    "b0c0ee62c7530a8859da5ddd408d4b3c8a4a1a8ce16cf8ff312917706a08fbd5"
    "5c957b643d3fb33a6d2ddb48c6d2d9e42f13fe2aeff2d601c935323e89dc8f9b"
    "02d1acde80f6dfe84c9846e2159fb1e6374786a1a55f287c20251dc30b842450"
    "b5f4c26f8b5b940f906860e9ad651ba856a299777421f1e7ba813875a6bc71af"
)

PACKS = ["item", "map", "show", "boss", "skill", "ui", "sound", "car", "city", "tupu"]


def rc4_ksa(key):
    S = list(range(256))
    j = 0
    for i in range(256):
        j = (j + S[i] + key[i % len(key)]) & 0xFF
        S[i], S[j] = S[j], S[i]
    return S + [0, 0]  # S[256]=i, S[257]=j


def prga_xor(buf, S):
    out = bytearray(len(buf))
    i, j = S[256], S[257]
    for k, b in enumerate(buf):
        i = (i + 1) & 0xFF
        a = S[i]
        j = (j + a) & 0xFF
        S[i], S[j] = S[j], S[i]
        out[k] = b ^ S[(S[i] + S[j]) & 0xFF]
    S[256], S[257] = i, j
    return bytes(out)


def drop(S, n):
    i, j = S[256], S[257]
    for _ in range(n):
        i = (i + 1) & 0xFF
        j = (j + S[i]) & 0xFF
        S[i], S[j] = S[j], S[i]
    S[256], S[257] = i, j


def decrypt_range(data, off, length):
    """game.exe 0x53df40：按 4096 字节分块的 RC4，块密钥由密钥流双字播种。"""
    S = list(PACK_TABLE) + [0, 0]
    block = off >> 12
    drop(S, block * 4)
    ksbuf = prga_xor(b"\x00" * ((length >> 12) * 4 + 8), S)
    out = bytearray()
    pos, rem, ctr, idx = off, length, block, 0
    offb = off & 0xFFF
    first = offb > 0
    while rem > 0:
        dw = int.from_bytes(ksbuf[idx * 4:idx * 4 + 4], "little")
        key40 = dw.to_bytes(4, "little") + PACK_KEY + (dw ^ ctr).to_bytes(4, "little")
        S2 = rc4_ksa(key40)
        drop(S2, (offb + 0x24) if first else 0x24)
        n = min((0x1000 - offb) if first else 0x1000, rem)
        first = False
        out += prga_xor(data[pos:pos + n], S2)
        pos += n
        rem -= n
        ctr += 1
        idx += 1
    return bytes(out)


def ext_of(b):
    if b[:8] == b"\x89PNG\r\n\x1a\n":
        return ".png"
    if b[:6] in (b"GIF89a", b"GIF87a"):
        return ".gif"
    if b[:3] == b"\xff\xd8\xff":
        return ".jpg"
    if b[:3] == b"ID3" or (len(b) > 1 and b[0] == 0xFF and (b[1] & 0xE0) == 0xE0):
        return ".mp3"
    if b[:4] == b"RIFF":
        return ".wav"
    if b[:2] == b"BM":
        return ".bmp"
    if b[:4] == b"OggS":
        return ".ogg"
    return ".bin"


def extract_pack(data, outdir):
    dec = decrypt_range(data, 0, len(data))
    if dec[:4] != b"ZMS\x00":
        raise ValueError(f"bad magic {dec[:8].hex()}")
    count = struct.unpack_from("<I", dec, 4)[0]
    os.makedirs(outdir, exist_ok=True)
    pos = 8
    n = 0
    for i in range(count):
        e = dec.find(b"\x00", pos)
        if e < 0 or e - pos > 260:
            break
        rawname = dec[pos:e].decode("gbk", "replace")
        name = rawname or f"{i:05d}"
        zsize = struct.unpack_from("<I", dec, e + 1)[0]
        zdata = dec[e + 13:e + 13 + zsize]
        if len(zdata) < zsize:
            break
        try:
            raw = zlib.decompress(zdata)
            with open(os.path.join(outdir, name + ext_of(raw)), "wb") as f:
                f.write(raw)
        except zlib.error:
            pass
        pos = e + 13 + zsize
        n += 1
    return n, count


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.abspath(os.path.join(here, "..", "..", "..", ".."))
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--game", default=r"D:\game\迷你传奇\data")
    ap.add_argument("--out", default=os.path.join(repo, "_scratch", "minimir", "extract_full"))
    args = ap.parse_args()
    total = 0
    for pack in PACKS:
        path = os.path.join(args.game, pack + ".dll")
        if not os.path.exists(path):
            print(f"{pack}: missing, skipped")
            continue
        n, count = extract_pack(open(path, "rb").read(), os.path.join(args.out, pack))
        total += n
        print(f"{pack}: {n}/{count}")
    print(f"TOTAL {total}")


if __name__ == "__main__":
    main()
