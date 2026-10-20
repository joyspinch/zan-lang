# Zan.Oss

对象存储操作 SDK：阿里云 OSS、腾讯云 COS、S3 兼容存储（AWS S3 / MinIO / 各家云
S3 网关）。纯 Zan 实现，零原生依赖。

## 能做什么

- 写入 / 读取 / 存在性检查 / 删除对象
- 流式下载到本地文件（先 HEAD 校验状态，避免把 404 错误正文当素材存盘）
- 凭证体检（发一次桶级签名请求，把 403/404 翻译成人能读的原因）
- 对象的公网直链（PublicUrl）

## 用法

```zan
using System.Oss;

OssConfig cfg = new OssConfig();
cfg.driver = "aliyun_oss";          // aliyun_oss / tencent_cos / s3
cfg.endpoint = "https://oss-cn-hangzhou.aliyuncs.com";
cfg.bucket = "my-bucket";
cfg.accessKey = "...";
cfg.secretKey = "...";
cfg.basePath = "ai/generated";      // 对象键前缀（可空）
cfg.pathStyle = true;               // 路径风格（MinIO/兼容网关常用）

OssClient client = OssClient.Create(cfg);
if (client == null) { /* 驱动不认识 */ }

OssResult put = await client.Put("a/b.png", bytes, "image/png");
OssResult got = await client.Get("a/b.png");
OssResult head = await client.Head("a/b.png");   // ok=false 且 status=404 → 不存在
OssResult del = await client.Delete("a/b.png");
OssResult toFile = await client.GetToFile("a/b.png", "local/b.png");

string problem = await client.Check();            // 空串 = 凭证与桶都对
string direct = client.PublicUrl("a/b.png");
```

`OssResult` 字段：`ok`（HTTP 2xx）、`statusCode`、`error`（失败原因，正文前 300 字）、
`etag`、`body`/`bodyLength`（Get 的正文）。

## 端点与风格

| driver | 端点 | 风格 | 签名 |
|---|---|---|---|
| aliyun_oss | `https://oss-cn-hangzhou.aliyuncs.com` | `pathStyle=true` 走 `端点/桶/键`；否则 `桶.端点/键` | OSS V1（HMAC-SHA1），头 `Authorization: OSS ak:sig` |
| tencent_cos | 留空则 `桶.cos.区域.myqcloud.com`；可写 `{bucket}` 占位 | 虚拟主机 | XML API（`q-sign-algorithm=sha1`） |
| s3 | `https://s3.amazonaws.com` / `https://minio.local:9000` | `pathStyle=true` 走路径风格 | AWS SigV4（HMAC-SHA256 派生密钥链） |

## 约定与边界

- 端点没给 scheme 时按 HTTPS 处理；S3 未指定 region 时用 `us-east-1`。
- 自测或内网端点：默认外呼策略禁止连回环地址，需要 `cfg.callPolicy =
  ExternalCallPolicy.Default().AllowLoopback().AllowLocalHttp()`。
- 签名算法是纯函数（`AliyunOssSigner` / `TencentCosSigner` / `S3Signer`），可脱离
  网络单独编译对账；conformance 用例 `oss_storage` 把三家固定向量钉死。
- 暂不覆盖：分段上传/分片、列举桶内对象（List）、服务端加密、生命周期策略、
  跨区域复制、CDN 回源配置。
