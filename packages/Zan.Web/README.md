# Zan.Web

服务端 Web 框架（纯 Zan，无原生依赖）。`using System.Web;` 按需拉入，
命名空间保留零破坏。

## 内容（System/Web 拆半：Designer 三件留守 stdlib，其余迁入）

| 文件 | 内容 |
|---|---|
| `WebApp.zan` | WebApp/WebHost/WebServer 应用骨架与状态文档 |
| `Router.zan` | 路由表与命中（Route/RouteHitDoc） |
| `Controller.zan` | 控制器基型 + HTTP 方法属性（HttpGet/Post/Put/Patch/Delete/NonAction）与 Filter/Hooks |
| `HttpContext.zan` | 请求上下文（Listing/ListQuery/RowList/StrMap/VNode） |
| `View.zan` | 视图渲染（View/ViewData） |
| `Validate.zan` | 校验器（Validator/ApiEnvelope/ApiError） |
| `StaticFiles.zan` | 静态文件挂载（StaticMount/ContentTypes） |
| `ApiDocs.zan` | API 文档生成（ApiParam/ApiParam 描述） |
| `Menu.zan` | 菜单构建（MenuBuilder/MenuNode） |
| `Security.zan` | Csrf/RateLimiter/PermBit/LockManager/LockLease/CustomAuthorization |
| `Attributes.zan` | 自定义特性（CustomAttribute/DescriptionAttribute 等） |
| `WebWs.zan` / `WsSession.zan` | WebSocket 与会话（Sessions） |

## 留守 stdlib（生成器子编译闭包，`--no-packages` 可达）

`DesignerHtml` / `Html` / `HtmlScope`——设计器 HTML 发射三件，已核验对迁出
13 文件零真实依赖。

## 消费者

- `Zan.Gui`（Designer.Html/DesignExport/Markup.Html 的设计器-服务端桥）
- ZanIDE（CodeNav/Workspace 的 API 文档/工作区服务）
- `Zan.Mvc`（ZanWeb 框架包，B-ID36 拆出）与 server-collab 模板同宇宙共存
- zan-mvc 监控/企业部署线（ServerMetrics 数据源在 `Zan.Diagnostics`）
