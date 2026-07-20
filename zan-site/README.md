# Zan 官网 (Cloudflare Workers)

Zan 编程语言官网，使用 **Cloudflare Workers 静态资源**（Static Assets）托管，免费额度（10 万请求/天）足够使用。

## 目录

```
zan-site/
├── public/           # 站点静态资源（部署内容）
│   ├── index.html
│   ├── styles.css
│   └── favicon.svg
├── wrangler.toml     # Workers 配置（assets-only，无需服务端代码）
└── package.json
```

## 本地预览

```bash
npm install
npm run dev        # wrangler dev，本地 http://localhost:8787
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

## 绑定自定义域名

在 Cloudflare Dashboard → Workers & Pages → 该 Worker → Settings → Domains & Routes 添加自定义域名（域名需托管在你的 Cloudflare 账户）。

## 免费额度

Workers 免费计划：100,000 请求/天、静态资源请求不额外计费，个人/开源官网完全够用。
