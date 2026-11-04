# 游戏美术素材

## 真实素材源：迷你传奇资源包提取（权威）

`D:\game\迷你传奇` 的 10 个 `data/*.dll` 资源包已完成加密破解与全量提取：
**6843 条目 → 6058 PNG + 87 GIF + 671 JPG + 19 WAV，0 失败**，全部通过
PIL 完整性校验。

- 工具与算法文档：`tools/minimir/`（`extract_minimir.py` 自包含提取器 +
  `README.md` 完整逆向记录：包对象布局、按 4096 字节分块的 RC4 链、ZMS
  目录格式）。密钥来自运行中进程堆对象的只读转储，不附加调试器。
- 复现：`python templates/game/legend/tools/minimir/extract_minimir.py`
  （默认输出 `_scratch/minimir/extract_full/<pack>/`；`_scratch` 会被清理，
  需要时重跑即可，全量约 30 秒）。
- 关键映射：物品图标 `data/items.csv` 的 `picture` 列（1–2819）与
  `item/00001.png…02819.png` 一一对应；地图卡对应 `map/`；怪物/装备外观的
  具体包归属（show/boss/car/tupu）尚在逐个比对确认。

## 客户端素材

`assets/client/portrait-warrior.png` 从用户指定的 `D:\game\传奇客户端\data\prguse3.wzl` 第 365 帧提取。原图 56×54，8 位调色板；同名 JSON 保留包文件、索引文件、调色板的 SHA-256 和帧偏移信息。只读取素材包，未执行客户端程序。

`tools/extract_wzl.py` 支持 WZL/WZX 的 8 位调色板与 16 位 RGB565、四字节行对齐和底向上像素排列，并校验索引、边界和解压长度。**不支持 PAK/GEEPAK3；没有声称提取了整个客户端。** 解码器回归见 `tests/templates/legend/test_extract_wzl.py`。

```powershell
python templates/game/legend/tools/extract_wzl.py --library 'D:/game/传奇客户端/data/prguse3.wzl' --frame 365 --palette templates/game/legend/assets/client/palette.json --output templates/game/legend/assets/client/portrait-warrior.png
```

`palette.json` 的经典 256 色表依据 Suprcode/Crystal 的 `LibraryEditor/Graphics/WeMadeLibrary.cs` 中调色板核对。参考仓库：`https://github.com/Suprcode/Crystal`。不能从调色板或格式兼容性推断素材授权；用户称客户端素材经过二次创作，正式对外发布前仍应核实使用范围。

## 已移除的生成素材流程

导航图集（Image2 生成 128×128 图标）、36 张 Image2 怪物套装、参考截图裁切
（`assets/reference/`、`assets/processed/`）与 12 类×3 档生成装备图及其
`tools/prepare_*.py` 脚本已全部移除（提交 184cedcf 移除入库素材，本次移除
配套工具）。`data/navigation.csv`、`data/monster_art.csv`、
`data/equipment_art.csv` 仍指向旧路径，接入提取原图时一并切换；客户端启动
校验（`main.zan`）对缺失图标显式报错，不静默降级。
