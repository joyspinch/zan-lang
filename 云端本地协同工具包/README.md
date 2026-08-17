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

## 1.5 一个命令行覆盖全部动作：`br.py`（推荐）

`bridge_pull.py` / `bridge_push.py` 只管搬运；日常要看目录、搜代码、拉子树、写回、
在本机跑构建，用 `br.py` 一个入口就够：

```bash
python3 br.py files src/compiler 1          # 看目录（别逐层爬）
python3 br.py search "emit_oom_check" 30 c  # 带行号的内容搜索（正则）
python3 br.py pull stdlib/System/IO/ByteBuffer.zan
python3 br.py pullglob src/ide_zan "src/**/*.zan"
python3 br.py push "一句话说清改了什么、验证到什么程度" src/compiler/irgen.c
python3 br.py exec "cd /d d:\project\zan-lang && cmake --build build --config Release --target zanc" 3000000
```

它与两个最小脚本的差别，都是踩过的坑：

- **按字节传输**（`encoding=base64`，push/pull 双向）。用文本方式读写会毁掉二进制：
  交叉编译出来的 Linux ELF 拉回云端直接跑不起来，而现象只是「莫名其妙的段错误」；
- **大文件分块**（单次 `/api/file` 读取有上限，超了被静默截断，看着像文件本身坏了）；
- `push` 一次性带上 `handoff.summary`，写回与交接说明不会脱节。

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

或 `python3 br.py exec "git status --short"`。注意本机侧是 **cmd.exe**：`head` / `grep` /
`pkill` / `Select-Object` 这类都不存在（管道给 `Select-Object` 会报「不是内部或外部命令」）。
要跑 PowerShell 脚本就写全 `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\test.ps1 smoke`；
要截取输出就把全文取回云端再用本地工具过滤。

## 省 token 的用法

- 先 `GET /api/workspace?manifest=1` 看全貌，别逐层爬目录；
- 找东西用 `GET /api/search?q=...&matchLines=1`（支持 `mode=regex`、`ext=`、`glob=`），
  比下载全文再翻便宜得多；
- 只读需要的行：`GET /api/file?path=...&offset=&limit=`。

## 6. 本机跑长任务（构建 / 发布，几分钟以上）

`/api/terminal/exec` 是同步的，长任务会超时断连；用常驻终端三件套（`create` → `write` → `output`）：

```bash
# 1) 建终端（返回 id；shell 是 cmd.exe，不是 PowerShell）
curl -s -X POST "$ZAN_BRIDGE_BASE/api/terminal/create" \
  -H "Authorization: Bearer $ZAN_BRIDGE_TOKEN" -H "Content-Type: application/json" -A "Mozilla/5.0" -d '{}'

# 2) 送命令：字段名是 input（写成 data 会被静默忽略，只送进一个换行，看着像“命令没执行”）
curl -s -X POST "$ZAN_BRIDGE_BASE/api/terminal/write" \
  -H "Authorization: Bearer $ZAN_BRIDGE_TOKEN" -H "Content-Type: application/json" -A "Mozilla/5.0" \
  -d '{"id":"t_xxx","input":"powershell -ExecutionPolicy Bypass -NoProfile -File scripts\\publish_ide.ps1 -NoBump > _scratch\\publish.log 2>&1 & echo DONE_RC=%ERRORLEVEL%\r\n"}'

# 3) 轮询：offset= 拿增量；也可以直接读重定向出来的日志文件
curl -s -H "Authorization: Bearer $ZAN_BRIDGE_TOKEN" -A "Mozilla/5.0" "$ZAN_BRIDGE_BASE/api/terminal/output?id=t_xxx&offset=0"
```

要点：

- 终端是 `cmd.exe`，PowerShell 语法（`Select-Object` 等）必须用 `powershell -NoProfile -Command "..."` 包起来；
- 把输出重定向到 `_scratch\*.log` 再用 `/api/file` 读，比翻终端缓冲稳，也便于只读尾部；
- 单条命令用 `... & echo DONE_RC=%ERRORLEVEL%` 收尾，轮询看到 `DONE_RC=` 就知道跑完了（cmd 的 `&` 是顺序执行，不是后台）。

## 7. 发布 IDE 预览版到 dist

```
powershell -ExecutionPolicy Bypass -NoProfile -File scripts\publish_ide.ps1 [-NoBump] [-SkipBuild]
```

`publish_ide.ps1` 默认先 `bump_version.ps1` 升补丁号再编译；`VERSION` 已升过但上次发布失败时用
`-NoBump` 复用当前版本，别把版本号白升一格。成功的收尾行是
`PUBLISH_OK v<版本> -> ...\dist\win-x64\ZanIDE.exe`，产物为扁平布局（exe + 几个驱动 dll +
`toolchain\ stdlib\ examples\ templates\ knowledge\ docs\`）。
