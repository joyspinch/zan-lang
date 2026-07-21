# Zan 标准库组件封装规范 (Stdlib Component Standards)

> 适用范围：`stdlib/` 下所有组件（System.*、Gui、Game、Platform）。
> 目标：开箱即用、库内消化潜在问题、高性能安全封装、保持灵活性、杜绝屎山。
> 本文与 `docs/CODING_STANDARDS.md`（编译器 C 代码规范）互补，约束 `.zan` 标准库层。

---

## 1. 三层架构（每个组件必须分层）

```
门面层 Facade   —— 用户默认接触的 API：零配置可用，链式可选配置
能力层 Core     —— 协议/算法实现：状态机、编解码、缓冲管理
原语层 Native   —— FFI / reactor / 系统调用绑定：仅供能力层使用
```

规则：
- 用户默认只需要门面层。示例必须体现"Workerman 式"体验：
  ```csharp
  HttpServer s = new HttpServer(8080);      // 一行可用
  s.OnRequest((req, resp) => { ... });
  await s.Start();
  ```
- 可选配置用链式方法（`.MaxConnections(1024).Timeout(30)`），全部有合理默认值。
- 能力层公开（供高级用户下钻），原语层的 `DllImport` 一律 `static` 私有，不外泄。
- 层间只允许向下依赖，禁止跨层调用。

## 2. 安全与正确性（库内消化，不让使用者踩坑）

1. **禁止字符串拼协议**：
   - JSON 必须走 `JsonValue`/`Json.Serialize`，禁止 StringBuilder 手拼（转义/嵌套一定会错）。
   - SQL 必须走参数绑定（`?`/`$n` 占位），禁止值内联；标识符（表/列名）白名单校验 `[A-Za-z_][A-Za-z0-9_]*` 并加引号。
   - 二进制协议走 Buffer 抽象，不拿 string 当字节数组做业务逻辑。
2. **统一错误模型**：失败必须可区分——抛异常（`try/catch` 已可用）或返回带 `IsError/LastError` 的结果对象；禁止静默返回 `""`/`0`/空集合冒充成功。
3. **资源确定性释放**：`calloc`/`free`、句柄 open/close 必须逐路径配对（含所有错误分支）；对外暴露的资源类提供 `Close()` 且析构器兜底。
4. **并发安全**：跨协程共享的状态用 `AsyncGate`/`Gate`/`AtomicInt` 保护；文档必须注明每个类型的线程/协程安全级别。
5. **真异步原则**：`XxxAsync` 必须真正挂起到 reactor（`await Gate.Park` / `Socket.*Ov`）；同步实现禁止套 Async 后缀冒充（现存的假异步包装要逐步清理）。

## 3. 性能

1. **流式优先**：大数据路径提供游标/流（`QueryReader`、`Stream`），禁止只有"全量物化"一种形态。
2. **缓冲复用**：热路径的 scratch buffer 复用（参考 RedisClient），不逐次 calloc。
3. **循环规范**：`Count`/`Length` 在循环外取一次存局部变量；逐字符处理禁用 `Substring(i,1)`（用 `s[i]` 字节访问）；排序禁用冒泡（用归并/插入混合）。
4. **语句/连接缓存**：网络与 DB 组件必须有池化 + prepared 缓存能力（可关闭）。
5. **批量 API**：凡逐条写入的组件必须配 `XxxMany`/`XxxBulk` 批量形态。

## 4. API 设计

1. 命名与 .NET 习惯对齐（`Open/Close/Read/Write/TryParse`），同一动词全库含义一致。
2. 每个公开成员必须有 `<summary>` 文档注释 + 顶部 Usage 示例。
3. 默认值哲学：90% 场景零参数可用；不确定的默认值宁可保守（安全/耐久优先）。
4. 破坏性/危险操作显式命名（`WhereRaw`、`UnsafeXxx`），并在注释中警告。
5. 平台差异在原语层用 `#if WINDOWS/#else` 消化，门面层 API 跨平台一致。

## 5. 反屎山守则

1. 单一职责：一个类一件事；超过 ~500 行先考虑拆层。
2. 重复三次的样板必须提取 helper（例：驱动里的 C 字符串拷贝 `CopyText`）。
3. 内部 helper 不进公共命名空间；实验代码进 `_scratch/`，不进 stdlib。
4. 修 bug 必须补 `tests/` 用例；新组件必须带 conformance 测试。
5. 禁止"文档先行但无实现"的空壳类（现存空壳要么补齐要么删除）。

## 6. 现存违规清单（改造 backlog）

| 组件 | 违规 | 处置 |
|---|---|---|
| Orm/QueryBuilder, Model | SQL 值内联拼接、标识符不校验 | 参数化改造（进行中） |
| Sqlite/MySql/Postgres/ODBC | 无参数绑定 | bind FFI / binary protocol / PQexecParams / SQLBindParameter |
| Orm/ModelRow.ToJson | 手拼 JSON，值含引号即产出非法 JSON | 改走 JsonValue |
| Threading/Channel | Send/Receive 不阻塞，假实现 | 基于 AsyncGate 重写 |
| Thread.SleepAsync / File.*Async / Model.*Async | 同步冒充异步 | 真异步或去 Async 后缀 |
| IO/File | 整读整写、无流式、错误静默、`int` 句柄 | Stream 家族 + 异常 |
| Socket 旧 `*Async` | select busy-wait | 迁移 AsyncSocket，标记废弃 |
| Enumerable.OrderBy | 冒泡 O(n²) | 归并排序 |
| 各驱动/File | `int` 承载指针 | 统一句柄封装（长期：语言级 nint） |
