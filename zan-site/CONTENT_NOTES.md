# 官网事实与维护证据

核对日期：2026-11-11。此文件记录内容维护依据，不替代目标平台验收。平台“运行记录”指已有历史证据；本轮站点构建、HTTP、浏览器与文档示例验证不能外推成各平台重新验收。

## 来源层次

1. 当前实现与对应 conformance 用例用于核实实际语义。
2. `docs/SPEC.md`、`docs/ZANC_CLI.md`、`docs/STDLIB.md` 等用于定位，再核对是否存在更新后的实现。
3. 历史提交和验收文档只能证明其具体版本、目标和测试范围。
4. 站点 `docs/*.md` 明确记录当前可用说明，不能直接拼接存在冲突的旧参考文档。

各篇末尾“核实来源”保留本地路径，避免把发行仓库 `joyspinch/zanlang` 假定为编译器源码仓库。

## 已校正的重要旧说明

| 主题 | 当前依据与站点口径 |
| --- | --- |
| 集合赋值 | `docs/SPEC.md` 集合节与 `tests/conformance/cs_list_aliasing.zan`：共享引用存储，无写时复制保证 |
| 析构 | `src/compiler/binder/binder_decl.c` 拒绝用户析构；ARC 释放对象，资源使用显式清理或 using |
| 字符串 | `docs/SPEC.md`：不可变 UTF-8，Length/索引按字节 |
| 结构体元素 | `tests/conformance/list_struct_field_assign.zan`：区分局部值副本和容器槽字段直接写入 |
| 异步 | `docs/ASYNC_CPS_DESIGN.md` 与任务实现：直接 async、Task.Run、Task.Spawn 结果与等待契约不同 |
| worker 默认 | `src/runtime/io/rt_io_co_pool.c`：SetWorkers → ZAN_CO_WORKERS → min(逻辑核数,4)，最终限制 1..256 |
| 文件异步 | `stdlib/System/IO/File.zan` 的部分 Async 方法先 Yield 再同步读写；不承诺全部异步接口非阻塞 |
| LSP | `src/lsp/lsp_main.c` 当前声明增量同步、内联提示与语义标记，旧 TOOLING 部分描述已过时 |
| MCP 构建 | `tools/mcp_server/McpServer.zan` 当前 BuildProject 仅传入口；多文件项目需要完整构建输入 |
| GUI 标记 | `packages/Zan.Gui/src/Gui/Markup/Html.zan` 的 Gui.Html.Load；HTML/CSS 子集不等于浏览器 |
| 包拆并 | 当前 `packages/*/zan.pkg`；CSV/YAML/TOML 等归 Zan.Formats，不能使用旧独立包名 |
| 命名空间 | 按源码 namespace 声明采集；Models 目录不自动生成新的命名空间 |
| 许可 | 根 LICENSE 为 ZCSL v1 + Runtime & Output Exception；逐包元数据不等于工具链根许可 |

## 平台实现与历史证据

| 平台 | 当前实现路径 | 证据边界 |
| --- | --- | --- |
| Windows | `src/compiler/crosscomp.c`、Win32 驱动 | x64 主力开发；ARM64 交叉链接不等于硬件验收；Win7 分支不等于全 SDK 认证 |
| Linux | `src/compiler/driver/linker_cross.c`、`src/drivers/gui/gui_runtime_x11.c` | WSL 与 QEMU 记录按环境限定；GUI 需要显示服务与系统图形依赖 |
| macOS | `src/drivers/gui/gui_runtime_mac.m`、`linker_cross_apple.c` | Cocoa/NSWindow/WKWebView 是实际实现；`a3c50cafd` 有双架构链接记录，完整 GUI 设备验收仍需明确证据 |
| Android | `src/drivers/gui/gui_runtime_android_native.c`、`src/compiler/apk.c` | API35 x64 模拟器与 ABI 转译不能等同 ARM 手机；默认调试签名不等于生产签名 |
| iOS | `src/drivers/gui/gui_runtime_ios.c`、`gui_runtime_ios_font.c`、`linker_cross_apple.c` | UIKit 启动、窗口、触控、CoreText、安全区已实现；`1bead3f77` 有 GUI 交叉链接，无明确设备/系统版本验收 |
| OpenHarmony | `src/drivers/gui/gui_runtime_ohos.c`、`src/compiler/driver/linker_cross.c` | GUI 静态可执行文件被拒，需 --emit-lib 与宿主；`5c6288f3b` 是 QEMU CLI，`e59be75a0`/`218eda3f6` 是 DevEco 模拟器 GUI |
| WASM | `src/drivers/gui/gui_runtime_wasm.c`、`src/runtime/rt_sync_wasm.c` | 需要 zan_env 宿主；`892d26b96`/`e2e236ea9` 浏览器、`6ad6be0c9` 微信开发工具、`37325a885` Node WASI；线程/网络不能按桌面等价承诺 |
| RISC-V | `src/compiler/crosscomp.c`、`tests/run_riscv32_object.cmake` | Linux RISC-V64 与裸机 RV32 区分；RV32/esp32c3 只出对象，外部工具链负责启动与最终链接 |

