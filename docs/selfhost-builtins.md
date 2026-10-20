# 自举编译器对 C 宿主内建表的能力矩阵

自举编译器（`src/selfhost/`）是语言语义的权威实现，C 宿主（`src/compiler/`）
只为引导 gen1 而存。宿主把一批 API 声明成**内建**（`src/compiler/builtin_api.c`
的 `builtin_types[]` / `members_*[]` 表），自举后端要么自己降级这些调用，要么
**报诊断拒绝**（fail-closed）——绝不静默返回 0。

本文记录自举目前能降到哪一步、还差哪一步，以及怎么重新测。

## 怎么测

```bash
# 1) 先编出 gen1（C 宿主编自举源）
cmake --build build --target zanrt_selfhost_diff_bundle
# 2) 扫一遍内建表：每个成员生成一个探针程序，只编译不运行
scripts\selfhost_builtin_sweep.ps1 -Gen1 build\selfhost_diff_gen1.exe
# 报告落在 build\selfhost_builtin_matrix.txt
```

脚本按内建表**自动生成**探针（参数类型从签名里取，值只要能过类型检查即可），
所以新增内建不用改脚本。分类看自举的诊断：

| 诊断 | 含义 |
|---|---|
| 无诊断 | 自举能降级这个成员 |
| `not supported by the self-hosted compiler yet` | **缺口**：自举还没实现 |
| `is a built-in type and cannot be used as a value` | 内建**属性**（如 `Console.Title`），自举不降级 |
| 其它 | 探针本身写错了（不是自举的缺口） |

⚠ **「无诊断」只代表"能编译"，不代表"行为对"**。`Console` 家族就是反例：矩阵里
`Write` / `WriteLine` / `PrintLine` / `Clear` / `ResetColor` / `ReadLine` 全都
"无诊断"，可自举当时把每个 `Console.*` 都当成"打印第一个实参"——`Clear()` 打印
`0`、`ReadLine()` 返回垃圾（取 `.Length` 直接访问冲突崩）、`WriteLine(true)`
打印 `1`、`WriteLine('x')` 打印 `120`。**行为一律由差分门断言**（同一程序两边
stdout 逐字相同）。

## 现状（2026-10-20）

已能降级：

- `Math`：`Abs` / `Max` / `Min` / `Pow` / `Sqrt` / `Floor` / `Ceiling` / `Round` /
  `Sin` / `Cos` / `Tan` / `Atan` / `Atan2` / `Asin` / `Acos`
- `string`：`Length` / `Substring` / `IndexOf` / `LastIndexOf` / `Contains` /
  `StartsWith` / `EndsWith` / `Replace` / `Trim` / `ToUpper` / `ToLower` /
  `Split` / `Equals` / `ToString`
- `String`：`Format` / `Join` / `IsNullOrEmpty` / `CompareOrdinal`
- `List<T>`：`Count` / `Reserve` / `Add` / `AddRange` / `Insert` / `RemoveAt` /
  `Clear` / `Contains` / `IndexOf` / `Reverse` / `ToArray`
- `Dictionary<K,V>`：`Count` / `Keys` / `Values` / `Add` / `Remove` / `Clear` /
  `ContainsKey`
- `StringBuilder`：`Append` / `AppendLine` / `Clear` / `Length` / `ToString`
- `File` / `Directory` / `Path` / `Environment` / `Convert` / `Console`（除颜色
  属性外）/ `BitOperations` / `NativeMemory`（41 个成员）
- 三个**编译器内建**（不是标准库方法，标准库自己就在用）：
  `String.CompareOrdinal` → `strcmp`、`string.ToBytes()` → `byte[]`、
  `byte[].ToStr(off,len)` → `string`

`Console` 家族是**逐个成员**降级的（照 C 宿主 `irgen_call.c` 的口径）：

| 成员 | 自举的降级 |
|---|---|
| `Write` / `WriteLine` / `PrintLine` | 按实参类型分流：string → `%s`；double → 运行时最短往返；bool → `true`/`false`；char → 它的 UTF-8 文本；ulong → `%llu`；其余 → `%lld` |
| `Clear` | `ESC[2J ESC[H` |
| `ResetColor` | `ESC[0m` |
| `ReadLine` | `fgets` + 把精确长度回写字符串头（换行留在值里） |
| `Read` | `getchar` |
| `ReadKey` | Windows `_getch`，其它平台 `getchar`（可选的 `intercept` 参数按宿主口径忽略） |
| `ForegroundColor` / `BackgroundColor` | 读＝模块级颜色槽（缺省前景 7 / 背景 0）；写＝更新槽（`color & 15`）并发出 `ESC[<code>m`（码表：`(idx<8?30:90)+4*b0+2*b1+b2`，背景再 +10） |
| `Title` | 只可写：发 `ESC]0;<title> BEL`（宿主没有 getter，读它在宿主那边就是编译错误） |

