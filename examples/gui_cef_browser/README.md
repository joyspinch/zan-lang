# gui_cef_browser — 内嵌 CEF 浏览器

多标签浏览器示例：地址栏、前进/后退/刷新/停止、标题与加载状态同步，浏览视图是
真正的 Chromium（CEF）原生子窗口，作为控件放在 Zan GUI 的控件树里。

```bash
build/zanc examples/gui_cef_browser/gui_cef_browser.zan --auto-stdlib -o build/cefbrowser
build/cefbrowser [起始地址]
```

首次运行会下载官方 CEF 包（几百 MB）并解包到缓存目录，之后启动只做校验。
同一个可执行文件同时充当 Chromium 的 helper 子进程（`CefBootstrap.RunHelper()`
必须是 `Main` 的第一句），所以不要把它改名后单独分发 helper。

macOS 例外：Chromium 要求子进程是一个独立的 `.app`，否则每个子进程都会按主包的
`Info.plist` 起来（Dock 里出现多个图标）。CEF 驱动目录里随驱动一起分发了一个只做
`cef_execute_process` 的小 helper（`zan_cef_helper`，几十 KB，`dlopen` 同目录的
`libzan_cef.dylib`），IDE 发布 macOS 时会把它装进
`app.app/Contents/Frameworks/<名> Helper.app/Contents/MacOS/<名> Helper`
并写 `LSUIElement=true`。运行时目录、驱动路径和 `switches` 由主进程通过
`ZAN_CEF_HELPER_RUNTIME`/`ZAN_CEF_HELPER_DRIVER`/`ZAN_CEF_HELPER_SWITCHES`
传给 helper，所以 helper 不需要包含应用本体。

## 路径与开关都能自定义

代码里配置（优先级最高），适合把运行时随应用一起分发、完全离线启动：

```zan
CefOptions o = new CefOptions();
o.runtimeDir = "D:/app/cef";              // 自备运行时，跳过下载
o.cacheRoot  = "D:/app/cache";            // 下载/解包缓存根
o.profileDir = "D:/app/data/web";         // Cookie、localStorage、HTTP 缓存
o.driverPath = "D:/app/bin";              // zan_cef 驱动所在目录或文件
o.switches   = "disable-gpu,disable-gpu-compositing";
o.locale     = "zh-CN";
o.helperPath = "";                        // 空 = 包里的 helper，没有就用当前可执行文件
if (!await CefBootstrap.StartAsyncWith(o)) { ... }
```

同名环境变量在没有代码配置时生效（运维/排障用）：`ZAN_CEF_RUNTIME`、
`ZAN_CEF_CACHE`、`ZAN_CEF_PROFILE`、`ZAN_CEF_DRIVER`、`ZAN_CEF_SWITCHES`、
`ZAN_CEF_LOCALE`、`ZAN_CEF_HELPER`。`ZAN_CEF_SWITCHES` 与代码里的 `switches`
是叠加关系（环境变量在前），其余字段是代码配置覆盖环境变量。

开关对**每个** Chromium 进程都生效：Chromium 只把自己认识的开关转发给子进程，所以
helper 分支（`CefBootstrap.RunHelper`）会把同一份 `switches` 再套一遍。因此
`CefOptions.Use` 要在 `RunHelper` 之前调，helper 才拿得到代码里配的开关。

开关串里出现分号时按分号分项，用来传本身带逗号的开关值：
`o.switches = "disable-features=A,B;use-angle=d3d11"`。`*-features` 这类列表开关是
**追加**到 CEF/Chromium 自己那份值里，不会把默认值顶掉。

### GPU 崩溃 / WebGL 建不起来

装了虚拟显示适配器的机器（向日葵 `OrayIddDriver`、`GameViewer 虚拟显示适配器`、
部分远程桌面）上，DirectComposition 交换链会让 GPU 子进程在建 GL 共享上下文时崩
（带 `ZAN_CEF_SWITCHES=log-severity=verbose;enable-logging=stderr;v=1` 看得到：GPU
进程在 `VizNullHypothesis is disabled` 之后就没了下文，宿主侧
`GPU process exited unexpectedly: exit_code=-2147483645` 连三条，第四个 GPU 进程报
`Failed to create shared context for virtualization`），Chromium 退到
`--use-gl=disabled`，此后页面里 `canvas.getContext('webgl')` 一律返回 null。用

```zan
o.disableDirectComposition = true;   // 或 ZAN_CEF_NO_DCOMP=1
```

关掉 DComp 即可：本机实测 GPU 0 崩溃，硬件 GL 仍走真实显卡
（`ANGLE (NVIDIA, NVIDIA GeForce RTX 4060 Laptop GPU ... D3D11)`）。比
`disable-gpu` 好的地方是不牺牲硬件加速。

`ZAN_CEF_LOG=1` 打开诊断：打印每个 Chromium 进程实际拿到的命令行（用来确认某个
开关到底有没有传进去），以及宿主消息泵的停顿（>300 ms 的间隔会被报出来）。

stderr 里的「Timeout of new browser info response」是 CEF 渲染进程侧的告警，
本机实测与宿主消息泵无关（同一次运行里没有一条 >300 ms 的泵间隔），也与 GPU
是否崩溃无关（关掉 DComp、GPU 0 崩溃时照样出现），条数固定、不影响导航与页面
功能，暂时按噪声对待。

## 页面能力（走 CDP）

原始通道是 `web.SendCdp(method, paramsJson)` + `web.TakeCdpMessage()`；上层封装
按事件名分派，拿过 `web.Cdp()` 之后不要再自己 `TakeCdpMessage()`（会互相抢消息）：

