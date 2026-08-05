# 排障方法论：从症状到根因

> 这份文档写的是**思路**，不是命令清单。核心一句话：
> **先分层定位，再最小复现，再修根因，再补回归。** 任何跳过其中一步的"修好了"都不算。

## 0. 先分清是谁的错

| 症状 | 先怀疑 |
| --- | --- |
| 报错信息里有源码位置和 `error:` | 编译器前端（词法/语法/绑定/检查） |
| 编过了但生成的程序行为不对 | IR 生成 / 运行时 |
| 编过了但链接/发布失败 | `main.c` 的打包发布路径、驱动 DLL 依赖 |
| 崩溃在启动瞬间、无任何输出 | 原生依赖（DLL 找不到）、运行时初始化 |
| 只在 IDE 里出错、命令行编译正常 | IDE 传参/工作目录/生成代码（`.zform` → `.g.zan`） |
| 只在并发跑测试时失败 | 资源争用（共享 `zanc.exe`、桌面/托盘/输入设备） |

**判断"是编译器缺陷还是代码写错"的标准问题**：
> 这段代码按语言规范应该合法吗？

- 应该合法却被拒 → 编译器**误报**，修编译器。
- 不该合法却被接受（或被静默降级成 0/null/空） → 编译器**漏报**，加诊断。
- 不该合法且被正确拒绝 → 代码的错，改代码；但要问一句"错误信息够不够指向问题"，
  不够就顺手改进诊断文本。

**禁止**：为了让编译通过而换一种写法绕过缺陷。绕过之后，缺陷会在标准库和用户项目里
反复出现，而且下一个人看到那段"怪写法"完全不知道为什么。

## 1. 缩小到最小复现

在 `_scratch/` 里写一个尽量小的 `.zan` 探针，直接用编译器跑：

```powershell
build\zanc.exe --stdlib-path stdlib _scratch\probe.zan -o _scratch\probe.exe
_scratch\probe.exe
```

削减策略（每步都要重新确认症状还在）：

1. 去掉与症状无关的成员/语句。
2. 把标准库调用换成同形状的自定义类（判断问题在标准库还是编译器）。
3. 把泛型/异步/继承等"复杂特性"逐个拿掉，看症状什么时候消失——**消失的那一步就是嫌疑点**。
4. 症状消失得太早，说明多个因素叠加：分别保留，做二分。

一个 5~20 行的探针，值 20 分钟的猜测。

## 2. 定位到编译器的哪一层

- 有诊断信息：**grep 消息文本**。`zan_diag_emit(g->diag, DIAG_ERROR, ..., "…")` 的字符串
  就在源文件里，一搜即到发出点，顺着读它的判断条件。
- 没有诊断（静默行为不对）：找该表达式形态的处理函数。成员访问/索引在
  `irgen_expr.c`，方法调用/重载在 `irgen_call.c`，控制流在 `irgen_stmt.c`。
  在这些文件里搜 AST 节点类型（`AST_MEMBER_EXPR`、`AST_CALL_EXPR`、`AST_ASSIGN`…）。
- 想看编译器"以为"生成了什么：把探针编译成 IR/汇编、或加临时 `fprintf(stderr, ...)`
  在可疑分支上打点（临时打点用完必须删掉）。
- **看兄弟路径**：几乎所有 IR 生成的缺陷都有一个"已经写对了的对称分支"。
  读取出错就去看赋值分支，右值出错就去看左值分支，实例方法出错就去看静态方法。
  对称分支是现成的正确实现范本，也是最强的证据。

## 3. 真实案例：成员读取被静默折成 0

**症状**：`item.text`（字段其实叫 `message`）编译通过，运行时读出 0/崩溃。

**分层**：能编过 → 不是语法/检查层拦得住的；行为错 → IR 生成层。

**定位**：`irgen_expr.c` 的成员表达式处理最后有一个"兜底返回常量 0"的分支；
而**赋值侧**（`item.text = ...`）早就有 `'X' has no member 'Y'` 诊断 —— 对称分支存在，
只是读取侧漏了。

