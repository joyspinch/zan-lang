# 运行时数据布局与 ARC 内存模型契约

## 1. 内建 Dict 内存布局契约

- **8 字段数据布局**：
  `{i64 count, i64 capacity, i8** keys, i64* values, i64* index, i64 index_capacity, i64 indexed_count, ...}`。
  - `keys` 和 `values` 是保持插入顺序的并行缓冲区。枚举顺序和 ARC 内存释放顺序均依赖此顺序。
  - `index` 是开地址哈希索引表（存储 entry + 1，0 表示空槽位），由 `__zan_dict_find` 惰性重建。重建判据为 `indexed_count != count` 或 `index_capacity == 0`。
- **Remove 操作严格保插入序**：
  移除元素时严禁使用 swap-remove（将末项搬移至空位），否则会破坏遍历顺序并违反语义规范。正确做法是使用 memmove 整体向前搬移空位之上的所有条目，并将索引表整体置为失效（`indexed_count = 0`），由下一次查找触发重建。
- **尾部槽位清零**：
  在容量收缩或移除元素后，尾部槽位必须清零（memset 为 NULL/0），防止 ARC 遍历时访问到脏指针。

## 2. ARC 所有权与借用契约

- **DllImport 外部字符串借用规则**：
  从标准 C FFI（`DllImport`）返回的字符串默认为借用指针，由调用方或 C 库自身管理生命周期，Zan 运行时的 ARC 机制永不主动释放该指针，也不应进行危险越界下标。
- **内建函数与借用白名单**：
  内建函数（Builtin API）默认接管或生产受控对象。若内建函数返回受控或借用字符串，必须显式在借用白名单中登记。新增内建 API 必须同步检查所有权与释放行为，防止漏释放或重复释放。
- **临时所有权实参的生命周期回收（Lookup Builtins）**：
  在集合查找内建（如 `List.Contains`、`List.IndexOf`、`Dict.ContainsKey`、`d[key]` 读等）中，若实参是内联求值生成的临时对象（如字符串拼接或函数调用产生），IRGen 发射完成后必须通过 `emit_release_owned_call_temp` 释放该临时对象，避免每次查找泄露一次临时对象。
- **异常展开栈与实参临时注册（EH Unwind）**：
  在 setjmp/longjmp 异常展开体系中，向函数传参时，若实参属于 ARC 管理的临时对象，调用方必须在压栈前将其实参临时槽注册至每线程的 unwind 栈（`__zan_eh_tmp_push`）。当被调函数抛出异常时，展开流程将自动清理这些已注册的临时对象。调用成功返回后，pop 的次数必须与 push 次数严格保持一致，防止展开栈失衡。

## 3. 委托与闭包形态

- **委托的双形态表示**：
  委托具有两种物理形态：① 纯静态函数指针；② 携带环境的闭包（Tagged Closure，携带上下文环境和捕获变量）。
- **跨帧保活与标记检查**：
  当委托跨调用帧逃逸或传递给异步线程（如 `Thread.Start`）时，若为闭包结构，必须执行 ARC retain 操作并在入口点进行 `ZAN_CLOSURE_TAG` 判别，确保闭包上下文环境在目标线程执行期间生命周期完整。
- **数组字面量初始化循环**：
  数组字面量初始化包含引用类型元素时，循环赋值必须遵守 ARC retain 协议，保证各元素引用计数正确自增。

## 4. 新增平台能力的落点：编译器侧发 libc 调用，不新增运行时导出符号

- **为什么不能新增运行时导出符号**：各平台的预编译运行时目标是仓库里的产物，
  在非目标平台上**重建不了**。新增一个 `zan_xxx` 导出符号，就等于让所有交叉
  目标链接失败——所以新能力优先落在**编译器侧**（IRGen 直接发 libc 调用）或
  只用 libc 既有符号。
- **落地形态**：内建表（编译器的内建成员表）登记签名 + IRGen 直接发
  libc 调用。例如 TTY 判定：`Console.IsOutputRedirected()` /
  `Console.IsTerminal()` → 发 `_isatty(1)`（Windows）/ `isatty(1)`（POSIX）；
  wasm 没有终端概念 → 恒为"非终端"。**不新增运行时导出符号**。
- **stdlib 没有对应类也能用**：`Console` / `Math` 这类内建类型在 stdlib 里
  **没有类定义**，成员签名只在内建表里，编译器在发码阶段拦截这些调用。用户
  自己定义了同名类（`zan_type_defines` 命中）则以用户实现为准。
- **坑出处**：2026-10-20 之前 CLI 颜色策略只能看环境变量
  （`ZAN_CLI_COLOR` / `NO_COLOR` / `TERM=dumb`），判不出"输出被重定向到文件"，
  ANSI 序列被写进日志文件。有 isatty 之后判定顺序是
  `ZAN_CLI_COLOR` > `NO_COLOR` > `TERM=dumb` > stdout 不是终端。
- **TTY 判定的验证方式**：管道里跑（stdout=管道 → 已重定向）与
  `cmd /c start "" /wait prog.exe`（新控制台 → 是终端）**两侧都要验**，
  只测一侧等于没测——本机默认没有可见控制台，测"是终端"必须自己开一个。
