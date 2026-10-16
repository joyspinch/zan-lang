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
- **实现模型**：值类型单态化展开（Monomorphization，生成零成本独立专用代码），引用类型采用类型擦除配合虚表分发。

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
3. **确定性作用域释放**：配合 `using` 语句或局部变量离开作用域，保证系统资源即时回收。

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

- 基于 CPS 转换的无栈/有栈协程调度系统。
- 关键字 `async` / `await` 语法与 C# 完全对齐。
- 内建高效轻量级并发通道 `Channel<T>` 与多工分片 `AsyncRuntime`，支持在游戏主循环与服务端网络高并发场景下灵活调度。
