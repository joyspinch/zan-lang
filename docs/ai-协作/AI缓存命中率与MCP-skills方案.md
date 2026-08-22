# IDE AI 助手缓存命中率低的原因与改进方案（zan-lang）

面向 `d:\project\zan-lang` 的现状写的。结论先给：缓存低不是模型问题，
是**上下文前缀被反复打断 + 每轮塞进大量易变内容**。改法分三层：会话纪律、IDE 配置、线上 MCP+skills 架构。

---

## 0. 结论速览

| 症状 | 真实原因 | 改法 |
|---|---|---|
| cache hit 长期 <30% | 前缀里混了易变内容（时间戳、git 状态、打开文件列表、目录快照） | 把易变内容移到对话尾部，规则/清单固定不动 |
| 每次都像重新认识工程 | 会话被 /clear、/compact、切模型、改规则打断 | 一任务一线程，append-only；只在任务边界清 |
| 反复读同一文件 | 缺工程地图，靠爬目录；改完又整篇回读校验 | 先建索引/接口摘要；改完用**编译器**校验而不是回读 |
| 上下文很快爆 | 日志/exe/构建产物进了检索与上下文 | ignore 文件 + 只 grep 不整读 + 输出 `tail` |
| 停一会儿再问就全量重算 | 前缀缓存 TTL 只有几分钟（典型 5min，可续期） | 同一任务连续做完，别中途长时间挂起 |

---

## 1. 缓存到底怎么工作（决定了所有优化方向）

所有主流厂商（Anthropic / OpenAI / Gemini / DeepSeek）的推理缓存都是**前缀缓存（prefix cache）**：

1. 只有从第 0 个 token 开始**逐字节完全相同**的那一段能命中；
2. 一旦某个位置的 token 变了，**它之后的全部内容都要重算**；
3. 命中通常还有块粒度（如 1024/128 token 对齐）与 TTL（常见 5 分钟，被复用则续期）。

由此得到三条铁律：

- **前面放死的，后面放活的。** 系统提示 → 工具定义 → 规则/skills → 工程稳定摘要 → 历史对话 → 本轮易变信息。
- **只追加，不回改。** 追加内容不破坏已缓存前缀；编辑历史消息、删除旧工具输出、压缩/摘要、重排附件，都等于全量失效。
- **工具清单属于前缀。** 增删一个 MCP 工具、改一个工具描述，整轮缓存归零。所以工具集要**冻结**、要少。

### 你这边最可能的具体破坏源（已在工作区确认）

- 工作区 1.07 GB / 2256 文件，其中 **422 个 .exe、378 个 .log**，还有 `build/`、`dist/`、`Testing/`、
  `_devin_ref_tmp/`、`_scratch/`、`对话记录/`、`devin_ref.zip`、`devin_sync.bundle`、根目录一堆
  `*.log`（`build_a2.log`、`zan_leaks.log`…）。IDE 的自动索引/自动附加上下文会把这些卷进来：
  每次内容都不同 → 前缀每轮都变 → 缓存必然低，且上下文迅速被挤爆触发压缩（再次清零）。
- 缺 `.cursorignore` / `.gitignore` 之外的 AI 侧忽略清单，也没有 `.cursor/rules`、`CLAUDE.md`、`.mcp.json`，
  等于把「放什么进上下文」完全交给 IDE 的启发式。
- C/C++ 编译型工程 + GUI/服务端测试：构建与测试输出极长（`build_a2.log` 之类），
  整段贴进对话是缓存与预算的双杀。
- 根目录有 `sk.txt`、`secrets.local.ps1`、`tls_test_*.pem`。**顺手提醒**：这类文件既是泄密面，
  也常被 AI 全文读入上下文，建议移到工作区外或加进 AI ignore 与 `.gitignore`。

---

## 2. 会话纪律：让「直接开发」代替「反复读写」

反复读写的根因是**信息没沉淀**，每轮都得重新发现。把发现成本一次付清：

### 2.1 先建三份稳定资产（放进前缀，长期不动）

1. `AGENTS.md`（已有，6.4KB，OK）——只写不变的规范：构建命令、测试命令、目录约定、禁止事项。
   **不要**往里写「当前进度」「TODO」，那属于易变内容，一改就废掉全局缓存。
2. `docs/ai-协作/工程地图.md`——模块 → 目录 → 关键文件 → 对外接口签名（谁调谁）。
   compiler 这类工程尤其值：`src/lexer|parser|sema|codegen|runtime|stdlib` 各自入口与不变量。