还差：

- `Task`：`Delay`（同步上下文里）、`Yield`、`WhenAll`、`WhenAny`、`IsDone`、
  `Cancel`、`IsCancellationRequested`、`JoinNew` / `JoinBind` / `JoinWait` /
  `JoinCancel`——这些要走协程帧与调度器，见 TASKS.md 的 A385。

### 任务句柄的**形状**（`Task` / `Task<T>`）

自举的检查器已经认识 `Task` 这个内建类型名，并按 C 宿主
`checker.c` 的 `task_run_result_type` 给 `Task.Run` / `Task.Spawn` 定型：

| 实参形态 | 句柄类型 | 理由 |
|---|---|---|
| `Task.Spawn(...)` | `Task`（无结果） | 脱钩帧：结果由完成路径丢弃 |
| `Task.Run(lambda/Action)` | `Task`（无结果） | action 帧：没有结果 |
| `Task.Run(<无结果调用>)` | `Task`（无结果） | 脱钩帧 |
| `Task.Run(<带类型的调用>)` | `Task<T>` | keep_result 帧：结果由用户侧释放 |

无结果句柄流进 `Task<T>` 的槽位（变量初始化、赋值、返回值、实参）在**编译期**
被拒，诊断与宿主**逐字一致**：

```
cannot convert 'Task' to 'Task<int>' in initializer: a task started from a
lambda/Action carries no result; declare 'Task' (not 'Task<T>') and wait with .Wait()
```

这条不是洁癖：action 帧由完成路径释放一次，读者再释放一次就是 double free，
运行期直接 abort（宿主 `checker.c` 的注释与
`tests/diag/task_run_action_result_type.zan` 是同一个理由）。

**Task 形状的 async 返回类型**（`async Task<T> Foo()`）带的是**结果**，不是
句柄：await 出来的值是**元素类型**。宿主在解析期就把声明的返回类型改写掉
（`parser.c` 的 `desugar_async_task_method`：`async Task<int>` → `int`、
`async Task` → `void`），自举在检查器与 IR 后端两处分别补了同一口径
（检查器用元素类型判「所有路径都返回值」，后端取元素类型作为 await 的结果
类型）。不拆包的后果很隐蔽：整数与字符串靠位往返**碰巧**对，double 结果落到
double 槽里就成了指针，clang 直接拒 IR。

**形状认识 ≠ 能降级**这条边界现在只剩剩下的成员：任务句柄族
（`Task.Run` / `Task.Spawn` / `t.Wait()` / `t.Result` / `t.IsCompleted` /
`Task.IsDone`）已经降级，差分门里 `task_run_action_handle` 是**通过**而不是
记账缺口（`tests/selfhost/diff_known_gaps.txt` 现在是空的）；还没降级的是
`Task.Cancel` / `Task.WhenAll` / `Task.WhenAny` / `Task.Join*` /
`Task.Yield` / `Task.IsCancellationRequested`，它们一律报
"not supported by the self-hosted compiler yet"（fail-closed，不给错答案）。

降级用的是**自举自己的帧布局**（句柄 = 帧指针，完成位/结果位各占一槽），
没有对齐宿主 A385 的帧头——差分门比的是 stdout 逐字，不是帧字节，所以这条
不算缺口。唯一已知的语义偏差：`Task.IsDone` 读到「还没完」时自举会让调度器
跑一次（自举没有工作线程池，否则 `while (Task.IsDone(h) == 0) { }` 会死
循环），宿主则直接查标志位。

> 上表说的是**自举的 IR 后端**（`irgen*.zan`，差分门考的就是它）。**ngen 后端**
> （`ngen*.zan`，直接发机器码）只降级了 `Console.Write` / `WriteLine` /
> `PrintLine` / `ReadLine`，其余成员**报诊断拒绝**（fail-closed，不给错答案），
> 它自己的回归门还没建。

## 为什么差分门是唯一权威

能力矩阵只测"能不能编译"。**行为**是否与宿主一致由差分门断言
（`tests/run_selfhost_diff.cmake`）：同一个程序，宿主编一遍、自举编一遍，
两边 stdout 逐字相同才算过。新降级的内建都要配一个 conformance 用例
（`tests/conformance/<name>.zan` + `.out`）并进 `tests/selfhost/diff_cases.txt`。

**诊断**的对齐由 reject 用例断言：`tests/selfhost/reject_cases.txt` 的一行
可以带三段（`名字|||自举子串|||宿主子串`），第三段非空时门禁还会拿 **C 宿主**
编同一个程序，要求**两侧用同一句诊断拒绝它**——光"都拒绝"不算对齐，措辞
和位置都得逐字一致（措辞见宿主 `binder.c` 的 `binder_resolve_type`）。
