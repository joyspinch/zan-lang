# Zan 语言规范 (Zan Language Specification)

<p align="left">
  <a href="SPEC.md">English</a> | <b>简体中文</b>
</p>

> 本文档描述 Zan 语言的**当前实测语义**。所有声称的语言特性均在 `tests/conformance/` 具备对应的验证用例或通过编译探针复核。

---

## 1. 概述 (Overview)

Zan 是一门具有 **C# 语法风格**、基于 **LLVM 本地编译后端**、采用 **自动引用计数 (ARC)** 进行内存管理的静态类型强类型系统级编程语言。

**核心设计目标：**
- **亲切舒适的语法**：C# 现代语法，无繁琐生命周期标注，无生僻特殊符号。
- **极致本地性能**：零成本抽象，值类型栈上直接分配，LLVM 全套后端优化。
- **确定性内存安全**：基于 ARC 的即时确定性析构，配合 `weak` 弱引用打破循环引用，无悬挂指针，零 GC 停顿。
- **务实高效**：极简的原生 FFI（DllImport）、纯源码分发标准库、轻量化开发工具链。

---

## 2. 词法结构 (Lexical Structure)

### 2.1 源码编码
源文件必须是 UTF-8 编码，文件后缀名为 `.zan`。

### 2.2 注释
```csharp
// 单行注释

/* 多行
   块注释 */

/// XML 文档注释（供 zandoc 提取）
```

### 2.3 标识符
大小写敏感，支持 Unicode 标识符字符。正则规则：`[a-zA-Z_][a-zA-Z0-9_]*`。

### 2.4 关键字
```
abstract   as         async      await      base       bool
break      byte       case       catch      char       checked
class      const      continue   decimal    default    delegate
do         double     else       enum       extern     false
finally    fixed      float      for        foreach    get
goto       if         in         int        interface  internal
is         let        lock       long       namespace  new
nint       null       object     operator   out        override
private    protected  public     readonly   ref        return
sbyte      sealed     set        short      sizeof     static
string     struct     switch     this       throw      true
try        typeof     uint       ulong      unchecked  unsafe
ushort     using      var        virtual    void       weak
when       where      while
```
- `value` 为上下文关键字（仅在属性 `set` 访问器中表示传入值）。

### 2.5 字面量
- **整数字面量**：`42`（十进制）、`0xFF`（十六进制）、`0b1010`（二进制）、`1_000_000`（数字分隔符）。后缀 `L`/`l` 表示 64 位 long，`U`/`u` 表示无符号 uint/ulong。
- **浮点字面量**：`3.14` (double)、`3.14f` (float)、`1.0e10` (科学计数法)。
- **字符串字面量**：
  - 普通字符串：`"hello\nworld"`（支持转义 `\n \r \t \\ \" \' \0`）。
  - 逐字字符串：`@"C:\path\to\file"`。
  - 插值字符串：`$"Hello, {name}!"`。
- **字符字面量**：`'A'`，`char` 存储为 8 字节 Unicode 码点字（`sizeof(char) == 8`）。

---

## 3. 类型系统 (Type System)

### 3.1 类型分类
```
类型体系
├── 值类型 (Value Types - 栈上分配，值复制语义，零 ARC 开销)
│   ├── 原生基元类型: bool, byte, sbyte, short, ushort, int, uint, long, ulong, float, double, char, nint
│   ├── struct (用户定义结构体)
│   └── enum (枚举类型，基于整数)
│
├── 引用类型 (Reference Types - 堆上分配，ARC 自动引用计数管理)
│   ├── class (用户定义类)
│   ├── interface (接口抽象契约)
│   ├── delegate (委托函数指针 / 闭包记录)
│   └── array T[] (托管连续数组)
│
└── 特殊类型
    ├── string (不可变 UTF-8 字符串)
    ├── object (引用类型根基类)
    ├── void (无返回值)
    └── T? (值类型可空包装器)
```

### 3.2 局部类型推导 (`var`)
仅支持在带有显式初始值的局部变量声明中使用 `var`：
```csharp
var count = 10;                     // 推导为 int
var message = "Hello";              // 推导为 string
var list = new List<string>();      // 推导为 List<string>
```

### 3.3 可空类型 (`Nullable`)
值类型可通过 `?` 后缀声明为可空：
```csharp
int? score = null;
if (score.HasValue) {
    int val = score.Value;
}
int finalScore = score ?? 0;        // 空合并运算符
```
支持空条件成员访问运算符 `?.`。

