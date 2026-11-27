---
name: zan-compiler-internals
description: zanc 编译器内部（parser/checker/irgen/runtime/ABI）设计契约与底层不变量规范。修改 src/compiler/*、src/runtime/*、src/selfhost/*、交叉目标或 conformance 测试时使用。
---

# zanc 编译器内部设计契约与底层不变量

定位：`src/compiler/`、`src/runtime/` 与 `src/selfhost/` 开发和维护的架构规范与底层契约总索引。按改动子系统按需加载对应参考文档：

## 一、专题参考索引

| 改动领域 | 主契约参考 | 深度细则参考 |
|---|---|---|
| Parser / Binder / Checker 前端 | [frontend-contracts.md](references/frontend-contracts.md) | [frontend-binder.md](references/frontend-binder.md) |
| LLVM IRGen / 泛型 / 向量 / 优化 | [irgen-contracts.md](references/irgen-contracts.md) | [irgen-llvm.md](references/irgen-llvm.md) |
| Runtime / ARC / 发布与交叉链接 | [runtime-packaging.md](references/runtime-packaging.md) | [runtime-abi.md](references/runtime-abi.md) |
| 内建成员表 / await / 协程调度 | [builtins-async.md](references/builtins-async.md) | — |
| 自举编译器（`src/selfhost/`）总索引与门禁 | [selfhost-closure.md](references/selfhost-closure.md) | [selfhost-string.md](references/selfhost-string.md)、[selfhost-classid.md](references/selfhost-classid.md) |
| 自举专题（一）：闭包 / 异步 / 帧头（§12–§39） | [selfhost-closures-async.md](references/selfhost-closures-async.md) | — |
| 自举专题（二）：值类型 / 重载 / 单态化（§40–§75） | [selfhost-types-overloads.md](references/selfhost-types-overloads.md) | — |
| 自举专题（三）：泛型特化 / 委托 / 插值（§76–§114） | [selfhost-generics-lowering.md](references/selfhost-generics-lowering.md) | — |
| 自举专题（四）：宽度 / 所有权 / 容器（§115–§148） | [selfhost-ownership-containers.md](references/selfhost-ownership-containers.md) | — |
| 自举专题（五）：能力对齐复核轮次（§149–§164+） | [selfhost-parity-rounds.md](references/selfhost-parity-rounds.md) | — |

## 二、六大子系统铁律速览

1. **前端（Parser / Binder / Checker）**：类型位节点（`AST_TYPE_REF`）与表达式标识符严格隔离；限定名必须按命名空间祖先边界解析，禁止退化猜同名类；抽象方法须在检查器与虚表记账全路径拦截；可空守卫穿透 `lock` 等复合控制流。详情见 [frontend-contracts.md](references/frontend-contracts.md) 与 [frontend-binder.md](references/frontend-binder.md)。
2. **代码生成（IRGen / LLVM）**：调用发射严格区分静态/实例接收者槽位；结构体/值类型严守指针与边界拷贝契约；泛型方法与类按具体实参闭合实例化，禁止回退擦除桩；优化与 EH helper 防止 `-O2` 组合爆炸。详情见 [irgen-contracts.md](references/irgen-contracts.md) 与 [irgen-llvm.md](references/irgen-llvm.md)。
3. **运行时与 ARC（Runtime / ABI）**：所有权（owned vs borrowed）在赋值、`as`/`is`、属性 setter、集合查找、接口调用与 EH unwind 路径同源判定，零泄漏且禁止 double-free；`Dict` 移除严格保插入序并清零尾部槽位；Windows TLS 重定位与栈对象同步析构不可破坏。详情见 [runtime-packaging.md](references/runtime-packaging.md) 与 [runtime-abi.md](references/runtime-abi.md)。
4. **发布与交叉链接（Packaging & Cross-Linking）**：Windows 原生链接必须使用 bundled LLD；静态/发布模式剥离符号前须保留反射、HTML 事件处理器与入口根；驱动束产物须同步到对应包 `drivers/` 目录。详情见 [runtime-packaging.md](references/runtime-packaging.md)。
5. **内建成员表一致性**：内建类型（字符串、集合、数值、硬件/向量）在检查器成员表、IRGen 降级表与反射/LSP 元数据间必须单源一致，防止“能编译但运行期静默错值/崩溃”。详情见 [builtins-async.md](references/builtins-async.md)。
6. **await / 协程调度与自举闭环**：`await` 跨挂起存活值必须落帧并在 resume 块重算；自举编译器（`src/selfhost/`）遇到未实现能力一律 fail-closed，固定点仅证明自洽，行为对齐必须以差分门与 conformance 金标为准。详情见 [builtins-async.md](references/builtins-async.md) 与 [selfhost-closure.md](references/selfhost-closure.md)。