**根因**：读取路径在所有已知形态（字段、属性、方法组、泛型字段…）都不匹配时，
直接返回 `LLVMConstInt(..., 0)`，把"不存在的成员"当成了合法的 0。

**修法**：抽出共享的成员查找（沿基类链），在兜底之前判断"该类及其基类是否声明了这个
成员"，没有就 `zan_diag_emit(DIAG_ERROR, "'%.*s' has no member '%.*s'")`。
继承成员必须继续放行——所以查找要走 `base_type->sym` 链。

**回归**：`tests/diag/class_member_read.zan`（直接成员，应报错）+
`tests/diag/class_member_read_inherited.zan`（继承成员合法、缺失成员报错），
用 `tests/run_compile_error.cmake` 断言错误文本。

**加了诊断之后必须做的事**（这一步最容易被忽略）：**新诊断会把仓库里既有的真实错误
全炸出来**。这次炸出 4 处，全部是真 bug，且都必须**按语义**修，不能改回去骗过诊断：

| 位置 | 错误写法 | 正确写法 | 说明 |
| --- | --- | --- | --- |
| `stdlib/Gui/Widget/CodeBlock.zan` | `app.theme.surface` | `app.theme.bgSecondary` | Theme 没有 `surface` |
| `stdlib/Gui/Widget/Label.zan` | `app.theme.windowW` | `app.canvas.Width()` | 窗口宽度不在 Theme 上 |
| `stdlib/Gui/Component/DataTable/DataTable.Compute.zan` | `dispRows[i].level` | 经 `groups[gi].level` 查 | 层级在分组行上，不在显示行上 |
| `templates/server/server-iot/.../Routes.gen.zan` | `.Group("iot")` / `.Rank(9)` | `.Meta("Group","iot")` / `.Meta("Rank","9")` | `Route` 没有这两个方法，元数据走 `Meta` |

**教训**：静默降级（返回 0/null/空串）是最贵的"容错"。宁可编译期报错。

## 4. 运行时/崩溃类问题

- **启动即退出、退出码 0、无输出**：怀疑原生依赖。Windows 上典型是
  `由于找不到 SDL3.dll，无法继续执行代码`——它会弹**模态系统错误框**（进程看起来
  "还活着"就是在等这个框）。检查 exe 旁/`build\` 里的驱动 DLL，以及运行时是不是用
  `LoadLibrary` 动态加载（那时搜索路径是**工作目录**，不是 exe 目录）。
- **ARC 相关（重复释放/泄漏）**：`irgen_arc.c` + `tests/leakprobe`、`leak_*` 测试。
  泄漏用 `run_leakcheck.cmake` 那套断言，不要靠肉眼看。
- **async/协程**：`irgen_async.c`；先用 `tests/conformance/conf_async_*` 里最接近的
  用例做对照，改一行看行为差异。
- **崩栈但无信息**：IDE 的未处理异常会写到 `<exe 目录>/cache/ide_error.log`
  （`ZanIDE.CacheDir()`），先看它。

## 5. 修完必须做的三件事

1. **回归测试**：编译期错误进 `tests/diag/`，运行行为进 `tests/conformance/`，
   泄漏进 `leak_*`，GUI 进 gallery/`tests/gui`。注册到 `CMakeLists.txt` 对应的
   tier 标签里。
2. **分层验证**：见 [testing.md](testing.md)。改了编译器至少 `standard`，
   动了 GUI/标准库要额外编 gallery 和 IDE。
3. **清场**：`_scratch/` 里的探针可以留（它是证据），但仓库根、`stdlib/`、`src/`
   里不能留临时打点和试验文件。

## 6. 反模式清单

- ❌ 改测试让它通过。
- ❌ 用 `try/catch`、默认值、`if (x == null) return;` 掩盖不该发生的状态。
- ❌ 在标准库里为了绕编译器缺陷写"魔法写法"而不留注释和 issue。
- ❌ 只跑 smoke 就宣布编译器改动安全。
- ❌ 把并发跑测试导致的桌面资源争用当成功能缺陷（先单独重跑那一个用例再判断）。
- ❌ "能编过"就说功能完成——GUI 改动必须在真实窗口里看过。
