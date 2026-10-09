# 原生绘图、图片、游戏与音频驱动

当前实现（源码核对：2026-10-08）。四个 driver 按职责拆分，应用按实际调用携带依赖。**win-x64 已完成原生动态验证；wasm32 已完成离屏 WASI 编译、链接与运行验证。其他平台须重建对应驱动后使用，并完成目标平台验证。**

## 模块与使用入口

| Driver | 职责 | Zan 使用入口 |
| --- | --- | --- |
| `zan_gui` | 窗口、surface、基础绘图、文字、裁剪、表面合成及 CPU/GPU 绘图 context | `Gui.Canvas`、GUI 控件 |
| `zan_image` | 图片解码、SVG 光栅化、文件及内存图片缓存 | `System.Drawing.Imaging.ImageCodec` |
| `zan_game` | sprite 句柄、烘焙、批量绘制，以及 3D 网格上传与绘制；复用 GUI context 和图片缓存 | Canvas 的 sprite/3D 包装及 `Game.Graphics` |
| `zan_audio` | WAV/Vorbis 解码、音频设备、播放与混音，独立于 GUI | `System.Media` |

图片模块一次带上其 codec 集合；选择不同图片格式不会分别引入一个 driver。图片及音频可在没有窗口的程序中独立使用。

`ImageCodec` 的静态 API：

- `Width(string key)`、`Height(string key)`：首次读取文件时解码并缓存，失败返回 `0`。
- `LoadMem(string key, string data, int len)` 及 `byte[]` 重载：注册压缩图片字节，返回宽度，失败返回 `0`。
- `LoadSvg(string key, string svg, int rasterW, int rasterH)`；字节重载为 `LoadSvg(string key, byte[] svg, int len, int rasterW, int rasterH)`：按比例适配目标盒，尺寸不大于零时使用固有尺寸。
- `Evict(string key)`：驱逐缓存；`MemReport()`：图片缓存统计。
- `View(string key) -> nint`：借用 native bitmap 描述符，仅供同步 graphics bridge 转发；Zan 代码不得读取、释放或跨缓存变动保留它。

内存 key 使用 `mem:` 前缀。`LoadMem`/`LoadSvg` 重复注册活跃 key 保留首次结果；需要替换时先驱逐再加载。输入字节仅在加载调用期间借用，返回后可释放。

## 常见依赖

| 实际使用功能 | 所需 driver |
| --- | --- |
| 基础绘图、文字与 surface 合成 | `zan_gui` |
| 图标（现有 SVG icons 路径） | `zan_gui` + `zan_image` |
| 仅图片解码、尺寸查询或缓存 | `zan_image` |
| 仅音频 | `zan_audio` |
| sprite / 3D 游戏绘制 | `zan_gui` + `zan_image` + `zan_game` |

音频按调用另加 `zan_audio`。Canvas 保留公共图片、sprite 和 3D API 签名；图片包装转发到 ImageCodec，sprite/3D 包装调用游戏 driver。离屏预览可保留 Canvas，以 `BlitSurface` / `BlitSurfaceScaled` 合成，无需烘焙成游戏贴图。SVG icons 继续使用原图片路径与外观。

## Windows x64 产物与验收

本次 Release 构建的共享库尺寸：

| Driver | 字节 | KiB |
| --- | ---: | ---: |
| `zan_gui` | 324,096 | 316.5 |
| `zan_image` | 740,864 | 723.5 |
| `zan_audio` | 221,696 | 216.5 |
| `zan_game` | 118,272 | 115.5 |

同工具链拆分前 GUI 为 993,792 字节；基础 GUI 减少约 67.4%。全部模块之和并未减少；收益是未使用的功能不进入应用依赖。图标依赖 SVG，因此实际使用 SVG 图标的程序仍携带图片模块。

已运行五种功能程序各自的 shared/static 发布：基础绘图、独立图片、独立音频、sprite 游戏与图标。检查执行结果、PE 导入和随行 DLL，shared 依赖与上表的常见依赖一致，static 不携带 Zan DLL；音频通过 WASAPI 验证 WAV/Vorbis 加载和静音播放。六个原生绘图用例及现有图片、图标、sprite conformance 用例通过，覆盖 CPU/GPU 像素、状态与资源生命周期契约。

实际设计器用例的发布产物仅导入并携带 `zan_gui.dll` 和 `zan_image.dll`，没有 game/audio 依赖。设计器完整 conformance 仍有 22 个 CSS DPI 断言失败：`Style.ScaleLayout` 未缩放像素行高、字距和边框。新增检查直接创建 App，不调用设计器或表面合成；该遗漏在拆分前的 HEAD 也存在，已独立登记为 B-ID123。此处只确认设计器发布依赖，不能将其完整用例记为通过。

## WebAssembly 与静态库验收

Wasm 分别构建 `zanrt_gui.o`、`zanrt_image.o`、`zanrt_audio.o` 和 `zanrt_game.o`；编译器按程序引用选入对象，并为 game 补齐 GUI/image 依赖。SVG 图标使用的压缩资源另选入 `zan_inflate.o`。构建配方和 stale 检查位于 `scripts/build_cross_rt.cmd` 的 Wasm 段与 `scripts/check_toolchain_stale.py`。

五种功能程序均已在 Node WASI preview1 下离屏运行，断言像素或解码结果，并检查链接中没有指针 ABI signature mismatch。该验收不包含浏览器窗口、WebGL 或音频设备；WASI 音频可离线解码 WAV/Vorbis，打开设备仍返回原有的 backend not available 错误。

