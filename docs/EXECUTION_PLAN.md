# zan-lang 综合执行计划（2026-07-23）

> 本计划整合三类问题：(A) ZanIDE 生产可用差距（承接 `docs/PRODUCTION_PLAN.md`，
> 此处不重复其表格，只排序引用）；(B) C# 语法/LINQ 友好度差距；(C) 仓库
> 工作流问题（分支管理）。按优先级 P0 → P2 执行，每项含验收标准。

---

## Workstream A：ZanIDE 生产可用（引用 PRODUCTION_PLAN.md）

执行顺序保持 PRODUCTION_PLAN.md 的建议不变：

1. **P0 硬伤**（阶段 1）：
   - A1 修复 O0 codegen bug（`docs/bugs/O0-list-index-write-stack-leak.md`）。
     **已完成**：循环体内局部量/编解码临时量改为函数入口块一次性
     alloca（emit_entry_alloca），-O0 不再每次迭代增长栈
     （`tests/conformance/list_index_write_o0.zan`）。
   - A2 stdlib 错误模型统一：File/Net/Json 失败 throw 标准异常，不再静默返回空。
     **File 部分已完成**：`FileNotFoundException`/`IOException` 落地，
     读/写/复制失败抛异常（`tests/conformance/io_errors.zan`）；编译器
     builtin `File.*` 降级在 stdlib 源码存在时让位（`src_method_takes_over`）。
     **Net 部分已完成**：async try/catch codegen 修复（异常状态存 entry
     alloca，跨 CPS resume 块重新 load，`tests/conformance/async_try_catch.zan`）；
     `Socket.ConnectAsync` 通过运行时 `zan_io_connect_status`（select
     写/异常集探测 + SO_ERROR）返回连接结果，`TcpClient.ConnectAsync`
     失败返回 null，`HttpClient` 连接/TLS 失败抛 `HttpRequestException`
     （`tests/conformance/net_errors.zan`）。已知限制：异常穿过 async CPS
     帧时这些帧不被释放（net_errors 在 leakcheck 的 `_known_leaky`
     允许列表中，ARC 后续工作）。
     **Json 部分已完成**：`JsonValue.Parse` 对畸形输入抛 `JsonException`
     （先用无分配扫描校验再建树，错误路径无泄漏）；原 best-effort 行为
     保留为 `JsonValue.ParseLenient`，现有调用点（UI 文档/RPC/IDE）已
     迁移到 lenient，行为不变（`tests/conformance/json_errors.zan`）。
   - A3 IDE 长时运行泄漏验证（`--check-leaks` 2h+ 零净增长）。
2. **P1 编辑体验 + 质量保障**（阶段 2.1–2.3、4.1–4.2）：LSP rename、
   workspace/symbol、IDE 接入 zanfmt、IDE golden-path UI 测试、发布包冒烟测试。
3. **P1 产品化**（阶段 3）：代码签名 → 自动更新 → 崩溃上报 → 包商店。
4. **P1 收尾**（阶段 2.4–2.5、4.3）：增量文档同步/大文件、崩溃恢复、Regex。
5. **P2 跨平台**（阶段 5）：Linux/macOS 自包含工具链与 IDE 发布。

## Workstream B：LINQ / C# 语法友好度

背景：现 `System.Linq.Enumerable` 是对 `List<T>` 的急切求值实现，与 C# 手感
差异集中在：排序只支持 int 键、First/Last 返回默认值不抛异常、缺常用算子、
无查询语法、yield 急切降级为 List。

| # | 事项 | 优先级 | 验收标准 |
|---|------|--------|----------|
| B1 | ~~泛型可比较键~~ → 已落地为类型化键名：`OrderByStr/OrderByStrDescending`（string 键）、`OrderByNum/OrderByNumDescending`（double 键）、`OrderBy(k1, k2)` 二级 int 键（泛型键受编译器 bug 限制，见 `docs/bugs/generics-uniform-repr.md`） | **已完成** | `tests/conformance/linq_extended.zan` |
| B2 | 核心算子：`GroupBy/GroupByStr`、`SelectMany`、`Single/SingleOrDefault`、`LastOrDefault`、`Sum/Min/Max/Average(selector)`、`TakeWhile/SkipWhile`、`ContainsStr/DistinctStr/InStr`（`ToDictionary` 被 Dictionary 泛型值 bug 阻塞，见 bug 文档 §5） | **已完成** | 同上 |
| B3 | 异常语义对齐 C#：空序列 `First/Last/Single` 抛 `InvalidOperationException`；保留 `*OrDefault` 返回默认值 | **已完成** | 同上（`stdlib/System/Exception.zan` 新增 InvalidOperationException/ArgumentException） |
| B4 | `Contains/Distinct/In` 对引用类型支持 `Equals` 语义：新增 `EqualityComparer<T>` 委托重载（虚方法 `Equals` 受泛型统一表示限制） | **已完成** | `tests/conformance/linq_equality.zan` |
| B5 | 文档：`docs/STDLIB.md` 增补 LINQ 章节，明确与 C# 的差异清单（急切求值、无 IEnumerable） | **已完成** | STDLIB.md §3.6 |
| B6 | 惰性迭代器：`yield` 生成真正的惰性迭代器，LINQ 算子基于 IEnumerable 惰性链 | P2 | 无限序列 + Take 用例通过；现有急切用例不回归 |
| B7 | LINQ 查询语法（`from … where … select`）脱糖为方法链 | P2 | parser/binder 支持，conformance 覆盖 join/group |
| B8 | 语言便利特性（按需）：`using` 声明、`init`/`with`、`checked` | P2 | 逐项 SPEC + conformance |
| B9 | 编译器：修复泛型统一表示导致的 7 项 bug（泛型比较/相等、委托返回类型重载、Dictionary 泛型值、泛型累加器泄漏等），修复后可将 OrderByStr/ContainsStr 等并回 C# 同名重载 | P1 | `docs/bugs/generics-uniform-repr.md` 各条最小复现通过 |

> B1–B3 收益最大、改动集中在 stdlib，一周期内可完成；B6/B7 涉及编译器，
> 放在 A 的 P0/P1 之后。

## Workstream C：仓库工作流

| # | 事项 | 优先级 | 验收标准 |
|---|------|--------|----------|
| C1 | 分支管理规则落地：禁止创建长期分支，所有提交直接进 main；规则写入 AGENTS.md 硬规则 | P0（已完成） | AGENTS.md 含该规则；仓库仅存 main |
| C2 | 每次会话结束核查：`git branch -a` 只有 main、`git status` 无未收尾文件 | 持续 | 会话收尾清单 |

---

## 建议总执行顺序

1. C1（已完成）→ 2. A1–A3（P0 硬伤，B3 依赖 A2）→ 3. B1–B3（LINQ 核心补齐）
→ 4. A 的 P1 各项 + B4–B5 → 5. A 的 P2 与 B6–B8。
