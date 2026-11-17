# HTML 窗口——混排与 WebView

> 从 html-window.md 拆出：文档与原生控件混排五坑、内嵌真网页三道坎。

## 文档与原生控件混排：五个画不对的坑（都真踩过）

- **文档里的选择器只命中文档元素**：运行期装载的文档会给自己的元素
  混入作用域类（`zs-load-<序>`），代码后挂进去的控件（徽标、原生
  网页视图、工具条）**不带**这个类——写在文档 `<style>` 里的
  `.wxrailbadge` 这类规则永远匹配不到它们，控件会退回普通流内布局
  （把父容器挤歪）。这类控件的外观规则只能放**皮肤层**（全局）。
- **原生层是兄弟层，永远盖在保留树上**：网页视图（WebViewBox）这类
  原生控件不是画在画布上的，它是一枚独立窗口。工具条与原生视图
  不能都当"填充项"（两个 flex-grow:1 的项会整块重叠，原生视图把工具
  条盖住，布局自检会报 overlap）。原生视图的宿主必须拿到一块**明确
  的矩形**：工具条固定高（44px）+ 原生视图 grow 吃掉余下。
- **图标大小不是字号**：按钮/标签的字形边长 = 字号 × 4/3
  （`BoxContent.GlyphSize`）。想要 20px 的图标得给 15px 字号；给
  20px 字号就是 27px 的巨图标（占掉半条导航栏）。
- **单行标题类文本要 `white-space: nowrap`**：引擎量块流文本时可能
  拿不到容器宽（先量在 0 宽上），一折行就把固定高度的标题行撑爆、
  文字溢到下面的分隔线。标题/副标题/表头这类"一行的东西"直接
  nowrap；正文才让它折。
- **同一个类不要文档与皮肤各写一半**：`.wxhead` 一半在文档（只写
  flex）、一半在皮肤（高度内距），两边合起来的最终值谁也说不清
  （交叉轴对齐失效、标题块量到 0 宽）。共享给"代码建的控制"的类放
  皮肤，页面独占的类放文档，**一份类只出现在一处**。

## 内嵌真网页（WebView）：三道坎（都真踩过，症状全是"静默失效"）

`.html` 设计稿是**引擎自己的控件树**，不是网页；只有两处用真浏览器引擎：
服务端渲染好的正文（`LoadHtml`）与后端管理台（`Navigate` 到服务端页面）。
这条路上的坑没有一条会自己报错——界面正常、零日志、功能没生效。

- **WebView2 的 COM 入口有 UI 线程亲和**：跨线程调用回
  `UI_E_WRONG_THREAD`（0x802A000C）。Zan 的 `async` 体由后台协程池驱动，
  不在 GUI 线程上——在协程里发 `NavigateToString`，正文永远装不上，页面
  停在 `about:blank` 且不报任何错。解法是包里的 `WebView2.IssueOnUi`
  （Dispatcher 投回 UI 线程），`Navigate`/`LoadHtml`/`ExecuteScript`
  都要走它。坑出处：文档页正文空白只能降级纯文本；内嵌后台网页永远要
  再登录一次。
- **vtable 槽位按 SDK 头文件数，别凭印象**：`ICoreWebView2` 本体 58 个
  方法（槽 3..60，0..2 是 IUnknown），`ICoreWebView2_2` 的自有方法从 61
  起——`add_WebResourceResponseReceived`=61、`remove_`=62、
  `get_CookieManager`=66。槽位写错就是读到 vtable 之外的内存，HRESULT
  报 `E_INVALIDARG`（0x80070057），症状是"某个功能从来没生效过"。
  核对来源：NuGet `microsoft.web.webview2` 的
  `build/native/include/WebView2.h`（方法按声明顺序就是槽位顺序）。
- **客户端已登录 ≠ 网页已登录**：API 走 `Authorization: Bearer <令牌>`，
  后台网页认的是 `zsession` 会话 Cookie；网页视图是**独立的浏览器
  上下文**，不继承客户端的请求头。令牌两边通用（服务端 API 登录落的
  `zsession` 就是这枚令牌）→ 客户端把令牌种成 `zsession` Cookie 即可。
  种 Cookie 的**唯一时机是首屏导航之前**（原生视图还没建出来），
  `SetCookie` 必须能排队到"创建完成、首次导航之前"再落罐。
- **别吞 COM 的 HRESULT**：正文没装上时宿主只看到"空白"，看不到原因。
  `Navigate`/`LoadHtml` 失败要打出来，否则这类问题只能靠猜。

取证与验收（网页内容抓不到像素——它画在独立子 HWND 上，压在画布之上）：
① 控件树**分支判定**（网页容器可见 / 纯文本降级容器不可见）；
② **地址条**（原生 label，显示真实 URL）——落在 `/admin/login` 还是
`/admin/dashboard` 一眼分明。A/B 对照时注意 WebView2 的 Cookie 存在磁盘
（默认 `%LOCALAPPDATA%\ZanGui\WebView2\default`），用
`WEBVIEW2_USER_DATA_FOLDER` 换一个干净目录，否则第二次跑继承第一次的
Cookie，"对照组"就不成立了。