3. `.agents/skills/*/SKILL.md`（已有 3 个测试类 skill）——把「怎么跑 GUI 测试 / 怎么起 admin server」
   这类反复重现的过程固化，避免每次让模型现场摸索。

有了这三份，模型不需要爬目录，读的是稳定文本 → 天然可缓存。

### 2.2 一次成型的编码回路（关键）

```
定位（grep 带行号，只看命中行）
  → 确认接口（读接口签名区间，不整读文件）
  → 一次性写出完整补丁（同一文件的所有改动合成一次编辑）
  → 用编译/测试当校验器（不要回读整文件确认）
  → 只读报错涉及的那几行 → 修 → 再编译
```

配套习惯：

- 读文件用**行区间**（`offset/limit`），不要整读大文件；桥接侧对应 `/api/file?offset=&limit=`。
- 搜索用 `grep -n` 语义（桥接 `/api/search?matchLines=1`），返回行号+命中行即可定位。
- 构建/测试输出**必须裁剪**：`cmake --build . 2>&1 | tail -n 60`、`ctest -R <单个用例> --output-on-failure | tail -n 80`。
  永远不要把 `build_a2.log` 整个贴进对话。
- 一个任务一个线程，做完再开新的；**不要**中途 `/compact`、切模型、改规则文件。
- 需要换方向时，新开线程 + 让它读稳定资产，比在旧线程里挣扎更省 token 也更准。

### 2.3 「以瞎猜接口为耻」与省 token 不冲突

不是少读，而是**读得准**：读接口定义（头文件 / 声明区间）而不是读实现全文；
不确定就 grep 定义处再读那 20 行。猜接口的代价（编译失败 + 返工）远高于精准读的代价。

---

## 3. 专用 AI IDE 怎么配才吃得到缓存

### 3.1 通用配置动作（今天就能做）

在工作区根加 AI 侧忽略清单（Cursor 用 `.cursorignore`，Claude Code / Codex 走各自 ignore 或
`AGENTS.md` 明确禁读目录）：

```gitignore
# 构建与产物
build/
dist/
Testing/
toolchain/**/bin/
**/*.exe
**/*.dll
**/*.so
**/*.a
**/*.lib
**/*.o
**/*.ll
# 日志与快照
**/*.log
**/*.err
**/*.out
**/*.trace
**/*.dump
_devin_ref_tmp/
_scratch/
对话记录/
devin_ref.zip
devin_sync.bundle
**/*.bundle
**/*.patch
**/*.diff
# 敏感
sk.txt
secrets.local.ps1
**/*.pem
```

再加一份 `CLAUDE.md`（或让它 include `AGENTS.md`，保持单一真源），内容只放稳定规范。

### 3.2 各家的注意点

- **Claude Code**：system + tools + CLAUDE.md 会被 cache_control 标记，天然可缓存；
  杀手是 `/compact`（重写整个前缀）和会话中途改 CLAUDE.md/新增 MCP。用 `/cost`、
  或 API 用量里的 `cache_read_input_tokens / cache_creation_input_tokens` 看命中率。
  目标：稳定任务里 cache_read 占输入 token 的 70%+。
- **Cursor**：自动 prompt caching，但 `@codebase`、自动附加上下文、每轮变化的
  "open files / linter errors" 都会插进前缀。做法：关掉自动附带、手工 `@file` 指定少量文件、
  规则写进 `.cursor/rules`（项目级、稳定），别把 TODO 写进规则。
- **Codex CLI / Cline / Roo**：确认打开 prompt caching 开关；Cline 的「上下文压缩」等同缓存清零，
  宁可换新任务也别频繁压缩。
- **通用**：别在一个任务里混用两个模型/两个 provider——不同前缀、不同缓存池，等于每次冷启动。

### 3.3 度量（不度量就是瞎调）

拿到每轮的 `cached_tokens / cache_read_input_tokens` 占比，记三条曲线：命中率、单轮输入 token、
任务完成轮数。做上面的改动后，正常表现是命中率上升 + 单轮输入下降 + 轮数下降。

---

## 4. 线上通用 MCP + skills：可行，而且是正确方向

**能部署，且推荐**：把本机桥接那套能力做成一个**远程 MCP（Streamable HTTP + Bearer/OAuth）**，
所有 IDE（Cursor / Claude Code / Codex / Web）都连同一个地址，工具定义全网一致。

