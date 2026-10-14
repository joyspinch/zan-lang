# 官网内容维护说明：平台能力与证据

核对日期：2026-10-13；本轮调研末尾 HEAD：`fd9b0c01`。这是官网内容的证据备忘，不替代正式兼容性验收。
本次仅只读核对源码、文档、脚本和历史提交，未运行构建、测试或任何目标平台验证；随后按要求只新建本文件，未修改原 docs、capabilities.js 或其他文件，未提交。
下列“已运行”均指仓库历史记录；目标条目、驱动文件、构建脚本或 CI 配置存在，仅能证明实现/配置存在，不能当作实机通过。
“模拟器”包括 Android ABI 转译、DevEco、QEMU；WSL、浏览器、Node WASI 也单独标明，不统称物理设备实机。
所有证据链接均相对本文件；行号是本次快照的导航提示，后续以符号和实际分支为准。

## 七项平台矩阵（官网推荐口径）

| 平台 | 编译、链接与包格式 | GUI 后端 | 历史运行证据与公开边界 |
| --- | --- | --- | --- |
| Windows | x64、ARM64，PE `.exe`；没有 Windows x86 目标 | Win32；两种架构有驱动路径 | x64 为主要交付平台；ARM64 已验证交叉链接，未找到 ARM 设备运行验收；Win7 仅有兼容实现证据 |
| Linux | x64、ARM64；显式交叉目标走 musl 静态 ELF | X11；仍需显示服务，Wayland 环境需 XWayland | x64 有 WSL 执行；ARM64 有 QEMU 运算探针，不能升级为 ARM 桌面 GUI 实机验收 |
| macOS | Intel、Apple Silicon；macOS 11+ Mach-O；IDE 发布层组织 `.app` | Cocoa、NSWindow、WKWebView 已有实现 | 双架构构建/链接与签名结构有记录；交叉 GUI 应用的真实 Mac 验收待补，正式签名/公证未闭环 |
| Android | x64、ARM64，API 28+；CLI ELF；`--emit-apk` 生成已签名 APK | NativeActivity、EGL、FreeType，含触控和生命周期处理 | API 35 x64 模拟器运行；ARM64 有模拟器 ABI 转译记录；不宣称 ARM 手机硬件验收，默认 debug 签名 |
| iOS | 仅 ARM64，iOS 14+ Mach-O；`--emit-ipa` / `-o app.ipa` 封装 IPA | 未找到完整 UIKit 窗口与应用生命周期闭环 | 实验性编译与 IPA 封装；未找到 iPhone/iPad 运行验收，不能称普通设备免证书安装或可直接上架 |
| OpenHarmony | x64、ARM64；静态 ELF、共享 `libmain.so`；HAP 需另行封装 | XComponent、NativeWindow、EGL 已实现 | x64 QEMU CLI、DevEco GUI 已有记录；ARM64 仅构建/链接证据；当前编译器没有 `--emit-hap` |
| WASI / WebAssembly | `wasm32` 输出 `.wasm`；浏览器应用需要 JS 宿主 | `zan_env` 事件/帧桥，浏览器及微信开发工具有 GUI 记录 | Node WASI 的计算/文件/异常/单线程异步已运行；不等于桌面网络、多线程完整支持或一键小游戏发布 |

### 平台关键证据与历史提交

