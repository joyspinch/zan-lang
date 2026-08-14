# 云端本地协同工具包

把「本机工作区（主副本）」与「云端开发机（有完整工具链）」连起来的两个最小脚本。
分工：**开发/编译/测试都在云端做，只把验证过的交付物写回本机**；桥接只是搬运通道。

## 1. 配置凭据（不写进脚本，也不要提交）

```bash
export ZAN_BRIDGE_BASE="https://<当次桥接地址>"
export ZAN_BRIDGE_TOKEN="<当次 Bearer 令牌>"
export ZAN_BRIDGE_ROOT="d:/project/zan-lang"     # 本机工作区根，可省略
export ZAN_MIRROR="$HOME/repos/zan-lang"          # 云端镜像根，可省略
```

地址与令牌会轮换：取「本机文件桥接」笔记里的最新值。连不上（DNS/连接失败）=地址已换；
403 且 `error code: 1010` = 被按 UA 拦截，脚本已自带浏览器 UA。

## 2. 拉到云端（1:1 镜像，保持原相对路径与文件名）

```bash
python3 bridge_pull.py src stdlib CMakeLists.txt
```

目录走 `/api/bundle`（一次一棵子树），单文件走 `/api/file?raw=1`。

## 3. 在云端开发验证

```bash
cmake --build build -j2 && (cd build && ctest -L smoke -j2)
```

## 4. 写回本机（增量：字节相同自动跳过，改动才备份 + 落检查点）

```bash
python3 bridge_push.py "$ZAN_MIRROR" \
  src/compiler/irgen.c src/common/zan_abi.h \
  --summary "一句话说清这次改了什么、验证到什么程度"
```

`--summary` 会作为 `handoff.summary` 一起提交，便于后续会话接续。只写要交付的源码/配置，
临时探针与日志（如 `_scratch/`）不要写回。

## 5. 本机侧命令（构建、git 状态、提交）

```bash
curl -s -X POST "$ZAN_BRIDGE_BASE/api/terminal/exec" \
  -H "Authorization: Bearer $ZAN_BRIDGE_TOKEN" -H "Content-Type: application/json" \
  -A "Mozilla/5.0" -d '{"command":"git status --short"}'
```

## 省 token 的用法

- 先 `GET /api/workspace?manifest=1` 看全貌，别逐层爬目录；
- 找东西用 `GET /api/search?q=...&matchLines=1`（支持 `mode=regex`、`ext=`、`glob=`），
  比下载全文再翻便宜得多；
- 只读需要的行：`GET /api/file?path=...&offset=&limit=`。