### 4.1 但要先说清一件事

远程 MCP 本身**不会**提高缓存命中率——缓存在模型推理侧，跟工具跑在哪无关。
它提高命中率是通过两件事：

1. **工具清单被冻结成同一份**：工具名/描述/schema 全局一致且极少变动 → 前缀里工具段永远命中。
   （反例：每个项目一套 MCP、按需开关 MCP，前缀每次都变。）
2. **skills 从「塞进提示」变成「按需拉取」**：系统提示只留一份很小的技能目录（名字+一句话），
   正文用工具在需要时才取。前缀小且稳定，正文增量追加。

### 4.2 推荐架构

```
                ┌──────────── 远程 MCP（一个，全球唯一入口）────────────┐
IDE / Devin ───▶│  工具集冻结为 6 个：                                  │
                │   ws.manifest   工程全貌（缓存友好、结果稳定）        │
                │   ws.search     grep -n 语义，带行号/正则/glob        │
                │   ws.read       行区间读（必须传 offset/limit）        │
                │   ws.write      批量写回（bundle 语义 + handoff 摘要）│
                │   ws.exec       跑构建/测试，服务端裁剪输出           │
                │   skill.get     取 skill 正文（list 走 resources）     │
                └───────────────────────┬───────────────────────────────┘
                                        │  按 workspaceId 路由
                        ┌───────────────┴────────────────┐
                   本机桥接（zan-lang, win32）        其他机器/工作区
```

设计要点（每条都直接服务于缓存）：

- **工具数量少且冻结**：宁可一个 `ws.search` 带参数，也不要 `search_ts / search_cpp / search_symbol` 三个。
  工具描述定稿后当 API 契约看待，改动要像发版一样慎重（改一次全网缓存清零）。
- **技能渐进披露**：MCP `resources`（或一个极简 `skills/index.md`）只列
  `name + 30 字用途`；模型判断需要时才 `skill.get(name)` 取全文。
  这样 100 个 skill 的成本 ≈ 100 行目录，而不是 100 篇正文。
- **服务端负责省 token**：默认裁剪（read 强制行窗口、exec 只回尾部 N 行 + 错误摘要、
  search 每文件≤20 命中），把「省上下文」做成服务端不变量，而不是靠模型自觉。
- **确定性输出**：同一请求返回同样字节（字段顺序固定、不带时间戳/随机 id/耗时统计）。
  工具结果进的是历史对话，带时间戳就等于给后续所有轮制造差异。
- **无状态 + 幂等**：每次调用自带 workspace/path，服务端不藏会话态，方便多 IDE 并发复用。
- **鉴权**：Bearer/OAuth + workspace 级 ACL；只读模式对 `ws.write/ws.exec` 返回 403。
  （你现在的 token 轮换机制可以保留，但把它挪到 MCP 网关内部，客户端配置就能长期不变
  ——客户端配置变了同样会动前缀。）

### 4.3 skills 放哪儿

- **项目内 `.agents/skills/`**：与代码强绑定的过程（你现在的 GUI/admin 测试三件套）——跟着仓库走。
- **线上 MCP 托管**：跨项目通用的（桥接同步、发版、性能剖析、崩溃三板斧），
  统一升级、所有 IDE 立即生效。
- 判断标准：会随代码一起改的放仓库，独立演进的放线上。

### 4.4 落地路径（增量，不推倒重来）

1. 先把现有 `云端本地协同工具包/br.py` + 桥接 HTTP API 包一层 MCP server（Node 或 Python SDK 都行），
   工具就上面 6 个，直接转发到桥接。
2. 部署到一台常驻机（或 Cloudflare Tunnel 命名隧道 / Fly / 自家内网），走 Streamable HTTP。
3. 各 IDE 写死一份配置（示例，Claude Code / Cursor 同构）：

```json
{
  "mcpServers": {
    "ws": {
      "type": "http",
      "url": "https://mcp.<你的域名>/mcp",
      "headers": { "Authorization": "Bearer ${WS_MCP_TOKEN}" }
    }
  }
}
```

4. 观察命中率与轮数两周，再决定是否补 `ws.codegraph`（符号级上下文，比 grep 更省，
   但**新增工具会清一次缓存**，攒到一起发）。

---

## 5. 本周落地清单