- **Windows**：目标表/架构解析见 [crosscomp.c](../src/compiler/crosscomp.c#L9)，GNU ABI 转换见 [main.c](../src/compiler/main.c#L5126)，ARM64 PE 链接见 [链接分支](../src/compiler/main.c#L7317)。目标表的 MSVC 描述不能替代实际 GNU 链路。
  x64 清空开发工具 PATH 后仍编译运行的历史记录见 [RELEASE](../docs/RELEASE.md#L276)；提交 `7f31a251` 只证明 ARM64 PE 与 GUI DLL 消费链接，不证明 ARM64 设备执行。
  Win7 兼容依据为 [运行时 0x0601](../src/runtime/rt_io.c#L6)、[DirectWrite 降级](../src/runtime/gui_runtime_dwrite.cpp#L7)、[CEF 109 策略](../packages/Zan.Gui.Browser/src/Gui/Component/CefBrowser/CefRuntime.zan#L67)；提交 `f5fe69b4` 说明旧系统 WebView2 自动安装受限。
- **Linux**：实际静态 musl 链接见 [main.c](../src/compiler/main.c#L7043)，GUI 的显示连接见 [XOpenDisplay](../src/runtime/gui_runtime_x11.c#L354)。`linux-x64` 名称中的 GNU triple 不表示此交叉路径依赖 glibc。
  提交 `60d7580b` 记录字体库双架构链接修复及 WSL `EXIT=0`；`a8193b9a`、`27620a8b` 记录 QEMU AArch64 CRC/AES 对比。证据只覆盖相应探针，不推导完整 GUI 验收。
- **macOS**：11.0 与 ad-hoc 签名见 [main.c](../src/compiler/main.c#L7702)，窗口见 [NSWindow](../src/runtime/gui_runtime_mac.m#L396)，浏览器控件见 [WKWebView](../src/runtime/gui_runtime_mac.m#L1735)，应用包见 [MacAppPrepare](../src/ide_zan/src/services/ZanIDE.Package.zan#L27)。
  [平台文档](../docs/platform-targets.md#L144)明确保留真实 Mac 验证缺口；`d4b7438e` 记录 GUI 探针交叉链接；`21c6ad53` 是双架构驱动构建记录，不能视为 GUI 实机验收。
  [异步设计文档](../docs/ASYNC_CPS_DESIGN.md#L251)声称三系统回环验证，应限定为运行时覆盖；[runtime-tests CI](../.github/workflows/runtime-tests.yml#L14)的配置存在不等于本次查验了成功运行。
- **Android**：入口见 [android_main](../src/runtime/gui_runtime_android_native.c#L561)，依赖 `.so` 搜集与打包见 [main.c](../src/compiler/main.c#L8336)，Java 获取见 [apk.c](../src/compiler/apk.c#L616)，debug 签名见 [签名流程](../src/compiler/apk.c#L845)。
  [平台文档](../docs/platform-targets.md#L159)记录模拟器验证，[ARM64 说明](../docs/platform-targets.md#L214)明确包含 x64 模拟器转译；`f3eb9e3a` 是 APK 全链路验收，`1a437365` 修复 APK 装载失败并在模拟器复验。
- **iOS**：链接见 [main.c](../src/compiler/main.c#L7702)，IPA 实际写入二进制、Info.plist、PkgInfo 三项见 [ipa.c](../src/compiler/ipa.c#L275)；`3e284401` 引入交叉编译与封装，无设备运行验收正文。
  [构建脚本](../scripts/build_cross_rt.cmd#L56)表明 `zanrt_gui.o` 来自兼容桥，不能据文件名认定 UIKit；[sync 门控](../src/compiler/main.c#L6283)白名单仍不含 iOS，因此不能称全运行时支持。
- **OpenHarmony**：生命周期桥见 [gui_runtime_ohos.c](../src/runtime/gui_runtime_ohos.c#L87)，共享库/HAP 壳入口链接见 [main.c](../src/compiler/main.c#L6940)；[IDE 发布分支](../src/ide_zan/src/services/ZanIDE.Publish.zan#L160)检测不到 `--emit-hap` 就跳过。
  `5c6288f3` 为 x64 QEMU CLI 验收，`e59be75a` 为 DevEco GUI 画廊交互，`218eda3f` 正文明确“MateBook Pro 模拟器”；`a0396033` 为 ARM64 驱动首建，不是硬件验收。
- **WASI / WebAssembly**：宿主契约见 [gui_runtime_wasm.c](../src/runtime/gui_runtime_wasm.c#L10)，按引用链接 GUI/媒体运行时见 [main.c](../src/compiler/main.c#L7537)，Node/微信证据见 [平台文档](../docs/platform-targets.md#L269)。
  `37325a88` 记录 Node WASI `Task.Yield` 实跑；`892d26b9`、`e2e236ea` 为浏览器画廊/中文验证；`6ad6be0c` 为微信开发工具呈现与点击验证，不能升级为微信手机端发布验收。

## 六项特色：用用户收益连接实现证据

| 特色 | 可维护的表述与证据 |
| --- | --- |
| HTML 界面声明 | HTML 装载进入 Zan Gui 控件体系；证据 [Html.zan](../packages/Zan.Gui/src/Gui/Markup/Html.zan#L31)。避免称完整浏览器或覆盖全部 HTML/CSS。 |
| Binding 语法 | 字段可绑定到 `Binding<T>`，示例包含读取、反向写回与字段更新；证据 [binding_sugar.zan](../tests/conformance/binding_sugar.zan#L28)。用测试说明语义，不声称本次重新跑过。 |
| 源码按需拉入 | 编译器按引用发现并拉入所需源码，减少无关模块进入编译；证据 [main.c](../src/compiler/main.c#L2387)。不要等同于任意依赖均自动安装。 |
| 原生驱动按需发布 | 驱动发现、存活性与 `.bundle` 条件依赖有回归；证据 [run_driver_liveness.cmake](../tests/run_driver_liveness.cmake#L50)。减少手工拷库，但不保证任意第三方库闭包。 |
| IDE 设计与运行一致性 | 设计器预览和运行控件有矩形、像素对比测试；证据 [designer_wysiwyg_test.zan](../tests/gui/designer_wysiwyg_test.zan#L43)。避免扩大成所有控件/所有平台均已验收。 |
| 编译元数据供工具使用 | 语义元数据生成路由、实体和项目索引，复用生成器判定；证据 [GenIndex.zan](../stdlib/System/Compiler/GenIndex.zan#L1)。突出工具/AI 可定位代码，不宣称无需源码即可理解全部行为。 |

统一入口的实际价值：同一 `zanc` 选择目标；[发布驱动闭包](../src/compiler/main.c#L8144)与[资源自动内嵌](../src/compiler/main.c#L6013)减少部署漏库/漏素材；Android 可直接完成 APK 编译、封装和签名。
准确边界：依赖对应目标工具、已提供的驱动及清单；macOS `.app` 在 IDE 层组织，OpenHarmony HAP 尚无编译器直出；不写“所有目标一条命令直接上架”或“业内唯一”。

## 文档漂移（本次只记录，不修改原文）

- [platform-targets 目标表](../docs/platform-targets.md#L19)仍写十四/十五项；当前 [crosscomp.c](../src/compiler/crosscomp.c#L9)有十七个名称，含别名以及仅对象输出的 `riscv32/esp32c3`，不能把名称数当成熟平台数。
- [无移动端/WASM GUI](../docs/platform-targets.md#L115)、[OHOS GUI future](../docs/platform-targets.md#L245)、Cocoa 仅存根等说法已落后；上文后端源码和模拟器/浏览器历史记录足以校正。
- [Windows IDE 仅发布 Windows](../docs/platform-targets.md#L340)已过时；[PublishTargetIds](../src/ide_zan/src/shell/ZanIDE.Workspace.zan#L441)列出 Linux、macOS、Android、OHOS，但列表不保证发布链路完整。
- [iOS 可免证书安装](../docs/platform-targets.md#L261)表述过强：linker ad-hoc 签名不等于普通 iPhone 安装、provisioning 或商店签名闭环；IPA 打包代码未提供这些步骤。
- [RELEASE 的待实现打包步骤](../docs/RELEASE.md#L175)部分过时：[package_bundle.sh](../scripts/package_bundle.sh#L101)已有 tar.gz/校验和，[release.yml](../.github/workflows/release.yml#L202)已有归档汇总，但 POSIX 图形 IDE 交付仍未完成。
- 提交 `b1836e7c` 的“全平台 GPU 加速”标题不能直接用作官网事实：[Metal 基础绘制](../src/runtime/gui_metal_backend.m#L67)仍委托 CPU，[WebGL](../src/runtime/gui_gl_context.c#L829)仅 Emscripten 分支实现，原始 WASI 分支仍为桩。

## 面向用户的发布仓库

官网对外入口统一为 [joyspinch/zanlang](https://github.com/joyspinch/zanlang)，用于发布 IDE / SDK 与相关包。公开仓库 API 确认默认分支 main；根目录含 ZanIDE.exe、toolchain、stdlib、文档与示例，README 指引通过仓库 ZIP 下载后启动 IDE。此次查询 Releases API 返回空数组，因此当前下载入口用仓库页面的 Code → Download ZIP，不使用空的 Releases 列表作为唯一下载路径。

[AI_README.md](https://github.com/joyspinch/zanlang/blob/main/AI_README.md)、llms.txt 和 docs/AI_ONBOARDING.md 已存在，官网 AI 指令使用发布仓库的入门说明。官网的 38 包能力清单来自本地包清单，不代表发布仓库已提供 38 个单独下载；此次检查未见根目录 packages。下载目录中的其他平台 runtime/sysroot 也不等于对应平台的 IDE 安装包。下文源码证据仅供维护者核对能力边界，用户获取流程无需阅读编译器源码或自行构建。

## SDK 发布打包缺项与重大限制

- Windows [publish_ide.ps1](../scripts/publish_ide.ps1#L201)复制 `ld.exe/ld.lld.exe`，未见 `ld64.lld/wasm-ld`；[目标目录清单](../scripts/publish_ide.ps1#L250)遗漏 `android-x64/android-arm64/apk-shell`。不能由源码能力推导发行包全目标零配置。
- POSIX [package_bundle.sh](../scripts/package_bundle.sh#L9)明确仅打 SDK、不含图形 IDE；[复制清单](../scripts/package_bundle.sh#L67)遗漏 Android/APK 壳、iOS、`packages` 和交叉链接器，并仍列旧 `riscv64` 名，需逐包复核交付闭包。
- 部分交叉目标缺少原生驱动时会生成桩并警告运行失败，例见 [main.c](../src/compiler/main.c#L5483)；链接成功不能作为所有标准库功能跨平台一致的证明。
- 非 Android x64 代码生成默认启用 AVX2/FMA/AES 等，见 [irgen_emit.c](../src/compiler/irgen_emit.c#L3032)；Win7 API 兼容不等于所有旧 CPU 或整套 SDK 均可运行。
- Android 无完整 SDK 的消费链路有实现，但签名需要 Java；Windows 可下载便携 JRE，其他宿主需安装 Java。默认 debug key 不等于生产签名，首次下载也不等于离线零依赖。
- Linux GUI 仍需显示服务；GPU 路径需要系统图形库。macOS 需要补真实应用验收及 Developer ID 签名/公证；iOS 需要补 GUI、安装与签名闭环。
- WASM 需 JS 宿主和字体资源；[rt_sync_wasm.c](../src/runtime/rt_sync_wasm.c#L11)含线程同步退化及网络/共享表失败桩。微信开发工具验证还受包体/CDN与事件桥限制，不能直接宣称正式手机小游戏交付。
- [交叉测试注册](../CMakeLists.txt#L2036)明确 WASM/Android 部分用例仅编译链接；维护官网状态时必须另附目标运行记录，不能把 CI 配置或预编译对象当新一轮验收。

维护原则：新增或升级平台状态时，同步核对目标表、链接分支、GUI、签名/封装、SDK 清单与运行环境；保存提交 ID 和具体环境，证据不足时保留“实验性/模拟器验证/待实机”限定。
