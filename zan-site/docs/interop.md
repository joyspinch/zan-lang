# 原生互操作、ABI 与平台边界

当前实现说明，按仓库源码与测试核实于 **2026-11-11**。已安装 SDK 可能与此仓库状态不同。本文介绍调用原生库与处理原生内存时的约束；具体目标环境见[平台支持](platforms.md)。

原生函数声明需要同时匹配符号名、参数宽度、返回值、调用约定、结构体布局和所有权。一个声明能够通过语法检查，不代表它一定匹配外部库的 ABI。

## DllImport 与 extern

`extern` 声明没有 Zan 方法体的外部函数。`[DllImport]` 提供库与入口点信息，常见声明放在类型内部并使用 `static extern`。

下面是声明片段，对应 Windows `GetTickCount64` 的用法；它需要放入类型中，且只适用于对应平台。

```zan-fragment
[DllImport("kernel32.dll", EntryPoint = "GetTickCount64")]
static extern long GetTickCount();
```

库名与 `EntryPoint` 分别解决“从哪里取得符号”和“符号叫什么”。Zan 方法名可以与原生入口点不同。

`"crt"` 是仓库中用于 C 运行库及运行时桥接的特殊导入约定。它不是把任意第三方函数自动加入程序的机制；第三方库仍需正确链接或加载。

当前没有把 C 头文件直接转换成语言声明的 `[CImport]` 功能。应根据头文件逐项建立明确的声明，并保留对应版本的头文件或 ABI 依据。

## 整数、指针与字符串映射

| Zan 声明 | 应核对的原生含义 |
| --- | --- |
| `byte` / `sbyte` | 8 位无符号 / 有符号整数 |
| `ushort` / `short` | 16 位无符号 / 有符号整数 |
| `uint` / `int` | 32 位无符号 / 有符号整数 |
| `ulong` / `long` | 64 位无符号 / 有符号整数 |
| `float` / `double` | 32 位 / 64 位二进制浮点 |
| `nint` | 原生地址或句柄，具体边界按目标验证 |
| `string` 参数 | UTF-8、NUL 终止的字符指针约定 |
| `struct` | 布局与目标调用约定均匹配后才能按值传递 |
| `delegate` | 仅在表示与签名匹配时才能作为原生回调 |

C 的 `long`、`size_t`、`BOOL`、`wchar_t` 在不同平台上有不同宽度，不能只按名字对应 Zan 类型。例如 Windows API 的 `BOOL` 通常是 32 位整数，不等于 Zan 的单字节布尔存储。

Zan `char` 当前为 8 字节码点，并不是 C 的单字节 `char`。枚举当前也按 64 位整数降级，不能假定与 C 枚举自然兼容。

当前主编译器将 `nint` 映射为 64 位整数。它虽用于地址与句柄，也不能由名字推断所有 32 位目标上的传参形态；跨目标时应检查实际 ABI 降级。

`decimal` 有独立的 128 位表示，不是 C 的 `double`。原生库若使用不同十进制格式，应通过明确转换函数或序列化边界适配。

## 原生地址与 unsafe

Zan 没有供普通代码使用的 `T*` 指针类型。原生地址用 `nint` 表达，内存操作通过 `NativeMemory`、视图与明确的外部函数进行。

`unsafe { ... }` 与 `fixed (T name = expression) { ... }` 是合法语法。当前 `fixed` 主要建立作用域别名，不表示为移动式 GC 固定对象；Zan 使用 ARC。

这些语法不能自动验证地址，也不会为错误的长度、已释放缓冲区或不匹配的元素类型补上保护。对象仍由 ARC 管理，也不意味着任意原生访问都具有内存安全保证。

下面是方法体片段，API 形状由 `ffi_widths.zan` 覆盖。

```zan-fragment
nint address = NativeMemory.Alloc(4096);
Span<byte> bytes = new Span<byte>(address, 4096);
bytes[0] = 1;
bytes[4095] = 255;
NativeMemory.Free(address);
```

