# Zan 官网 (Cloudflare Workers)

Zan 编程语言官网，使用 **Cloudflare Workers 静态资源**（Static Assets）托管，免费额度（10 万请求/天）足够使用。

## 目录

```
zan-site/
├── public/           # 站点静态资源（部署内容）
│   ├── index.html    # 官网首页（手工维护）
│   ├── styles.css    # 站点样式（手工维护）
│   ├── app.js        # 语言切换/滚动监听/搜索等
│   ├── app.json
│   ├── lang.html / gui.html / stdlib.html / wiki.html / examples.html
│   │                  # 文档页（由 guides/*.md 生成）
│   ├── lang.md / gui.md / ...     # 同一内容的 Markdown（供 AI 检索）
│   └── ref/          # 标准库 API 参考（自动生成）
│       ├── index.json # 命名空间索引（机器可读，供 AI 检索）
│       ├── <ns>.html  # 每命名空间参考页
│       └── <ns>.md    # 每命名空间 Markdown（供 AI 检索）
│       # 全量 API 模型是构建中间产物 gen/ref-data.json（~19MB），只在构建时使用，不部署
├── guides/           # 手写指南（Markdown 源）
│   ├── lang.md       # 语言参考（全部语言能力，实测语义）
│   ├── gui.md        # GUI 开发指南（.zform + 代码式组件 + 控件）
│   ├── stdlib.md     # 标准库总览与索引
│   ├── wiki.md       # IDE 指南
│   └── examples.md   # 示例与模板
├── gen/
│   ├── api_extract.py  # 从 ../stdlib/**/*.zan 提取 API → public/ref/data.json
│   └── site_build.py   # guides/*.md → HTML + ref/data.json → ref/<ns>.html/.md
├── wrangler.toml     # Workers 配置（assets-only，无需服务端代码）
└── package.json
```

## 重新生成文档

标准库更新或指南修改后，运行：

```bash
python gen/api_extract.py   # 1) stdlib → public/ref/data.json（约 10 s）
python gen/site_build.py    # 2) 数据 + guides → 所有 HTML/MD 页面
```

- `api_extract.py` 是语法感知的 token 扫描器：解析 namespace/类型声明/成员
  （字段/属性/方法/构造/运算符/事件/嵌套类型），把 `///` 文档注释附着到
  声明上。全量模型写到 gen/ref-data.json（仅构建用，不部署）；
  轻量索引 public/ref/index.json 随站部署。改动 stdlib 后重跑即可。
- 两个脚本输出全部提交到 `public/`，部署不依赖构建步骤。

## 本地预览

```bash
npm install
npm run dev        # wrangler dev，本地 http://localhost:8787
# 或纯静态：
python -m http.server 8787 --directory public
```

## 部署到 Cloudflare

1. 安装依赖并登录（首次）：
   ```bash
   npm install
   npx wrangler login          # 浏览器授权你的 Cloudflare 账号
   ```
2. 部署：
   ```bash
   npm run deploy              # 等价于 npx wrangler deploy
   ```
   部署后会得到 `https://zan-site.<你的子域>.workers.dev`。

### 用 API Token 部署（CI / 无浏览器）

```bash
export CLOUDFLARE_API_TOKEN=xxxxx        # 需含 "Edit Cloudflare Workers" 权限
export CLOUDFLARE_ACCOUNT_ID=xxxxx
npx wrangler deploy
```

## 文档约定

- 链接统一用**无扩展名**形式（`/lang`、`/ref/System.IO`）：Cloudflare Workers
  Assets 会把无扩展名路径自动解析到同名 `.html` 资产，同时会把 `.html`
  请求 307 剥掉扩展名——不要加 `.html` 后缀，也不要配置把这些路径
  重定向回 `.html`（会与 307 互跳成环）。
- 每份文档同时发布 `.md` 版本（`/lang.md`、`/ref/System.IO.md`），供 AI
  直接抓取作为检索上下文；`/ref/index.json` 是机器可读的全量索引。
- 导航栏活跃页标记：`.nav-links a.active`（styles.css）。
- 站点内容若与 `docs/SPEC.md` 冲突，以 conformance 用例与 stdlib 源码为准
  （SPEC.md 部分章节已过时，如 `?.`/record 实际已实现）。
