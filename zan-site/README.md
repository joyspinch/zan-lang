# Zan 官网与文档

中文展示站与语言文档站，采用原生 HTML、CSS、ES modules 和 Node 内置工具；没有第三方前端运行依赖。构建从当前仓库读取文档与包清单，输出可独立部署的静态站点。

## 本地预览

在仓库根目录运行：

```sh
npm --prefix zan-site run dev
```

命令先构建，再启动 `http://127.0.0.1:4173/`。通过 `PORT` 环境变量可更换端口。服务统一读取 `build/zan-site/`，保证本地与部署后的文档、包目录、AI 导出一致。修改源码后重新运行构建并刷新页面；没有热更新。

已构建时只启动预览：

```sh
npm --prefix zan-site run preview
```

## 检查与构建

```sh
npm --prefix zan-site run check
npm --prefix zan-site run build
```

`check` 检查 JavaScript 语法，构建站点，再执行 `tests/site/docs.test.mjs`。检查覆盖：静态页面正文、Markdown 与全文一致性、内部链接与锚点（含动态包卡片及首页搜索栏目）、当前包清单与真实命名空间、文本转义，以及 GET/HEAD/404 和内容类型。文档里的完整 Zan 程序仍需要使用当前编译器验证；静态站点检查不替代语言运行测试。

输出位于仓库 `build/zan-site/`。将该目录内容按现有发布流程上传静态托管即可；不需要 API 服务器或 SPA 路由回退。页面和资源使用相对路径，支持子路径部署。

需要 canonical 与 sitemap 时，构建前配置真实部署根地址（示例域名需替换）：

```powershell
$env:SITE_URL = "https://example.com/zan/"
npm --prefix zan-site run build
```

未配置时不推测域名，仍生成 `robots.txt` 和全部文档入口。配置时生成匹配子路径的 `sitemap.xml`、canonical 和 Open Graph URL。静态托管请按扩展名提供 HTML、Markdown、TXT、JSON、XML 的正确 MIME 类型，并保留真实 404。

## 内容来源与生成关系

- `docs/*.md`：人工维护的语言、入门、库、工具与 AI 文档正文。
- `src/documentation.js`：章节顺序、摘要、分类和关键字。
- `src/capabilities.js`：首页与文档共享的平台实现、运行记录与交付边界。
- `src/ecosystem.js`：包的场景分类与核心标准库摘要；包名、版本、状态与许可来自构建读取的清单。
- `src/package-guide.js`：首页、文档与搜索共享的中文包标题和已核实的导览修正；完整目录另外保留清单描述原文。
- `scripts/generate-docs.mjs`：生成 HTML、Markdown、完整包目录、搜索语料、文档清单和 AI 全文。包命名空间从 `.zan` 的实际声明读取，不能由目录推断。
- `scripts/markdown.mjs`：本项目文档使用的受限 Markdown 渲染器，支持标题、段落、代码围栏、列表、表格、引用和链接；正文中的 HTML 会转义。
- `src/docs.css`、`src/docs.js`：可直接阅读的文档布局与渐进增强全文搜索。
- `src/content.js`、`src/app.js`、`src/explore.js`：首页展示和搜索；指南入口直接导航到文档页。
- `agent-guide.md`、`llms.txt`：英文 AI 入口与主题索引。

所有生成内容只进入 `build/zan-site/`，包括 `src/package-catalog.js`。维护时应保留仓库 `packages/` 和 `assets/brand/`，构建不是把 `zan-site/` 单独复制出去后执行。

## 品牌与视觉

网站直接使用 `assets/brand/VI.md` 配套的官方鹅标、白色路径字标、赞宝与大肥鹅主视觉，以及官方 PNG 分享横幅。`scripts/build.mjs` 从仓库品牌目录复制这些资产，映射见 `assets/README.md`；站点不维护另一套 Logo 副本。

首页与所有文档共享 `src/theme.css` 的深蓝、电光蓝、金色点缀及字体变量。首页的布局与组件适配在 `src/brand.css`，文档的阅读布局在 `src/docs.css`；最后加载的 `src/controls.css` 统一下拉框、滚动条、焦点、搜索与禁用状态。下拉框保留原生选择语义：支持 `appearance: base-select` 的浏览器使用配套弹出面板，旧浏览器保留系统选择器；浅色应用演示使用独立的控件色板。

修改主题时应同时检查桌面和手机：首页首屏、交互演示、搜索、示例卡片、包目录与 FAQ，以及文档正文、侧栏、代码块和表格。真实示例与模板入口指向发行仓库中已有的公开目录；包卡片直达对应文档锚点。`src/package-guide.js` 为全部当前包提供中文用途摘要，更新时保留实验性、迁移中与许可边界。

## 发布后的文档入口

- `docs/index.html`：文档目录与全文搜索；关闭 JavaScript 仍能阅读全文。
- `docs/<topic>.html` 与 `docs/<topic>.md`：相同正文的网页和 Markdown。
- `llms.txt`：推荐读取顺序与所有主题链接。
- `llms-full.txt`：英文 agent guide、全部主题与完整包目录；适合检索或上下文导入。正文相对链接调整为以全文文件为基准，代码示例保持原样。
- `docs/manifest.json`：schemaVersion、核实日期、页面/Markdown 地址、来源和内容 SHA-256。
- `docs/packages.json`：当前全部包的清单元数据与实际命名空间。
- `docs/search.json`：与已发布正文一致的全文检索语料。

`llms.txt` 是检索入口，不保证所有模型自动读取，也不等于训练模型。编程时仍须查询安装的 SDK 并编译运行。

## 内容维护约定

事实以当前实现和回归用例为依据，引用文档前检查是否过时。每篇有核实日期、版本范围与“核实来源”；来源中的仓库路径不是发行仓库的公开下载地址。维护证据见 `CONTENT_NOTES.md`。

文档完整程序使用 `zan` 围栏；需要上下文、不能单独运行的片段使用 `zan-fragment`，并在正文说明。修改示例后重新编译运行。不要把代码展示、AI 调用动画或 ARC 动画说成在线编译结果。

新增文档时同时登记 `src/documentation.js` 并更新 `llms.txt`。正文内部使用相对 Markdown 链接，构建器转为网页链接；网站根资源使用 `../`。渲染器不接受任意 Markdown 扩展，新语法应先增加有意义的检查。

平台状态必须分别说明编译、运行、GUI、交付。模拟器、WSL、QEMU 与实体设备区分记录；实现存在不等于全平台验收。包状态只采纳清单显式标记，未标记时称“源码可用”，不推断生产成熟度。

工具链根许可为 ZCSL，含 Runtime & Output Exception；包清单各自声明许可。首页不再用 MIT 概括整个语言。对外下载统一指向 `https://github.com/joyspinch/zanlang`，按其当前 README 与发行文件选择；不维护“Releases 永远为空”等会过期的事实。