该片段展示分配和释放的对应关系。实际资源代码应把可能失败的操作放在适当的 `try` / `finally` 中；释放后所有别名与视图都失效。

## NativeMemory 与所有权

`NativeMemory` 提供 `Alloc`、`Free`、`Copy`、`Fill`、`Compare` 等底层操作。长度参数和偏移的单位应按真实签名检查，不要把元素数误当字节数。

原生分配不参与普通对象的自动释放。即使地址保存在结构体、类字段或集合里，也不会因为字段消失而自动调用原生释放函数。

跨库分配与释放还必须使用匹配的分配器。某库返回的内存可能只能通过该库的专用释放函数释放，不能一律交给 `NativeMemory.Free`。

外部 API 的返回值应明确属于以下哪种情况。

- 调用者拥有的新分配内存，需要匹配的释放操作。
- 原生库借出的地址，只在句柄或某次调用结束前有效。
- 指向调用者提供缓冲区的地址，寿命由调用者控制。
- 不可解引用的句柄，只能传给规定的后续 API。

## 结构体布局

普通结构体默认按字段声明顺序与自然对齐布局。自然对齐会引入填充，所以总大小不一定等于字段大小的简单求和。

例如一个 `byte` 后接 `int`，通常需要在两个字段之间加入填充。跨平台布局还与目标数据布局、字段类型及调用规则有关。

显式布局使用 `[StructLayout(LayoutKind.Explicit)]` 与 `[FieldOffset(n)]`。下面是声明片段，直接采用 `explicit_layout.zan` 的联合体形状。

```zan-fragment
[StructLayout(LayoutKind.Explicit)]
struct Word {
    [FieldOffset(0)] int All;
    [FieldOffset(0)] byte B0;
    [FieldOffset(1)] byte B1;
    [FieldOffset(2)] byte B2;
    [FieldOffset(3)] byte B3;
}
```

多个字段共享偏移可表达重叠存储。字段偏移须满足实现的对齐检查；使用这一形状解释数值字节时，也需要考虑目标端序。

不要把含类、字符串或其他受管理引用字段的结构体，当作可任意覆盖的 C 联合体。引用生命周期与原始字节重解释是不同契约。

`[StructLayout]` 只解决布局声明的一部分，不能替你证明原生库的打包规则、位字段或特殊 ABI 与之相同。

## sizeof 与 Marshal

`sizeof(T)` 获取当前 Zan 值类型内存表示的字节数，并在编译期求值。结构体、基元、枚举和可空值类型可使用；类、字符串、数组、委托等引用形态会被拒绝。

这里的结果不是对象图占用量，也不是字符串长度。`sizeof(char)` 反映 Zan 的码点表示，不能拿来推导 C 字符缓冲区大小。

`System.Marshal` 提供 `SizeOf<T>()`、`PtrToStructure<T>(address)` 与 `StructureToPtr<T>(value, address)` 等包装。

以下是方法体片段，要求已声明 `struct P { public int X; public long Y; }`，形状来自 `marshal_struct_roundtrip.zan`。

```zan-fragment
nint address = NativeMemory.Alloc(sizeof(P));
NativeMemory.Fill(address, 0, sizeof(P));
Marshal.StructureToPtr(new P { X = 91, Y = 92 }, address);
P value = Marshal.PtrToStructure<P>(address);
NativeMemory.Free(address);
```

这是相同布局之间的按值往返，不是自动理解任意 C 数据结构的通用序列化器。目标地址必须有效、足够大并满足对齐要求。

## 调用约定与结构体传参

Zan 原生互操作使用目标平台的 C 调用约定，Windows x64、System V AMD64 与 AArch64 的规则并不相同。

结构体按值传递和返回会按目标 ABI 分类。可能使用寄存器、寄存器组合、调用方副本或隐藏返回地址；不能用一个“大小小于某阈值就一定走寄存器”的跨平台规则概括全部情况。

Windows x64 与 SysV 的间接参数规则也不同。编译器已有专门降级路径，并会对部分无法稳定映射的外部签名给出诊断。

