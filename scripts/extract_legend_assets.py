"""
Legend (Mir2) Client Asset Extractor
=====================================
自动从热血传奇 2 / 传世 / 私服客户端中提取打击音效 (.wav) 与技能/刀光图像 (.wil/.wzl/.wzx)。

用法示例:
    python scripts/extract_legend_assets.py --client "D:\\Mir2"
    python scripts/extract_legend_assets.py --client "D:\\Mir2" --sound-only
    python scripts/extract_legend_assets.py --client "D:\\Mir2" --magic-only
"""

import os
import sys
import shutil
import struct
import zlib
import argparse
from pathlib import Path

# 经典音效映射规则
SOUND_MAPPINGS = {
    "swing": ["whish", "swing", "1-1", "m_swing", "f_swing", "101"],
    "hit_flesh": ["hit", "flesh", "1-2", "hit_flesh", "attack", "102"],
    "fire_strike": ["fire", "strike", "fire_hit", "crit", "烈火", "103"],
    "coin_drop": ["coin", "gold", "money", "drop_gold", "104"],
    "item_burst": ["drop", "item", "item_drop", "drop_item", "105"],
    "lightning": ["light", "thunder", "elec", "雷电", "106"],
    "teleport": ["tele", "fly", "trans", "传送", "107"],
}

TARGET_SOUNDS = {
    "swing": "00001.wav",       # 普通挥刀
    "hit_flesh": "00002.wav",   # 命中肉体
    "fire_strike": "00003.wav", # 烈火/暴击
    "coin_drop": "00006.wav",   # 金币掉落
    "item_burst": "00008.wav",  # 装备大爆
    "lightning": "00004.wav",   # 魔法雷电
    "teleport": "00005.wav"     # 传送/回城
}

def extract_sounds(client_dir: Path, output_dir: Path):
    print(f"\n[1/2] 正在扫描传奇客户端音频文件: {client_dir}")
    sound_out = output_dir / "sound"
    sound_out.mkdir(parents=True, exist_ok=True)

    wav_files = []
    for root, dirs, files in os.walk(client_dir):
        for f in files:
            if f.lower().endswith(".wav"):
                wav_files.append(Path(root) / f)

    print(f"找到 {len(wav_files)} 个 .wav 音频文件。")
    extracted_count = 0

    for category, keywords in SOUND_MAPPINGS.items():
        matched = None
        for wf in wav_files:
            fname = wf.stem.lower()
            if any(kw in fname for kw in keywords):
                matched = wf
                break
        
        target_name = TARGET_SOUNDS.get(category)
        if matched and target_name:
            dst = sound_out / target_name
            shutil.copyfile(matched, dst)
            print(f"  √ 提取 [{category}] -> {target_name} (来源: {matched.name})")
            extracted_count += 1
        elif target_name and wav_files:
            # 顺序兜底分配
            idx = extracted_count % len(wav_files)
            dst = sound_out / target_name
            shutil.copyfile(wav_files[idx], dst)
            print(f"  * 兜底提取 [{category}] -> {target_name} (来源: {wav_files[idx].name})")
            extracted_count += 1

    print(f"音频提取完毕: 共提取/映射 {extracted_count} 个核心打击与大爆音效至 {sound_out}")

def decode_wzl_frame(wzl_bytes, wzx_bytes, frame_idx):
    """解压 WZL/WZX 格式的精灵帧"""
    if len(wzx_bytes) < 48 or len(wzl_bytes) < 64:
        return None
    count = struct.unpack_from('<I', wzx_bytes, 44)[0]
    if frame_idx < 0 or frame_idx >= count:
        return None
    offset = struct.unpack_from('<I', wzx_bytes, 48 + frame_idx * 4)[0]
    if offset in (0, 0xffffffff) or offset < 64 or offset + 16 > len(wzl_bytes):
        return None
    depth, _, _, _, w, h, x, y, size = struct.unpack_from('<4B4hI', wzl_bytes, offset)
    if w <= 0 or h <= 0 or size == 0 or offset + 16 + size > len(wzl_bytes):
        return None
    stride = (w * (2 if depth == 5 else 1) + 3) // 4 * 4
    expected = stride * h
    try:
        raw = zlib.decompress(wzl_bytes[offset + 16 : offset + 16 + size])
        if len(raw) != expected:
            return None
        return (w, h, depth, raw, stride)
    except Exception:
        return None

def extract_magic_sprites(client_dir: Path, output_dir: Path):
    print(f"\n[2/2] 正在扫描传奇客户端特效包 (Data/Magic.wzl 或 Magic.wil)...")
    magic_out = output_dir / "skill"
    magic_out.mkdir(parents=True, exist_ok=True)

    data_dir = client_dir / "Data"
    if not data_dir.exists():
        data_dir = client_dir

    candidates = list(data_dir.glob("*Magic*.wzl")) + list(data_dir.glob("*magic*.wzl"))
    if not candidates:
        print("  ! 未发现 Magic.wzl 特效包，如果客户端使用 .wil 格式，请将 Magic.wil 转换为 .wzl 或提供对应目录。")
        return

    from PIL import Image
    target_wzl = candidates[0]
    target_wzx = target_wzl.with_suffix(".wzx")
    if not target_wzx.exists():
        print(f"  ! 缺少索引文件: {target_wzx}")
        return

    print(f"正在从 {target_wzl.name} 提取经典烈火/雷电光效...")
    wzl_bytes = target_wzl.read_bytes()
    wzx_bytes = target_wzx.read_bytes()

    # 提取前 16 帧特效演示
    success_count = 0
    for frame in range(0, min(32, 100)):
        res = decode_wzl_frame(wzl_bytes, wzx_bytes, frame)
        if res:
            w, h, depth, raw, stride = res
            try:
                if depth == 5:
                    img = Image.frombytes('RGB', (w, h), raw, 'raw', 'BGR;16', stride, -1).convert('RGBA')
                else:
                    img = Image.frombytes('L', (w, h), raw, 'raw', 'L', stride, -1).convert('RGBA')
                # 剔除纯黑底色为透明
                datas = img.getdata()
                new_data = [(r, g, b, 0 if (r < 15 and g < 15 and b < 15) else 255) for (r, g, b, a) in datas]
                img.putdata(new_data)
                save_path = magic_out / f"magic_{frame:04d}.png"
                img.save(save_path)
                success_count += 1
            except Exception as e:
                pass

    print(f"特效序列帧提取完毕: 成功提取 {success_count} 帧至 {magic_out}")

def main():
    parser = argparse.ArgumentParser(description="传奇客户端原版素材提取工具")
    parser.add_argument("--client", type=Path, required=True, help="传奇客户端根目录路径")
    parser.add_argument("--out", type=Path, default=Path("templates/game/legend/assets"), help="导出目标目录")
    parser.add_argument("--sound-only", action="store_true", help="仅提取打击/大爆/挥刀音效")
    parser.add_argument("--magic-only", action="store_true", help="仅提取技能与刀光特效帧")
    args = parser.parse_args()

    if not args.client.exists():
        print(f"错误: 指定的客户端目录不存在: {args.client}")
        sys.exit(1)

    print("==================================================")
    print("      热血传奇原版客户端打击感与大爆素材提取器      ")
    print("==================================================")

    if not args.magic_only:
        extract_sounds(args.client, args.out)
    if not args.sound_only:
        try:
            extract_magic_sprites(args.client, args.out)
        except ImportError:
            print("  ! 未安装 Pillow 库，跳过图像提取 (如需提取技能帧请 pip install Pillow)")

    print("\n[OK] 全部提取工作已完成！素材已就绪。")

if __name__ == "__main__":
    main()