发布编译器的定向回归覆盖未用驱动裁剪、分片、跨包递归依赖、旧依赖刷新和静态库符号索引。静态库还以真实 Zan 应用验证跨模块 weak 清除及重复公共导出诊断，并以 C 程序配合 SDK timer runtime 验证对象、数组和 weak 生命周期；未使用 whole-archive 或临时符号桩。

## Native ABI 与像素所有权

旧 `zan_gui` 的 image/game/audio FFI 导出已移到各自 driver；**这是 native ABI 变更**。即使 Canvas 公共签名不变，应用仍需重新编译，并与配套驱动一起更新；直接 FFI 消费者须同步调整 DllImport owner 和入口名。动态库、导入库及静态库应来自同一套更新，不能混用拆分前后的产物。

GUI 持有绘图 context，game 在同一 context 内绘制。原生 GPU 扩展通过 `zan_gui_gpu_begin` / `zan_gui_gpu_end` 进入和退出：GUI 负责提交前序绘图、同步像素、绑定目标及恢复 2D 状态。context/backend 重建后，game 的网格等 GPU 资源需重新建立。

跨模块传递 `zan_bitmap` 时遵循以下职责：

- 像素为直通 alpha 的 ARGB32（`0xAARRGGBB`）；`stride` 是一行的 **32 位像素数**，不是字节数，不可假定等于 width。
- 图片描述符及像素归 `zan_image`，借用有效期截至下一次缓存变动；surface 像素归 GUI，借用有效期截至该 surface 下一次绘制、resize 或 destroy。
- **COPY 由需要长期持有数据的接收方负责**：在有效期内用自己的分配器按 stride 逐行复制，并用同一分配器释放副本。借用方不得释放原数据，也不得把借用指针当成所有权转移。
- `zan_image_register_argb` 同步复制输入到图片模块自己的紧凑存储，结果 stride 等于 width；返回后调用方可释放源缓冲。
- `serial` 标识像素内容版本，不是描述符地址；每次成功注册或替换取得新 serial。`0` 表示不可缓存的 view。纹理消费者负责按内容版本更新缓存，不能仅按指针地址复用旧纹理。

## 构建与暂存

先按 [构建工具链](BUILD_TOOLCHAIN.md) 配置仓库的 `build/`。四个 CMake 开关为 `ZAN_BUILD_GUI`、`ZAN_BUILD_IMAGE`、`ZAN_BUILD_AUDIO`、`ZAN_BUILD_GAME`；game 需要同时启用 GUI 和 image。

在仓库根目录构建四个共享库 target，然后暂存 Windows 驱动：

```powershell
cmake --build build --target zan_gui zan_image zan_audio zan_game
powershell -File scripts/build_gui_driver.ps1 -Target win-x64
```

`build_gui_driver.ps1` 默认处理四个 driver：共享库路径读取 `build/` 中已有 DLL，生成 GNU 导入库并暂存 DLL、导入库及 `.bundle`。静态路径直接编译并生成静态库和 `.libs`：

```powershell
powershell -File scripts/build_gui_driver.ps1 -Static -Target win-x64
```

可用 `-Drivers` 选择模块、`-BuildDir` 指定构建目录；`-StageRoot` 额外生成可供 `--driver-dir` 使用的集中目录，静态产物位于其 `static/` 下。

每个 driver 暂存到自己的 owner 目录，`<target>` 为平台与架构：

| Driver | Owner 目录 |
| --- | --- |
| `zan_gui` | `packages/Zan.Gui/src/Gui/drivers/<target>/` |
| `zan_image` | `packages/Zan.Image/src/System/Drawing/Imaging/drivers/<target>/` |
| `zan_game` | `packages/Zan.Game/src/Game/Graphics/drivers/<target>/` |
| `zan_audio` | `packages/Zan.Desktop/src/System/Media/drivers/<target>/` |

共享产物及 `.bundle` 位于 target 目录；静态库与 `.libs` 位于其 `static/` 子目录。游戏 bundle 声明 GUI/image driver 依赖，音频独立。

其他平台的构建/暂存入口如下，均须使用匹配的工具链和系统依赖；这些路线不能替代动态验收：

| 平台 | 路线 |
| --- | --- |
| Windows ARM64 | `build_gui_driver.ps1 -Target win-arm64`，静态形式加 `-Static`；shared 须先提供同架构的 CMake DLL |
| Linux x64 / ARM64 | `bash scripts/build_linux_gui_static.sh linux-x64` 或 `linux-arm64`，生成 shared/static |
| macOS ARM64 / x64 | 在 macOS 上运行 `bash scripts/build_macos_gui.sh both`，也可指定 `macos-arm64` / `macos-x64` |
| Android ARM64 / x64 | `bash scripts/build_gui_android.sh all`，转入 `build_gui_android_static.sh` 生成 shared/static；配置 NDK 与 `FREETYPE_SRC` |
| OHOS ARM64 / x64 | `bash scripts/build_gui_ohos.sh all`，生成 shared/static；配置 `DEVECO_ROOT` 与 `FREETYPE_SRC` |

核实来源：`CMakeLists.txt`、上述构建脚本、`packages/Zan.Image/src/System/Drawing/Imaging/ImageCodec.zan`，以及 `src/runtime/zan_bitmap.h`、`zan_image.h`、`zan_gui_graphics.h`、`zan_game.h`。