接入新原生库时，应同时检查原生端的大小、对齐与字段偏移，并通过实际函数往返验证。只在 Zan 端读回自己写入的内存，不能证明外部 ABI 已匹配。

## 字符串与缓冲区边界

传给原生函数的 `string` 按 UTF-8 字符指针约定使用。原生端若需要 UTF-16 或其他编码，应明确转换，不要把同一个地址强行当作宽字符串。

原生代码若要在调用返回后继续保存指针，需要约定复制、持有或调用者缓冲区的有效期。不能默认一个字符串参数已经转移了所有权。

外部函数返回 `string` 的路径不是无条件深复制。借用的原生字符指针应建模为 `nint` 并在有效期内明确复制，避免借用数据被误当成长期受管理字符串。

不带受管理字符串头的外部字符指针也没有普通受管理字符串相同的边界保障。处理二进制载荷时应使用缓冲区与显式长度；NUL 终止字符串协议会在零字节处结束。

## 动态加载与回调

当前 `System.Interop` 的实际入口包括 `Load(name)`、`Symbol(handle, name)`、`Unload(handle)`、`LastError()` 和 `LoadLibrarySafe(name)`。

`Load` 或 `Symbol` 失败返回 0。使用安全句柄包装时，可按其释放协议限定共享库寿命；使用原始句柄则由调用方安排卸载。

库句柄必须比它提供的函数地址和正在执行的调用活得更久。卸载后，之前取得的地址不能继续调用。

原生 64 位平台中，静态方法组与无捕获 lambda 可以表示为裸函数指针；实例方法组与捕获 lambda 可能是带标记的闭包记录，调用时还需要隐藏上下文。

因此，不能把任意委托值强转为地址后交给只接受 C 函数指针的库。应使用签名匹配的静态回调，或建立明确的原生 thunk 与上下文通道。

如果原生端保留回调到调用返回以后，还必须保持委托及其上下文存活，并在释放前解除注册。wasm 的函数表表示又与原生指针不同，应独立核对。

## 可变参数与条件编译

`[DllImport("crt", Variadic = true)]` 可声明 C 可变参数函数。声明列出固定前缀，其余参数进入变参部分，并按 C 规则做相应提升。

格式字符串必须与实际类型匹配。可变参数不提供普通类型化 API 同等的参数检查，尤其不要默认结构体或受管理对象可以直接放入变参尾部。

平台代码可使用 `#if`、`#elif`、`#else`、`#endif`。仓库使用的目标符号包括 `WINDOWS`、`WIN32`、`LINUX`、`MACOS`、`APPLE`、`ARM64`、`X86_64`、`RISCV64`、`WASM32`、`WASI`、`MUSL` 和 `ZAN`。

符号描述编译目标，而不是让一个已经编译的函数在运行时自动切换实现。平台条件应尽量集中在系统适配边界；可复用业务代码优先使用现有平台抽象。

编译目标存在与某个原生库可以链接是两个事实。系统 SDK、库文件、架构、部署路径和运行权限也要符合目标要求，见[工具链](tooling.md)和[平台支持](platforms.md)。

## 核实来源

- `docs/SPEC.md`：§6.5、§7，地址、外部声明与 Marshal 语言表面。
- `docs/ABI.md`：§1.1 的分层说明、§3.2、§6；目标设计中的对象头不能当成当前布局。
- `src/compiler/irgen/irgen_type_map.c`：数值、`nint`、枚举与十进制的实际映射。
- `stdlib/System/Interop/NativeMemory.zan`、`Interop.zan`：原生内存与动态库接口。
- `tests/conformance/ffi_widths.zan`、`ffi_nint_handle.zan`：地址与整数宽度。
- `tests/conformance/explicit_layout.zan`、`marshal_struct_roundtrip.zan`：显式布局与按值往返。
- `tests/conformance/lock_goto_unsafe_fixed.zan`、`dllimport_variadic.zan`：`unsafe`、`fixed` 和变参声明。
- `src/common/zan_abi.h`、`docs/ABI.md` §3.6：委托记录与原生回调表示。
- `docs/STDLIB.md` §4.4：目标条件编译符号。