`crosscomp.c` 有 17 个配置名称，含别名；可用列表会被所链接 LLVM 后端过滤。站点应引导查询 `zanc --list-targets`，不宣传“完整支持 17 个平台”。

OpenHarmony 的 `2b0b543d0` 只更新驱动构建与共享库链接，不能把旧模拟器记录说成当前版本重验。`scripts/package_mobile.ps1` 中 iOS/鸿蒙签名分支包含脚手架，不能从分支存在推断商店签名闭环。

## 生成与复核

正文维护在 `docs/*.md`。`scripts/generate-docs.mjs` 用同一内容生成页面、Markdown、搜索和 AI 全文；包页来自当前 manifest 和源码命名空间，平台页来自 `src/capabilities.js`。生成物只进入 `build/zan-site/`。

`npm --prefix zan-site run check` 执行站点范围的语法、生成一致性、链接（包括 AI 全文）、中文包检索、元数据和 HTTP 检查。它不运行编译器完整测试档，也不证明新写的 Zan 示例可运行；完整示例另用当前 `build/zanc.exe` 编译运行并记录结果。

本轮验证：站点 8 项检查通过；三个完整 `zan` 示例使用 `-g --strict-runtime` 编译运行，分别输出 `Hello, Zan!`、`16`、`42`，退出码为零且没有运行时错误报告。已验证带子路径的 SITE_URL 生成 canonical、robots 与 14 条 sitemap URL，再不设置 SITE_URL 重建会清除旧绝对元数据。浏览器确认首页到文档中心、Task.Spawn 正文搜索和章节跳转；390px 下无横向溢出，目录默认收起。文档初轮的截图工具曾超时，视觉复核在下面的品牌更新阶段补做。

## 官网品牌与排版复核

2026-11-11：检查 `assets/brand/VI.md`、官方 SVG 和横幅/主视觉后，网站统一采用 Z-Goose、白色字标、赞宝与大肥鹅主视觉。构建直接读取仓库品牌目录；首页、文档与交互演示使用 `src/theme.css` 的共享色板，不再另画站点 Logo。

品牌更新后的 `npm --prefix zan-site run check` 再次通过全部 8 项检查。已取得 1440px 桌面首页、桌面文档、390/391px 手机首页与文档的实际截图；手机无页面横向溢出，文档目录默认折叠，官方图片加载完整。文档搜索 `Task.Spawn` 返回 2 篇结果；演示切换到 CSV 场景正常。菜单展开/收起、演示场景带入项目选择和 `#project-builder` 跳转通过页面事件验证。自动化工具的部分真实点击超时，页面事件检查不作为完整指针端到端验收。未部署线上。

## 交互控件与首页内容补齐

2026-11-11：首页与文档最后加载共享 `src/controls.css`，统一下拉框及支持浏览器中的原生弹出面板、滚动条、焦点、禁用态、搜索空状态、折叠区和操作链接。浅色应用演示保持浅色控件与电光蓝焦点；不支持 `appearance: base-select` 的浏览器保留系统选择器。修正手机菜单横线宽度，以及窄屏导航按钮和菜单滚动范围。

首页新增控件画廊、图表、数据库三个真实示例入口和模板入口，以及兼容性、应用商用分发、原生依赖、平台边界、问题反馈五项 FAQ。示例目录已通过发行仓库 GitHub API 核对存在，本轮没有编译运行这些项目。60 个包补全中文用途摘要，卡片直达对应文档锚点；原始清单描述与许可仍在完整目录保留。新增栏目可通过首页搜索查找。

最终 `npm --prefix zan-site run check` 通过全部 8 项检查，覆盖 13 个文档页面、60 个包的动态链接及新增首页搜索锚点。站点范围 `git diff --check` 无空白错误，仅提示工作区换行格式转换。此次未修改 Zan 程序或编译器，未重复运行此前已验证的语言示例或 ctest。

浏览器已取得桌面示例区、FAQ 展开、文档搜索，以及 390px 手机控件、320px 菜单和示例区截图；320px 和 1440px 下页面无横向溢出。手机菜单和 FAQ 通过页面点击事件验证；项目选择切换到 CSV 后需求文案正确更新，文档搜索 `JSONPath` 返回 2 篇。修正后卡片标题实际 margin 为 0，未选中命令标签为共享蓝灰文字，包搜索无结果状态正确返回零项。自动化的部分真实点击超时，Enter 触发没有取得可靠结果，截图也有间歇性超时；这些检查不构成完整鼠标／键盘端到端验收，旧浏览器系统选择器与强制颜色模式未做实机验证。未部署线上。
