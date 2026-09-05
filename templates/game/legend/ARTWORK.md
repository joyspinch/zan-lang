# 导航与客户端素材

## 导航图标

`assets/generated/navigation-atlas.png` 是本次通过本地配置的 Image2 接口生成的原始图集（1536×1024）。提示词保存在 `tools/navigation-prompt.txt`，不包含密钥。`assets/generated/navigation/` 内为审核裁切后的 32 张 128×128 RGBA 图标，内容缩放到 112×112 内并保留透明边缘。

- `data/navigation.csv`：稳定路由 ID、中文名称、图标路径；0–29 是三排导航，30 是设置，图标 31 预留为药水。
- 不使用取模重复分配图标；每个导航入口有独立素材。
- 图集单元并非严格等宽，裁切线已经逐行校准；换一张图集时必须重新检查裁切边界，不能直接沿用。
- 运行游戏不需要 Python。重新制作素材需要 Python 3 + Pillow：

```powershell
python templates/game/legend/tools/prepare_navigation.py
python tests/templates/legend/test_artwork.py
```

## 客户端素材

`assets/client/portrait-warrior.png` 从用户指定的 `D:\game\传奇客户端\data\prguse3.wzl` 第 365 帧提取。原图 56×54，8 位调色板；同名 JSON 保留包文件、索引文件、调色板的 SHA-256 和帧偏移信息。只读取素材包，未执行客户端程序。

`tools/extract_wzl.py` 支持 WZL/WZX 的 8 位调色板与 16 位 RGB565、四字节行对齐和底向上像素排列，并校验索引、边界和解压长度。**不支持 PAK/GEEPAK3；没有声称提取了整个客户端。**

```powershell
python templates/game/legend/tools/extract_wzl.py --library 'D:/game/传奇客户端/data/prguse3.wzl' --frame 365 --palette templates/game/legend/assets/client/palette.json --output templates/game/legend/assets/client/portrait-warrior.png
```

`palette.json` 的经典 256 色表依据 Suprcode/Crystal 的 `LibraryEditor/Graphics/WeMadeLibrary.cs` 中调色板核对。参考仓库：`https://github.com/Suprcode/Crystal`。不能从调色板或格式兼容性推断素材授权；用户称客户端素材经过二次创作，正式对外发布前仍应核实使用范围。

其余仍在使用的参考截图裁切素材尚未全部替换，装备外观也尚未做到每件装备独立图像；不要将这次导航素材更新视为全游戏美术完成。

## 排版验收

三排采用图标左、文字右：按钮高 40，图标 24×24，内边距与图文间距 4，网格间距 4（逻辑像素）。顶部两排共享十列网格；底部因位于中间内容区，在自身区域等分十列。允许 DPI 换算后列宽有一像素舍入差。

真实 UiDriver 的 `dump tree` 输出可以用以下脚本检查按钮高度、横向子元素顺序、垂直居中、间距、同排等宽、顶部逐列对齐与文字不越界：

```powershell
python tests/templates/legend/test_navigation_layout.py _scratch/legend/horizontal-tree.json _scratch/legend/inventory-horizontal-tree.json _scratch/legend/forge-horizontal-tree.json
```

该检查需要真实运行产生的树文件，不会用模拟坐标冒充运行验证。
