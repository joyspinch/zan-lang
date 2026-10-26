# Zan Firefox Browser（gui_firefox_browser）

多标签浏览器示例：每个标签一个 **FfBrowserBox**——拉起一个独立的
Firefox 子进程（专属 profile、`-no-remote`），把它的原生窗口寄宿进
Zan 窗口的控件矩形，导航/标题/JS 走 Firefox 内置的 Marionette 协议。
与 `examples/gui_cef_browser`（进程内 CEF）完全同构，引擎换成外挂的
真 Firefox。

## 运行

```bash
build/zanc examples/gui_firefox_browser/FfBrowserDemo.zan --auto-stdlib \
    --package-project zan-lang -o build/ffbrowser
build/ffbrowser [起始地址]
```

## 平台支持

| 平台 | 寄宿方式 | 说明 |
|------|----------|------|
| Windows | `SetParent` 真子窗口 | 完整嵌入，弹层由 NativeLayer 裁剪 |
| Linux (X11/XWayland) | `XReparentWindow` 真子窗口 | 子进程强制 `GDK_BACKEND=x11` |
| macOS | AX 浮窗锁定（实验性） | 见下节 |

## Firefox 从哪来

按顺序自动发现：`ZAN_FF_EXE` 环境变量 → 注册表（`PathToExe` /
App Paths）→ 标准安装目录。都找不到时控件画占位符说明原因。

注意：**部分自建/裁剪版 Firefox 的浏览器 UI 层不完整，Marionette
会话建立会挂起**（`WebDriver:NewSession` 无回复）。遇到这种情况请
换官方构建，并用 `ZAN_FF_EXE` 指定路径。

## 环境变量（对应 `FfOptions` 字段）

| 变量 | 作用 |
|------|------|
| `ZAN_FF_EXE` | Firefox 可执行文件完整路径（发现失败时兜底） |
| `ZAN_FF_PORT` | Marionette 端口（默认每实例自动挑空闲端口） |
| `ZAN_FF_CHROME` | `1` = 保留 Firefox 自带工具栏（默认隐藏，Ctrl+L/T 仍可用） |
| `ZAN_FF_ARGS` | 追加的 Firefox 命令行参数（如 `-headless`） |
| `ZAN_FF_PROFILE_ROOT` | 专属 profile 的根目录（默认在平台缓存根） |

代码级配置：`FfOptions.Use(...)`，字段优先于环境变量。

## macOS 说明（实验性）

AppKit 不允许把别的进程的窗口装进自己的视图树，所以 macOS 用辅助功能
（Accessibility）API 把 Firefox 窗口悬浮锁定在控件区域上方、随宿主
窗口移动。首次使用需要授权：

**系统设置 → 隐私与安全性 → 辅助功能** 里勾选宿主程序，授权后重启
程序生效。已知差距：Firefox 的标题栏（红绿灯）是系统画的，去不掉；
拖动宿主窗口时浮窗有轻微跟随延迟。要真正的画布内嵌入，可等待
headless 像素合成后端（抓帧 → 解码 → 画布绘制，约 10-15 fps）。

## 已知边界

- `CanGoForward` 恒为 false、`LastStatus` 恒为 0（Marionette 无廉价查询）；
  `CanGoBack` 按 `history.length > 1` 估算（2 秒刷新）。
- 每个标签 = 一个独立 Firefox 进程 + 独立 profile（多开互不干扰，
  关标签即退进程）；页面内 `window.open` 开出的是 Firefox 自己的
  独立窗口，不受宿主管理。
- 首次冷启动需要几秒（进程 + 窗口 + 会话），期间控件画占位符。