```zan
CefCdp cdp = web.Cdp();
cdp.On("Page.loadEventFired", (json) => { ... });   // 按事件名订阅
cdp.Send("Page.captureScreenshot", "{}", (result) => { ... });  // 回复回调

CefCookies ck = web.Cookies();
ck.GetAll((json) => { ... });                 // {"cookies":[...]}
ck.Set("token", "abc", ".example.com", "/", true);
ck.Delete("token", ".example.com");

CefPage page = web.Page();
page.AllowDownloads("D:/app/downloads", (json) => { ... });  // 默认是拒绝下载
page.AutoDismissDialogs(true);                // alert/confirm 一律确定
page.OnConsole((json) => { ... });
page.OnNetwork((json) => { ... });
page.SetUserAgent("...");
```

## 弹窗（window.open / target=_blank）

默认由 CEF 自己开一个独立的浏览器窗口——对标签式外壳来说这不是想要的行为，
所以有三档：

```zan
web.OnPopup((url) => { shell.NewTab(url); });  // 交给宿主：自己开标签
web.BlockPopups();                              // window.open 返回 null
web.AllowPopupWindows();                        // 恢复默认（CEF 自己开窗）
```

回调在 `Render` 里（UI 线程）触发，因此可以直接建控件、改标签页。

## 浏览器指纹

`web.Fingerprint()` 把 UA/Client Hints、语言、时区、屏幕与 DPR、WebGL 型号串、
canvas 噪声收成一次调用。前几项走 `Emulation` 域的 override（页面里读到的
`navigator.userAgent`、`Intl` 时区、`screen.width` 和请求头一起变），浏览器不给
覆盖的几项（`navigator.languages`、`deviceMemory`、`webdriver`、WebGL 型号串、
canvas 像素）靠 `Page.addScriptToEvaluateOnNewDocument` 在页面脚本之前打补丁：

```zan
CefFingerprint fp = web.Fingerprint();
fp.UseWindowsChrome(CefHost.ChromeVersion());   // UA + 品牌 + 平台一套预设
fp.locale = "zh-CN";
fp.acceptLanguage = "zh-CN,zh;q=0.9";
fp.timezone = "Asia/Shanghai";
fp.screenWidth = 1920; fp.screenHeight = 1080; fp.scalePercent = 100;
fp.hardwareConcurrency = 8; fp.deviceMemory = 8;
fp.webglVendor = "Google Inc. (NVIDIA)";
fp.webglRenderer = "ANGLE (NVIDIA, NVIDIA GeForce RTX 3060 Direct3D11 vs_5_0)";
fp.canvasNoise = 2;          // 0 = 不加噪声；加了画布内容会被改动一点
fp.hideWebdriver = true;
fp.Apply();
web.Reload();                // 注入脚本对「下一个」文档生效
```

空字段就是不动那一项，`fp.Reset()` 撤掉全部覆盖与注入脚本。改不了的：TLS/JA3
指纹（Chromium 内置网络栈）、真实 GPU 型号（要换 `--use-angle=`/`--use-gl=`
开关）、系统字体列表（要么注入补丁，要么换 profile 的字体目录）；代理与 WebRTC
本地 IP 走开关 `--proxy-server=`、
`--force-webrtc-ip-handling-policy=disable_non_proxied_udp`。

事件消息由 `CefBrowser.Render` 每帧分派（拿过 `Cdp()` 的浏览器才会），GUI 宿主
不用自己调 `Pump()`。

尚未包成 Zan API 的原生 handler：弹窗策略（`on_before_popup`，`target=_blank`
目前由 Chromium 自己开原生窗口）、右键菜单定制、打印/查找 UI。这些要动
`stdlib/Gui/Component/CefBrowser/native/zan_cef.c`，且 CEF 151 与 109 两个变体的
回调签名不同，见 `TASKS.md`。

## 镜像与完全离线安装

默认情况下不再依赖约 10 MB 的官方全量 `index.json`：程序会先从内置的
CEF stable + minimal 版本表选择归档。CEF 版本和原生 `zan_cef` driver 是绑定的，
不能只替换其中一个；升级时必须同时更新内置版本表、`CurrentBranch()` /
`LegacyBranch()` 前缀和 driver。

如果需要把下载切到镜像，可设置 `ZAN_CEF_MIRROR`。镜像上按官方原文件名放置归档，
并在同一个基址下提供 `/index.json`，这样新平台或未登记分支仍可走索引兜底：

```bash
export ZAN_CEF_MIRROR=https://mirror.example.com/cef
# 或本机镜像：
export ZAN_CEF_MIRROR=http://127.0.0.1:8099/cef
build/cefbrowser
```

归档 URL 会使用 `cef_binary_<版本>_<平台>_minimal.tar.bz2` 的官方文件名，
文件名中的 `+` 会按 URL 规则转成 `%2B`。镜像可以使用 `http` 明文，也可以使用
`https`；基址末尾是否带 `/` 都可以。

没有 VPN 或目标机器完全不能联网时，可以在另一台机器手工下载归档后拷贝过来，
设置 `ZAN_CEF_ARCHIVE` 指向本机归档的绝对路径。此模式不查索引、不联网，也不会
给下载任务添加条目：

```bash
export ZAN_CEF_ARCHIVE=/Users/me/Downloads/cef_binary_151.3.23+gd211df0+chromium-151.0.7922.170_macosarm64_minimal.tar.bz2
build/cefbrowser
```

本机归档的文件名必须包含当前平台，版本前缀必须与原生 driver 绑定的 CEF 分支
一致。内置表中有该完整版本时还会校验 SHA-1；归档必须平台、版本前缀和 SHA-1
都正确，否则会拒绝安装。内置表没有的同分支补丁版本可以使用，但会跳过 SHA-1
校验；版本前缀检查仍然会执行。