### 3.4 泛型 (Generics)
- **类型参数约束**：`where T : struct`（值类型）、`where T : BaseClass`（基类约束）、`where T : IInterface`（接口约束）。
- **`[rule:SPEC-GEN-01]` 单态化与 `$__Canon` 双路径**：值类型（`int`/`double`/`bool`/`struct`/`enum`/`Nullable<T>`）、`string`、`Task<T>`/`ValueTask<T>`、`Binding<T>` 与重载了 `==`/`!=` 的类按具体类型实参单态化展开；普通引用类（无 `==`/`!=` 重载）、`object` 与接口在方法体不含无约束鸭子类型访问时共享 `$__Canon` 代码，并携带每实例化独立的 `__gctx` 运行期字典。*(锚定用例：`tests/conformance/generic_dual_path_matrix.zan`、`tests/conformance/generic_canon_gctx.zan`)*
- **`[rule:SPEC-GEN-02]` 具体类型身份与反射等价**：两条路径均保持精确类型身份，`typeof(T)`、`is T`、`as T`、`(T)x` 与 `TypeInfo` 反射（`Name`/`FieldCount`）在 `$__Canon` 下经 `__gctx` 解析为真实类型实参，不退化为擦除占位符。*(锚定用例：`tests/conformance/generic_dual_path_matrix.zan`、`tests/conformance/generic_canon_gctx.zan`)*
- **`[rule:SPEC-GEN-03]` 静态字段隔离与默认零值**：泛型静态字段（如 `Box<T>.Counter`）按具体实例化隔离存储（`Box<Alpha>.Counter` 与 `Box<Beta>.Counter` 互不别名）；未初始化的 `T` 字段与 `T[]` 元素自动零初始化（值类型为 `0`/`false`/零结构体，引用类型为 `null`；Zan 不提供 `default(T)` 表达式语法）。*(锚定用例：`tests/conformance/generic_dual_path_matrix.zan`、`tests/conformance/generic_statics.zan`)*
- **`[rule:SPEC-GEN-04]` 约束与虚分发**：实例化期校验 `struct`/基类/接口约束；虚方法体依赖 `T` 时按实例化生成专属虚表分发。*(锚定用例：`tests/conformance/generic_constraints.zan`、`tests/conformance/generic_virtual_dispatch.zan`)*

---

## 4. 声明与结构 (Declarations)

### 4.1 命名空间与引用
```csharp
namespace MyProject.Core; // 文件范围命名空间

using System;
using System.Collections.Generic;
```

### 4.2 结构体 (struct)
- 默认零值初始化，栈上分配，赋值进行值拷贝。
- 支持 `[StructLayout]` 属性声明 C 兼容内存布局。
- 不支持继承其他结构体，但可实现接口。

### 4.3 类 (class) 与 面向对象
- 单继承体系，支持实现多个接口。
- 支持虚方法（`virtual`）、重写（`override`）与密封（`sealed`）。
- 访问修饰符：`public`、`private`、`protected`、`internal`。
- 构造函数初始化支持 `this(...)` 与 `base(...)` 级联调用。

### 4.4 属性 (Properties) 与 索引器 (Indexers)
```csharp
public class Person {
    public string Name { get; set; } = "";
    public int Age { get; private set; }

    public string this[int index] {
        get => ...;
        set => ...;
    }
}
```

---

## 5. 控制流 (Control Flow)

全面支持标准的结构化控制语句：
- 分支：`if-else`、`switch-case`（支持常量、类型模式与 `when` 保护从句）。
- 循环：`for`、`foreach`（支持数组与实现迭代契约的集合）、`while`、`do-while`。
- 跳转：`break`、`continue`、`return`、`goto`。
- 异常保护：`try-catch-finally`、`throw`。
- 资源作用域：`using (var res = new Resource()) { ... }`（作用域结束自动调用 `Dispose()`）。
- 同步排他锁：`lock (obj) { ... }`。

---

## 6. 内存管理与确定性 ARC (Memory Management & ARC)