- [ ] 加 `.cursorignore`（内容见 3.1），把 build/dist/日志/exe/中文快照目录全屏蔽
- [ ] `sk.txt`、`secrets.local.ps1`、`*.pem` 移出工作区或纳入 ignore + `.gitignore`
- [ ] `AGENTS.md` 清掉进度类内容，只留不变规范；新增 `docs/ai-协作/工程地图.md`
- [ ] 约定输出裁剪：所有构建/测试命令一律 `| tail -n 60`
- [ ] 一任务一线程；禁用中途 `/compact` 与换模型
- [ ] 记录每轮 `cache_read / input` 比例，做基线
- [ ] 把桥接包成远程 MCP（6 个冻结工具 + skills 渐进披露），各 IDE 统一接入

做完 1–5 项通常就能把命中率从「几乎不命中」拉到 60–80%（大头是 ignore + 不压缩 + 输出裁剪）；
第 7 项解决的是「多 IDE / 多机器一致性」和 skills 规模化，属于结构性收益。

---

## 6. ZanIDE 内置助手：已落地的改动（本轮）

目标：让助手**别再反复读同一段代码**，并让请求前缀稳定下来给 provider 命中缓存。

### 6.1 上下文从「每轮重发」改成「只追加一次」

- `AiAgent.ContextBlock` 不再把当前打开文件的正文（原来最多 6000 字符）放进
  每轮请求尾部，只留一句指针：正文已经钉在对话里，直接用，别 `read_file`。
- 新增 `AiAgent.PinNotes(ctxName, ctxCode, root)`：只有**文件名或正文变了**才
  产出一条 open-file 记录，项目参考同理；`ZanIDE.AiLaunchAs` 把它追加进历史。
  于是同一份正文在整段对话里只出现一次，并且落在**前缀**里，可以被缓存。
- `BuildContext` 折叠历史（sticky compaction）时调 `AiAgent.ResetPins()`：折掉的
  正文下一拍会重新钉一份，避免「手上没正文 + 重复读被拒」两头堵。

### 6.2 三类重复读取都会被当场拒掉

`ZanIDE.AiTools` 里对 lookup 类工具：

| 情况 | 判定 | 返回给模型 |
| --- | --- | --- |
| 完全相同的调用 | `AiSeenTool(CallSig)` | 这个调用刚才已经跑过了 |
| 同一文件、行区间被已读区间覆盖 | `AiSeenRead`（`aiReadPaths/From/To`） | 这些行落在读过的区间里 |
| 读的就是用户正打开、已钉全文的文件 | `AiPinnedRead` | 完整现状已经钉在对话里 |

被拒时不执行查询，只回一句原因 + `aiStatDupTools + 1`。

### 6.3 写入后只失效相关缓存

- 原来写完一个文件就 `AiForgetTools()` 全清，模型于是把刚读过的文件重新读一遍。
- 现在 `AiForgetToolsFor(path)`：只清掉**该文件**的调用签名与已读区间，外加工程
  级视图（`grep` / `list_dir` / `repo_map`）；其它文件的读取记录保留。
- 构建本身不再清空（构建不改源码）。

### 6.4 系统提示里写死读取纪律

`BaseInstructions` 的 WORKFLOW 增加 `2a. READ ONCE, PRECISELY, THEN EDIT`：
grep 拿 `path:line` → 只读那一窗（offset/limit 80–200）→ 一次 `edit_file` 改完 →
build。禁止「改一行、回读、再改一行」。

### 6.5 当前文件用工程相对路径

`AiPanel` 里上下文文件名从 `Path.GetFileName` 换成 `ZanIDE.AiDropRef(path, root)`，
模型拿到的就是能直接喂给 `read_file` / `edit_file` 的相对路径，不必再 grep 找位置。

### 6.6 怎么看有没有效

用量面板（会话行）现在有三项：

- **缓存命中**：provider 返回的 `prompt_tokens_details.cached_tokens` 占 prompt 比例
- **前缀复用**：本地算的相邻两次请求 JSON 共同前缀字节数占比
- **重复查询拦下**：被上面三条规则拒掉的次数

验证方式：同一段对话里问 3–5 轮同一个模块的改动，观察「前缀复用」是否稳定在高位、
「重复查询拦下」是否 > 0、以及总轮数/耗时是否下降。

构建与验证：`scripts\build_ide.ps1` → `IDE_BUILD_OK`；`scripts\test.ps1 smoke`
→ 124/124 通过。
