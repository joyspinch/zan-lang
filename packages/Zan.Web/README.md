# Zan.Web

服务端 Web 框架（纯 Zan，无原生依赖）。`using System.Web;` 按需拉入，
命名空间保留零破坏。

## 内容（服务端 Web 框架核心分层规划）

| 模块目录 | 内容 |
|---|---|
| `Http/` | WebApp / WebHost / WebServer 应用骨架与生命周期、HttpContext 请求上下文、HttpFormParser 表单/多部件解析器、HttpUploadStreamer 大文件流式落盘器、HttpSseStreamer SSE 流式传输器、Hooks 与 Filter |
| `Routing/` | Router / Route 路由表与匹配引擎、RouteTable 跨进程共享路由快照 |
| `Attributes/` | 统一特性标注（Attribute 基类、HttpGet/Post/Put/Patch/Delete、Route/RouteR、NonAction、ReadOnly 等） |
| `Controllers/` | Controller 控制器基型与 View 页面/片段视图渲染引擎 |
| `Security/` | AuthDelegates (AuthFn/PermissionFn)、Csrf 防护、RateLimiter 限流器、LockManager 排他锁、Sessions 会话、Validator 校验器 |
| `Seo/` | GeoAgent AI 爬虫识别、GeoMeta、GeoSchema、GeoSource、LlmsTxt (llms.txt)、RobotsTxt、Sitemap (sitemap.xml)、SeoRouter 协议路由与分发器 |
| `Static/` | StaticFiles 静态资源挂载与流式分发中间件 |
| `WebSocket/` | WebWs 握手升级装配与 WsSession 长连接会话 |
| `Navigation/` | MenuBuilder / MenuNode 动态导航树构建、ApiDocs 接口文档生成 |
| `Models/` | 领域传输与状态模型（ApiEnvelope、ApiError、Listing、LockLease、RouteStatEntry 等） |

## 标记层归位（Gui.Markup）

原 `DesignerHtml` / `HtmlParser` / `HtmlScope` 等设计器与 HTML 标记解析三件套已彻底归位至 `Zan.Gui`（`Gui.Markup` 命名空间），标准库 `stdlib/System/Web` 已彻底废除。

## 消费者

- `Zan.Gui`（Designer.Html/DesignExport/Markup.Html 的设计器-服务端桥）
- ZanIDE（CodeNav/Workspace 的 API 文档/工作区服务）
- `Zan.Mvc`（ZanWeb 框架包）与 server-collab 模板同宇宙共存
- zan-mvc 监控/企业部署线（ServerMetrics 数据源在 `Zan.Diagnostics`）