Zan 采用与 Swift 类似的确定性 **自动引用计数 (ARC)** 机制，不存在任何 GC 垃圾回收造成的毫秒级卡顿：
1. **即时释放**：对象的引用计数归零时，其析构逻辑与关联系统句柄立即在当前线程执行清理并释放内存。
2. **弱引用 (`weak`)**：使用 `weak` 修饰字段以打破闭包捕获或父子节点双向强引用导致的循环环路。
   **编译器找出声明出来的环（W2111）**：类经非 `weak` 字段直接（`Tree Owner`）或经集合/数组（`List<TreeNode> Nodes`）强持另一个类、链条又回到起点时告警，给出环路与 fix-it（在回指针类型前插入 `weak`，优先 `Parent`/`Owner` 这类名字）；全由集合组成的环只告警不给补丁。自引用（`Node Next`、`List<Node> Children`）不报——链表与树要真的闭环才泄漏，由 `--check-leaks` 退出报告兜底。接口、类型参数、委托不追踪；只检查用户代码。
   **事件订阅绑定订阅者生命周期**：`+=` 的目标以 `this` 为根（`Click`、`this.saveBtn.Click`、实例字段 `saveBtn.Click`、`items[i].Changed`）且右侧 lambda 用到 `this` 时，lambda **弱捕获** `this`——宿主→发布者→处理器→宿主的环不会形成，订阅者释放后处理器自动失效，写法不变、无需退订。目标是形参、局部量或静态成员时保持 C# 的强捕获（只靠事件存活的订阅者照常收到事件）；带返回类型的处理器、结构体方法里的 lambda、方法组订阅不受影响。
3. **确定性作用域释放**：配合 `using` 语句或局部变量离开作用域，保证系统资源即时回收。

#### 6.1 形式化 ARC 所有权与运行期语义 (S30)
- **`[rule:SPEC-ARC-01]` 参数所有权（借用与被调方持有）**：实参按借用传入，被调方将 ARC 托管值参 `retain` 入局部槽并在离开作用域时 `release`（只读不逃逸借用参数可消除）；`ref`/`out` 参数直接别名调用方存储，不额外增减计数。*(锚定用例：`tests/conformance/arc_local_alias_borrow.zan`、`tests/conformance/arc_lookup_owned_arg.zan`)*
- **`[rule:SPEC-ARC-02]` 返回值与临时量所有权**：返回 ARC 托管对象的函数向调用方移交 `+1` 持有引用；丢弃的返回值与表达式临时量在所属语句边界确定性释放。*(锚定用例：`tests/conformance/arc_chained_temp.zan`)*
- **`[rule:SPEC-ARC-03]` 闭包捕获所有权**：lambda 强捕获引用对象并将可变捕获局部变量装箱为 ARC 堆单元；目标以 `this` 为根的 `+=` 事件订阅弱捕获 `this`（S12），避免订阅成环。*(锚定用例：`tests/conformance/event_weak_this.zan`)*
- **`[rule:SPEC-ARC-04]` 引用计数原子性与不朽哨兵**：`retain` 先以原子 `monotonic` 读判定 `ZAN_RC_IMMORTAL` 哨兵（静态字面量跳过计数写），再执行 `atomicrmw add monotonic`；`release` 执行 `atomicrmw sub release`，计数 `1 -> 0` 时插入 `acquire` 栅栏再析构（非调试守卫且全局弱引用表为空时，`rc == 1` 走独占快路径直接析构）。*(锚定用例：`tests/conformance/arc_immortal_object_slot.zan`)*
- **`[rule:SPEC-ARC-05]` `weak` 置空时机、释放顺序与禁用析构器**：`rc == 0` 经 `acquire` 栅栏后，先由 `zan_rt_weak_destroy_begin` 原子置空所有指向该对象的弱引用（若并发弱读复活 `rc` 则中止销毁），**之后**再按字段声明顺序释放实例字段（`weak` 字段退注册、强字段递减）；线程局部延迟释放深度 `__zan_arc_rel_depth` 以迭代排水避免深层对象图栈溢出；编译期以 `E1016` 拒绝用户析构函数 `~X()`。*(锚定用例：`tests/conformance/weak_field_lifecycle.zan`、`tests/conformance/arc_deep_tree_drop.zan`、`tests/conformance/arc_deferred_stack.zan`、`tests/selfhost/reject/destructor_declaration.zan`)*
- **`[rule:SPEC-ARC-06]` 异常展开与 `using` 确定性释放**：抛异常展开栈帧时，已武装的局部变量、临时量、`finally` 与 `using` `Dispose()` 按词法逆序确定性执行。*(锚定用例：`tests/conformance/arc_eh_concat_throw.zan`、`tests/conformance/cs_b02_using.zan`)*
- **`[rule:SPEC-ARC-07]` 编译期环检与退出泄漏诊断**：用户代码类间强引用环触发编译期 `W2111` 告警并附 `weak` 补丁；运行期 `--check-leaks` 报告存活对象并追踪孤儿环路（`GC.CollectCycles` 仅作显式兜底 API，无后台 GC）。*(锚定用例：`tests/conformance/closure_cycle_warning.zan`、`tests/conformance/gc_cycle_detector.zan`)*

