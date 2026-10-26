# ff_bidi_demo —— WebDriver BiDi 协议功能演示

控制台程序（无 GUI 依赖），演示 `Zan.Gui.Browser` 里纯 Zan 实现的
WebDriver BiDi 客户端（`FfBidi` + `FfWs`）驱动真 Firefox 的完整能力：

| 步骤 | BiDi 命令 | 说明 |
|------|-----------|------|
| 1 | `session.new` | 建 BiDi 会话（WebSocket 连 `ws://127.0.0.1:<port>/session`） |
| 2 | `session.subscribe` | 订阅 `browsingContext.domContentLoaded/load`、`log.entryAdded`、`script.message` 事件 |
| 3 | `browsingContext.getTree` | 枚举顶层浏览上下文 |
| 4 | `browsingContext.navigate` | 导航（`wait: "complete"` 等加载完成） |
| 5 | `script.evaluate` | 页内求值（`document.title` / `location.href`） |
| 6 | 事件流 | 收 2 秒进站事件并打印 |
| 7 | `browsingContext.captureScreenshot` | 抓帧，base64 解码落盘 PNG |
| 8 | `browser.close` | 协议级优雅关停；3 秒未退则家族兜底强杀 |

结尾按断言打分（title/url/截图字节数/事件数），打印 PASS/FAIL。

## 运行

Windows（本仓库构建）：

```bash
build/zanc examples/ff_bidi_demo/FfBidiDemo.zan --auto-stdlib \
    --package-project zan-lang -o _scratch/ff_bidi_demo.exe
_scratch/ff_bidi_demo.exe        # 需本机装有 Firefox，或先设 ZAN_FF_EXE
```

macOS（Apple Silicon，交叉编译产物，已在本仓库 Windows 机器上验证过
同源代码）：

```bash
chmod +x ff_bidi_demo-macos-arm64
./ff_bidi_demo-macos-arm64
```

Firefox 来源与 `gui_firefox_browser` 相同：`ZAN_FF_EXE` 环境变量 →
标准安装位置（macOS 是 `/Applications/Firefox.app`）。**自建/裁剪版
Firefox 可能无法建立自动化会话，请用官方构建。**

## 生命周期约定（防 "already running" 弹窗）

- **每次运行独立 profile**（`<缓存根>/bidi-<端口>`）：上一轮的孤儿
  进程不会锁住下一轮。
- 收尾先 `browser.close` 协议优雅关停（Firefox 正常退出、写 profile），
  3 秒后仍有家族进程存活才按**进程家族**强杀（沿 parentPid 找后代，
  绝不波及用户自己打开的 Firefox）。

## 实现注记（踩过的坑）

- WS 客户端帧必须掩码，掩码后载荷可能出现 NUL——字符串的 C 串语义
  会在 NUL 处截断，帧在原生缓冲区构建后走二进制安全的
  `Socket.ReactorSendPtr`。
- `session.new` 的 `capabilities` 必须是对象；这版 Firefox 的
  `script.evaluate` 要求显式 `awaitPromise` 布尔。
- BiDi 的 `script.evaluate` expression 是脚本语句（完成值即结果），
  不写 `return`。
