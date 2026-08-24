# 示例与模板

> 可运行示例在仓库 `examples/`，项目模板在 `templates/`（IDE 新建项目向导
> 直接使用）。本文列出全部模板与示例主题，方便按图索骥；模板源码本身也是
> 学习 GUI / 服务端的最佳起点。

## 项目模板（IDE 新建向导）

| 模板 | 分类 | 说明 |
|---|---|---|
| `console` | Console | 最小 Hello World 控制台 |
| `console-args` | Console | 命令行参数（`Environment.ArgCount/ArgAt`） |
| `console-multi` | Console | 多文件工程组织 |
| `gui-empty` | Window App | 最小 `.zform` 窗口（Label + Input + Button + 事件）——**GUI 起点** |
| `gui-free` | Window App | 自由画布（layoutMode 1） |
| `gui-tabform` | Window App | 标签页 + 工具条 + 状态栏（多窗口/多页编排） |
| `gui-sidebar` | Window App | 侧边栏布局 |
| `gui-ribbon` | Window App | Ribbon 功能区 |
| `gui-toolbar` | Window App | 工具条 |
| `gui-components` | Window App | Gui 组件面板/工具栏展示 |
| `gui-webview` | Window App | 内嵌 WebView（网页视图） |
| `gui-watch` | Window App | 圆形表盘窗口（异形窗口示例） |
| `gui-dashboard` | HMI/SCADA | 仪表板卡片网格仪表 |
| `gui-hmi` | HMI/SCADA | 工控仪表（Gauge 等） |
| `server-mvc` | Server | 注解式 Web：`WebApp` + `[HttpPost]` 路由 + `Controller` + `HttpContext` + 模板/静态文件（**服务端首选**） |
| `server-http` | Server | 轻量 HTTP：`HttpServer` + `OnRequest` |
| `server-iot` | Server | MQTT 物联网 |
| `server-ws` | Server | WebSocket 网关 |
| `server-tcp` | Server | TCP 服务 |
| `dll-export` | Library | DLL 导出函数 |
| `dll-class` | Library | DLL 导出类 |

## 示例程序（examples/）

| 目录 | 主题 | 一句话 |
|---|---|---|
| `audio` | 音频 | 播放/音效（SDL3） |
| `db` | 数据库 | SQLite / 数据访问用法 |
| `game` | 游戏 | 游戏框架示例（Pong/贪吃蛇/打砖块等经典） |
| `gamepad` | 输入 | 手柄输入 |
| `gpu` | GPU | GPU 加速渲染示例 |
| `gui_browser` | GUI | 多标签网页浏览器（多个 `WebViewBox`） |
| `gui_cef_browser` | GUI | CEF Chromium 浏览器 |
| `gui_charts` | GUI | 图表（ChartView：柱状/折线/饼图等） |
| `gui_gallery` | GUI | **组件大观园**：60+ 控件、主题/皮肤、即时与保留式混编 |
| `input` | 输入 | 键盘/鼠标/热键 |
| `lua` | 脚本 | 嵌入 Lua |
| `net` | 网络 | HTTP/TCP/WS 用法 |
| `python` | 脚本 | 嵌入 CPython |
| `sdk` | SDK | 平台 SDK（京东/微信）调用 |
| `touch` | 输入 | 触控 |

## 最小 GUI 项目（从模板到代码）

新建 `gui-empty`，两个文件就是全部：

```json
// src/App.zform
{
  "name": "App", "winW": 480, "winH": 520,
  "fields": [
    { "kind": "Label",  "label": "Welcome", "name": "TitleLabel" },
    { "kind": "Input",  "label": "Name",    "name": "NameInput" },
    { "kind": "Button", "label": "OK",      "name": "OkButton" }
  ]
}
```

```zan
// src/App.zan — 你的代码（设计器不会覆盖）
using System;
using Gui;
using Gui.Widget;

partial class App {
    static void OnLoad(Form form) {
        OkButton.OnClick(() => { TitleLabel.Text = "Hello, " + NameInput.Text + "!"; });
    }
}
```

按 `F5` 运行。设计器/代码切换、事件绑定细节见「GUI 指南」。

## 最小服务端项目（server-mvc）

```zan
using System;
using System.Web;

[Route("/")]                                    // 类级前缀
class Api {

    [HttpPost("/api/echo")]
    static string Echo(HttpContext ctx) {
        return "you said: " + ctx.InText("msg", "");
    }

    [HttpGet("/")]
    static void Index(HttpContext ctx) {
        ctx.Reply(HttpResponse.Html("<h1>Hello</h1>"));
    }
}
```

配合 `WebApp`（`WebApp app = new WebApp("0.0.0.0", 8080);` + `app.Register<Api>();
await app.ServeAsync();`）即可。属性路由、`HttpContext`、参数校验、限流、
会话的完整说明见 [System.Web](/ref/System.Web)。

## 相关文档

- 语言全能力：[语言参考](/lang)
- GUI 开发：[GUI 指南](/gui)
- 标准库 API：[标准库](/stdlib)（含逐命名空间参考页 `/ref/...`）
- IDE 使用：[IDE 指南](/wiki)