---

## 7. 原生互操作与底层能力 (FFI & Unsafe)

```csharp
using System.Runtime.InteropServices;

public static class NativeMethods {
    [DllImport("user32.dll")]
    public static extern int MessageBoxW(nint hWnd, string text, string caption, uint type);
}
```
- 直接免头文件绑定 C 动态库导出函数。
- 提供 `NativeMemory.Alloc`、`NativeMemory.Free` 供底层数据缓冲直接操纵裸内存。

---

## 8. 异步编程与并发反应堆 (Async / Await & Concurrency)

- 基于状态机协程帧的异步调度系统，`async` / `await` 语法与 C# 对齐（`ValueTask` / `ValueTask<T>` 降级为 `Task`）。
- 内建高效轻量级并发通道 `Channel<T>` 与多工分片 `AsyncRuntime`，支持在游戏主循环与服务端网络高并发场景下灵活调度。
- **`[rule:SPEC-ASYNC-01]` 协程帧字段生命周期与 ANF 挂起**：跨 `await` 存活的参数与局部变量驻留于协程堆帧；`await` 前的子表达式与计算接收者经 ANF 提升到帧槽以保求值顺序与 ARC 存活；装箱闭包单元在每次恢复后重新加载单元地址；返回值写入帧尾类型化 `RVAL` 槽。*(锚定用例：`tests/conformance/async_frame_storage.zan`、`tests/conformance/async_var_locals_across_await.zan`、`tests/conformance/async_boxed_local_release.zan`、`tests/conformance/async_receiver_temp.zan`)*
- **`[rule:SPEC-ASYNC-02]` `await` 前后持有关系与终结释放**：协程帧跨越全部挂起点持有捕获实参/局部槽，并在正常完成、异常失败或取消时释放所有已武装槽位；`await` 完成时将任务持有的 `+1` 结果移交给等待方。*(锚定用例：`tests/conformance/async_spawn_result_release.zan`、`tests/conformance/async_frame_storage.zan`)*
- **`[rule:SPEC-ASYNC-03]` 跨 `await` 异常传播与 `finally`**：每次恢复挂载异常处理器并在挂起时卸载；未捕获异常写入任务故障槽并在 `await` 处重抛；包裹 `await` 的 `try`/`catch`/`finally` 清理块在完成或重抛前确定性执行。*(锚定用例：`tests/conformance/async_await_exception_finally.zan`、`tests/conformance/async_throw_across_frames.zan`)*
- **`[rule:SPEC-ASYNC-04]` 取消语义**：协程帧取消标志在挂起/检查点以原子 `acquire` 读取，位于 `finally`/catch 清理区间内时暂缓退出以确保清理完成；`CancellationToken` 提供 `IsCancellationRequested`、`ThrowIfCancellationRequested()` 与多等待者非阻塞 `WaitHandle()` 协作取消。*(锚定用例：`tests/conformance/async_cancel.zan`、`tests/conformance/cancellation_token_lifecycle.zan`)*
- **`[rule:SPEC-PLAT-01]` 跨平台逐字节一致与能力缺失链接期失败**：`tests/cross_platform_matrix.txt` 中的全部标准用例（覆盖所有独立 SPEC 锚定用例与 IEEE-754/数值/LINQ 不变量）在 Windows / Linux / macOS 上产生逐字节一致的 LF 归一化标准输出（由 `scripts/run_cross_platform_matrix.py` 与 CI `cross-platform-parity` 汇合任务机检）；目标平台缺失原生 `[DllImport]` 或运行期能力时于链接期显式报错，绝不静默降级。*(锚定用例：`tests/conformance/generic_dual_path_matrix.zan`、`tests/conformance/double_format.zan`、`tests/conformance/float_nan_compare.zan`)*
