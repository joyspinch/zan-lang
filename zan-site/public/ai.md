# AI 开发助手入口（文档地图）

> 本文是给 AI（也给人）的 Zan 文档检索入口：说明整套文档的结构、按任务
> 类型推荐抓取哪些页面，以及哪些内容以源码为准。**先抓本文，再按需抓取。**
>
> 所有文档同时提供 HTML（给人浏览）与 Markdown（给 AI，无标签噪音，推荐抓
> 取 `.md` 版本）。
>
> 想把 AI 编码工具接到本机 SDK（`AGENTS.md` + skills + MCP 配置，一条命令、
> 可离线）？看 **[`/ai-connect`](/ai-connect)**——那是 `tools\ai_pack\` 的网站镜像。

## 文档结构

| 页面 | 内容 | 适合 |
|---|---|---|
| [`/lang.md`](/lang.md) | **语言参考**：全部语法/类型/语句/泛型/异步/ARC/FFI/工具链，含「与 C# 的差异」与「未实现特性」清单 | 写任何 Zan 代码前必读 |
| [`/gui.md`](/gui.md) | **GUI 开发指南**：App/Form/Control 架构、`.zform` JSON 设计文档完整 schema、代码式组件操作、布局、样式/主题/皮肤、事件、控件、Canvas、WebView/CEF/DataGrid | 做界面/窗口应用 |
| [`/stdlib.md`](/stdlib.md) | **标准库总览**：全部命名空间地图、语言内建类型成员速查、常用写法（文件/JSON/HTTP/数据库/加密/线程） | 找 API 归属 |
| [`/ref/index.json`](/ref/index.json) | **机器可读索引**：命名空间 → 类型清单 + 源码文件列表 | 程序化检索 |
| [`/ref/<ns>.md`](/ref/System.IO.md) | **逐命名空间 API 参考**：类型 + 成员完整签名 + `///` 文档注释（从 stdlib 源码提取） | 查具体类/方法 |
| [`/wiki.md`](/wiki.md) | IDE 指南：下载、项目模板向导、设计器、运行/调试、发布、内置 AI/MCP | 给人类 IDE 使用 |
| [`/examples.md`](/examples.md) | 模板与示例目录清单 | 找可运行样例 |
| [`/cookbook.md`](/cookbook.md) | **代码示例库**：42 个经 zanc 真实编译验证的
最小可运行模式（基础/文件/JSON/HTTP/数据库/ORM/加密/线程/GUI 全部控件模式）——写任何功能先看这里抄 |
| [`/tooling.md`](/tooling.md) | **工具协议**：架构定位 → RepoMap 语义索引 → zanc 编译器/测试分层 → MCP 调用；「怎么定位、怎么用工具」的答案 | 开始动手前必读 |

标准库参考页清单（示例）：`/ref/System.IO.md`、`/ref/System.Net.md`、
`/ref/System.Data.md`、`/ref/System.Json.md`、`/ref/System.Linq.md`、
`/ref/System.Security.Cryptography.md`、`/ref/System.Threading.md`、
`/ref/System.Text.md`、`/ref/System.Web.md`、`/ref/Gui.md`、
`/ref/Gui.Widget.md`、`/ref/Gui.Component.md`、`/ref/Gui.Hmi.md`、
`/ref/Game.Arpg.md`、`/ref/SDL3.md`。

## 按任务类型推荐抓取

### 我要写一个 GUI 窗口/组件

1. `/gui.md` — 架构、`.zform`、代码式操作、布局样式事件（大部分答案在此）；
2. 按需 `/ref/Gui.md`（App/Control/Form/Canvas）、`/ref/Gui.Widget.md`（控件）、
   `/ref/Gui.Component.md`（DataGrid/WebView/CodeEditor/Chart 等）、
   `/ref/Gui.Hmi.md`；
3. 用 `.zform` 时注意：`kind` 写**类名**（`SelectBox`/`WebViewBox`/`CefBrowserBox`/
   `DataGrid`），泛型控件用 `of`；事件绑定 `onClick: "Handler名"` + 代码后置
   `static void OnLoad(Form form)`。

### 我要写服务端（HTTP/MQTT/WS/TCP）

1. `/ref/System.Web.md` — server-mvc：`WebApp`/`Route`/`Controller`/
   `[HttpGet]`/`HttpContext`；
2. 或 `/ref/System.Net.md` — 底层 `HttpServer`/`TcpListener`/`WebSocketServer`/
   `MqttClient`/`Worker`；
3. `/ref/System.Data.md` — 数据库/Redis/ORM；`/examples.md` 里 server-mvc 模板是最小参考。

### 我要写纯语言代码（非 GUI）

`/lang.md` 几乎全覆盖；集合/LINQ/字符串查 `/stdlib.md` 速查表 +
`/ref/System.Linq.md`；异步/线程查 `/lang.md` 的「异步」「并发与线程」+ 
`/ref/System.Threading.md`。

### 我要编译/发布/交叉编译项目

`/lang.md` 的「工具链」一节：`zanc` 选项、`--publish`、`--target`、
`--link-mode`、`zanfmt/zandoc/zan-lsp/zan-dap`、包管理。

## 准确性约定

- **参考页由代码生成**：`/ref/*.md` 内容直接来自 `stdlib/**/*.zan` 源码
  （类型/成员/`///` 注释），与仓库 **0.2.3** 版本对应。代码改动后运行
  `zan-site/gen/api_extract.py` + `gen/site_build.py` 即刷新。
- 与仓库内 `docs/SPEC.md` 冲突时，**以 conformance 用例与 stdlib 源码为准**
  （SPEC.md 部分章节过时：它声称 `?.`、record 未实现，实际已实现；`union`
  实际没有）。
- Cloudflare 会自动把无扩展名路径映射到同名 `.html` 资产（`/wiki` → `wiki.html`），
  并把 `.html` 请求 307 剥掉扩展名——文档链接统一用**无扩展名**形式；
  `.md` 链接保留 `.md` 后缀（`/lang.md`、`/ref/System.IO.md`）。
- 需要验证某个 API 是否存在时，最快的方式是查对应 `/ref/<ns>.md` 并 grep。

## 例：一个真实开发请求的处理流程

1. 抓 `/tooling.md`——先建立工具/定位心智（3 次调用内解决问题，杜绝通读）；
2. 抓 `/lang.md` 了解语法与限制；
3. 抓 `/gui.md` 确定用 `.zform` 还是代码建控件；
4. 抓 `/ref/Gui.Widget.md` 找 `Button`/`Tabs` 的字段与事件名；
5. 抓 `/ref/System.IO.md` 找文件读写 API；
6. 按 `/wiki.md`/`/examples.md` 组织项目文件结构（`zan.proj` + `src/`）；
7. 生成代码后交给 IDE 编译，用编译器诊断修正。

### MVC 项目：改一个接口的完整定位链（零通读）

```text
需求：给 /api/gather 加登录校验
 1. routes.json（RepoMap 从 Controller 属性提取）→ {method:POST, path:"/api/gather", action:"Api.Gather"}
 2. 读 src/Controller/Api/Gather.zan 的 Gather 方法（±40 行）
 3. 校验逻辑在 Framework/Auth / src/Feature；数据在 Dao/…；实体在 Model/…
    —— 全由目录约定直接到达，不需要读无关代码
 4. 改 → zanc 编译（诊断定位）→ scripts/test.ps1 smoke（回归）
```
