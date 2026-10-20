---
name: zan-sdk-agent
description: 在 zan-lang 源仓库里用配套工具干活：查地图定位、查影响面、算该跑哪组 ctest、跑模板门。改任何 .zan / 改公共 API / 要判断"这次改动该跑什么检查"时用它。
---

# 在 zan-lang 里干活

5900+ 个 `.zan`、65989 个符号、61027 个调用节点。**不靠读源码定向，靠地图。**

配套工具在 `tools/agent/`（本仓库自用）。它们只做判定得过的事，其余给命令。

## 0. 四条硬规矩

1. **定位不读文件，查地图。** 读文件只为了改它。
2. **改动必有判定器。** 说不出跑哪条命令验证的改动，不做。
3. **日常只跑 target 化测试。** `standard`/`full` 是发布门（AGENTS.md Rule 8），全套 3904 条，日常跑会占满所有核且与本改动无关。
4. **多文件项目必须枚举全部源。** zanc 不自动拉取同项目兄弟 `.zan`，单入口编译必报 `undefined type`。

## 1. 定向：地图三问（毫秒级）

```powershell
tools/agent/map.ps1 -Find 'ReadAllText'          # 符号在哪（file:line + 签名）
tools/agent/map.ps1 -File 'stdlib/System/IO/File.zan'
tools/agent/map.ps1 -Callers 'File.ReadAllText'  # 谁在调它（全仓，含模板与示例）
tools/agent/map.ps1 -Callees 'UpdateManifest.LoadFromFile'
```

地图过期（`zan_start_here` 报 `resources: []`，或刚改过结构）就重生成：

```powershell
scripts/build_repomap.ps1          # 稳定入口，内部负责枚举 tools/repomap 的全部源
```

**别再直接用 `zanc tools/repomap/RepoMap.zan ...`**：RepoMap 已被拆成多文件
（`RepoMap.zan` + `Str.zan` + `Models/*.zan`），单文件形式必然报一堆
`undefined type 'RefBucket'`。

查签名：

```powershell
build/zanc.exe --api 'File.ReadAllText'
```

## 2. 工作集：20–50 条，超限就分层

动手前列出这次要碰的条目（文件 + 符号 + 不变量）。**超过 50 条说明分层失败**——
该拆任务，不是加读代码。定向顺序固定：地图 → 命中条目 → 只读那几条的窗口。

## 3. 分档：按"可验证性"决定花多少力气

| 档 | 什么时候 | 怎么做 |
|---|---|---|
| A | 编译 / 格式化 / 类型能判 | 直接改，跑级联，错了改 |
| B | 只能判"契约是否被破坏" | 先跑 `impact.ps1`，再看该跑哪组 ctest |
| C | 没有判定器（架构取舍、改名、需求歧义） | 不许自己拍板就改，举手或先写方案 |

**升级靠证据，不靠感觉。**

## 4. 判定器级联

```powershell
# 一次拿到：必跑检查 + 该跑哪组测试
tools/agent/verify.ps1 -Paths stdlib/System/IO/File.zan

# 只预估，不执行
tools/agent/verify.ps1 -Paths src/compiler/checker.c -DryRun

# 真跑那一组（内部只用 smoke 档 + -Match）
tools/agent/verify.ps1 -Paths tests/conformance/aes_gcm.zan -RunTests
```

实测过的判据与坑：

- **fmt**：`zanfmt` 只做缩进规整；`zanfmt --check` 在 Windows 检出上不可用——
  `core.autocrlf` 把 `.zan` 变成 CRLF，而 `.gitattributes` 规定提交 LF，
  于是原样比较全仓 100% 假红（实测 8356/8356）。工具内部改为
  "归一化行尾后比较 `--stdout` 结果"，只差换行不算不格式。
- **compile**：单文件编译失败会自动回退到**模块整体编译**（`packages/<Pkg>`、
  `tools/<Tool>`）；多文件项目因此不再假红。**不用 `--deny-warnings`**：
  基线不干净时它会把 stdlib 既有告警升成错误（实测 Zan.Net
  `TaskCompletionSource.zan:101`），与本次改动无关。告警只报不拦。
- **ctest**：正则不写死命中数，工具用 `ctest -N` 现场验证；命中 0 就报"映射失效"。
  已验证的前缀见 `tools/agent/verify.rules.json`。
- **模板**：Windows 用 `tools/agent/gate-templates.ps1`（按 `template.manifest`
  + `zan.proj` 的声明做替换副本）；macOS 用 `scripts/test_all_templates.py`
  （官方脚本，硬编码 macOS 路径）。模板源带 `{{NAME}}` 占位符，**不能直接编译**。
- **新增 `tests/conformance/*.zan` 必须 re-configure 才进 ctest**：conformance 用例
  是 CMake `file(GLOB)` 登记的，GLOB 在 configure 时求值——不重新 configure，新用例
  **文件在、门不跑**。实例：`decimal_arithmetic` / `weak_reference_generic` 两个用例
  随特性一起提交后从未进过 ctest（`ctest -N -R decimal` 才有）。改完 conformance
  目录先 `cmake -B build` 再跑。
- **批量改可见性必须带影响面**：把成员改成 `private`（或补 `public`）会立刻打断
  外部调用点，而这类破坏不会自己暴露。实例：`Socket.ResolveIpv4` 规范成 `private`
  后，`conformance_async_dns` / `conformance_async_mt` 从那天起一直红，直到有人
  手工重跑才发现——档位日志还停在改动前一天。改前先 `tools/agent/impact.ps1`，
  改后定向跑受影响用例。

## 5. 影响面：改公共面之前必跑

```powershell
tools/agent/impact.ps1 -Path stdlib/System/IO/File.zan
tools/agent/impact.ps1 -Symbol 'File.ReadAllText'
```

出现 `CROSS-BOUNDARY` 就别只看编译结果——本仓库历史上最贵的一类漏改就是
"公共 API 改了，模板/示例里的调用者没跟上"。

## 6. 写回

- 改 `stdlib/`、`packages/` 公共面 → 刷地图（`scripts/build_repomap.ps1`）+ 跑模板门。
- 修好一个可复发的编译错误 → 留一条能拦住它的用例（`tests/conformance/`）。
- 临时探针、编译产物只进 `_scratch/`，不进根目录。
- 批量提交（一次暂存几千个文件，例如把一个大型模板搬出仓库）→ 提交门禁的
  blob 大小检查必须走**批量**（`git diff --cached --raw` + `git cat-file
  --batch-check`）。逐路径起 `git cat-file -s` 是每路径一个进程：7882 条
  暂存路径能把一次提交拖到半小时以上，门禁事实上等于不可用；改批量后同样的
  检查 0.3 秒跑完（冲突标记检查 0.5 秒）。改了钩子就顺手量一下耗时。

## 7. 红旗（出现即回退）

- 在读第 4 个文件还没动手 → 该用 `map.ps1`。
- 直接编译 `templates/**/App.zan`，或单文件编译多文件工具 → 必红，且不是真错。
- 报"编译通过"但没跑 `verify.ps1` → 不算验证。
- `impact.ps1` 报跨边界，却只跑了单文件编译 → 漏改已发生。
- 想跑 `scripts/test.ps1 standard` 来"确认一下" → 违反 Rule 8。
