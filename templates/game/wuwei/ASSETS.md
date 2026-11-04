# 美术与音频资产

## 来源与管线

素材从原作《无为修仙传》提取（原版即为 WebP，共 52.77 MiB），
`tools/prepare_assets.py` 用 Pillow 统一重编码进 `assets/images/`：

| 类别 | 处理 | 参数 |
|------|------|------|
| 场景/道具大图 | 有损 WebP，最长边封顶 1920×1200 | q85, method=6 |
| 小图标（≤256px） | 有损 WebP，保持锐边 | q90, method=6 |
| `title_calligraphy` | 无损 WebP + 原作题字着色器烘焙 | lossless, exact |

结果：**365 张 WebP，共 11.88 MiB**（同一批图按 PNG 中转时是
74 MiB）。管线只发 WebP，会清掉历史遗留的 `.png`。

## 题字着色器

原作 `Main_decompiled.gd` 对题字图执行
`paper = min(c.r, c.g, c.b); alpha = 1 - smoothstep(0.82, 0.94, paper)`，
把纸色渐隐成透明。`prepare_assets.py` 按同一公式逐像素烘焙进 alpha
通道（每字节只舍入一次），无损保存——墨边与红印与原作逐像素一致，
运行时不再需要着色器。

## 已知原作瑕疵

`bg_cave`、`bg_mountains` 边缘的白边来自原作自带的 WebP（提取件与
原文件逐字节一致），不是本次转换引入的。

## 音频

`assets/audio/bgm_main.ogg` 等经 System.Audio 播放，主菜单进入后循环播放。

## 内嵌与校验

发布构建把 `assets/` 整体内嵌进 exe（`--embed assets=assets`），
运行时经 `File.EmbedExists` / 内存解码（`zan_gui_image_load_mem`，
支持静态 WebP）直接读取，产物目录不留散装文件。
`tools/test.ps1` 用 `embeddedArt` / `embeddedMusic` / `menuArtLoaded`
探针确认资源确实来自内嵌且解码成功。
