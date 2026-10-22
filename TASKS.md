# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：`[ ]` 未完成 · `[~]` 进行中 · `[-]` 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 `[x]` 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出
>
> 2026-10-23 全仓 C# 语义复审（八域 96+ 探针实测，探针在 `_scratch/csaudit/`）：
> 新登记 A507–A526；A482 更正（宿主同病）。各域"实测确认正确面"见探针文件与提交正文。
>
> 2026-10-22 平账：移出 48 条已闭账条目（A390/A393/A394/A395/A396/A397/A399/
> A400/A401/A402/A403/A404/A405/A406/A407/A413/A415/A416/A428/A431/A432/A437/
> A441/A442/A445/A447/A448/A449/A463/A473/A474/A475/A476/A477/A478/A479/A480/
> A481/A483/A484/A485/A488/A491/A492/A497/A500/A502/A505）。编号不复用，结论与验证见 git 历史。

## 未完成

### [ ] A507 运行期错误 fail-mode 全线偏离 C#：软失败继续执行、不可 catch
- 现象（2026-10-23 八域审计交叉证实，全部亲验复现）：
  - 数组/List/字符串越界**读**：stderr 一行 `runtime error: ...`（带 file:line:col，每位置一次）后**钳位继续**，返回槽 0 的值；越界**写确定性写入槽 0**（`a[9]=99` 落进 `a[0]`，其余元素不动）——静默数据破坏，exit=0。
  - 整数除零：软 guard 后**返回被除数**（5/0→5、7/0→7、5%0→0）继续执行。
  - null 接收者**字段访问**：guard 后读出 0 继续；null 接收者**方法调用则完全无 guard，调用成功返回**（连报告都没有，隔离实测）。
  - `null.Length`→0、`Substring(7)`→钳 0 返回整串。
  - 硬模式（`--strict-runtime`/`ZAN_RT_HARD=1`）exit=70，仍不可 catch；`--publish` 不启用 strict。
- 期望 C#：以上全部是可 catch 的异常（IndexOutOfRange/DivideByZero/NullReference/ArgumentOutOfRange），未捕获才非零退出。
- 影响：迁移 C# 代码的异常处理假设全部失效——软模式脏数据向下游扩散，硬模式无法补救；靠 stderr+退出码判错的 CI/脚本全部漏采。
- 根因落点：`src/compiler/irgen_generics.c` `emit_runtime_check`（bad 路径 br 回 cont_bb 继续）；null 方法调用无 guard 在 `irgen_call.c` ~2103；软/硬开关在 `rt_timer.c` `zan_rt_soft_is_hard`。
- 方向：先给"越界写、null 方法调用"这两条响亮失败；最终口径应对齐 C#（全部可 catch）。
- 探针：`_scratch/csaudit/{collections/{p03,p09},exceptions/{p08,p09,p10},numeric/p02,string/p06}`。
- 关联：A508（Dict 缺 key 连 soft error 都没有）、A526（TOOLING.md 文档失真）。

### [ ] A508 Dict 缺 key 读完全静默返回默认值（连 soft error 都没有）
- 现象：`d["missing"]` 返回 0，无 stderr、无 zan_crash.log、exit=0。C# 抛 KeyNotFoundException。越界读尚有一行 runtime error，这条连一行都没有。
- 根因落点：`src/compiler/irgen_expr.c` ~7571-7596（Dict 索引读取 miss 直接走零值路径）。TryGetValue/ContainsKey 是正确出口。
- 探针：`_scratch/csaudit/collections/p06_dict_missing.zan`、`exceptions/p11_dictmiss.zan`。

### [ ] A509 失败下转静默类型混淆（无 InvalidCastException）
- 现象：`(string)userObj`（object 装 User）得**空字符串**；`(User)"x"`（object 装 string）得**非 null 的 User 槽**（实为 string 指针）——两边都"成功"，exit=0。C# 抛 InvalidCastException（可 catch）。
- 根因落点：`src/compiler/irgen_expr.c` `emit_expr_cast_expr`（:10723）对引用→引用无运行时检查分支（:10850-10876 落到 `return val`）；`is` 已有 `emit_runtime_is_check`（:10894）可复用。
- 方向：引用下转补运行时类型检查，失败走响亮失败/异常路径。
- 探针：`_scratch/csaudit/types/p09_object_cast.zan`（F6–F10）。

### [ ] A510 整数表达式按 64 位计算、不按声明类型收窄（表达式级溢出不环绕）
- 现象：`int a=int.MaxValue; WriteLine(a+1)`→2147483648、`a*2`→4294967294、`0u-1u`→18446744073709551615、`long j=i+1`→2147483648、`-8>>>1`→9223372036854775804。C# 全按 32 位环绕。存回 int 变量才截断——传参/打印/加宽路径不收窄。
- 同根：`TryParse(...)` 调用表达式直接打印得 "1"，存 bool 变量再打印得 "true"（调用结果表达式未按声明类型定型）。
- 根因落点：irgen 整数运算一律 64 位寄存器、仅写回 int 槽时截断；表达式定型与收窄缺失。
- 附带：`>>>` 宿主支持但 SPEC §2.6 运算符表未声明（→A526）。
- 探针：`_scratch/csaudit/numeric/{p03,p04,p12}`。

### [ ] A511 decimal 三处语义缺口：除法截断、与 double 混算垃圾、WriteLine(decimal) 编译崩溃
- ① 除法恒 18 位截断：`1m/3m`→"0.333333333333333333"（C# 28 位且末位进位）、scale 恒 18（`10m/4m`→"2.500000000000000000"）；加/乘精确。
- ② decimal+double 混算被接受且产出垃圾：`0.5m+0.5`→"19768355002338416264795389952"、`0.1m+2.0`→"19807040628566225135759764102"（C# CS0019 拒绝）。`Decimal.FromDouble` 本身正确——坏的只是混合算术的隐式转换 lowering（checker.c:1280-1285 对赋值路径已有拒绝，算术实参路径放行）。
- ③ `Console.WriteLine(decimal)` 直接编译失败：LLVM 校验错——`%Decimal` 结构体传给 `zan_rt_print_int(i64)`（亲验复现）。C# 正常打印。
- 根因落点：① `src/runtime/rt_decimal.c` 除法 scale 策略；② checker 算术实参 decimal 提升 + irgen 转换 lowering；③ irgen 打印分派缺 decimal 分支。
- 探针：`_scratch/csaudit/numeric/{p07,p08,p13}`。

### [ ] A512 字符串解析失败/溢出静默返 0 继续执行
- `Convert.ToInt32("abc")`、`int.Parse("abc")`、`Convert.ToDouble("xyz")` 全返 0 继续；`"2147483648"` 溢出静默接受（还借 A510 打出 64 位值）。C#：FormatException/OverflowException。
- `int.TryParse` 存在且正常——失败语义应与它对齐或补异常通道。
- 根因落点：同 A507（runtime 无异常通道，Convert/Parse 失败分支返 0）。
- 探针：`_scratch/csaudit/numeric/p11_convert_fail.zan`。

### [ ] A513 舍入语义偏离：Math.Round 远离零、Convert.ToInt32(double) 截断
- `Math.Round(2.5)`=3、`Round(-2.5)`=-3、`Round(0.5)`=1（C# 银行家：2/-2/0）；无 MidpointRounding 重载。`Convert.ToInt32(3.9)`=3（C# 舍入=4）；`Decimal.ToInt64(2.9m)`=2（C# 3）。Floor/Ceiling 正确。
- 根因落点：`irgen_call.c` Math/Convert 分支舍入模式（C 运行时 away-from-zero）；stdlib `System/Decimal.zan` ToInt64。
- 探针：`_scratch/csaudit/numeric/{p09,p12}`。

### [ ] A514 三元类型合并两侧各选一遍：checker 有提升阶梯、irgen 按 then 臂定型
- 现象：`(t?1:2.0)/2`→**0**（整数除法截断）、`(t?1:2L)/2`→0；double 在前才对；赋值语境靠隐式转换掩盖（`double v=(t?1:2.0); v/2`=0.5）。C# 公共类型=double。
- 根因：checker 的 `merge_conditional_types`（checker.c:1212-1214）有数值提升阶梯，但 irgen 的 `expr_family`（irgen_expr_core.c:1076-1080）对 AST_CONDITIONAL **先取 then 臂 family**，未共用同一份合并——与重载分解同病（skill §10）。
- 探针：`_scratch/csaudit/operators/p07_ternary.zan`。

### [ ] A515 ?? 的右支总是求值（短路缺失）
- `nn ?? Side()`：返回值对但 Side() 被调用（C# 非 null 时 calls=0）。日志/计数/惰性初始化全受影响。
- 根因：`irgen_expr.c:4320-4329`（可空路径）与 :4132（主路径）两臂无条件求值后 `LLVMBuildSelect`——skill 二.2 明令禁止的形态（带副作用的死臂）。
- 方向：cond-br + phi 合流。
- 探针：`_scratch/csaudit/operators/p08_nullish.zan`。

### [ ] A516 构造顺序与 C# 分叉：基类构造先于派生字段初始化器
- 实测 `b-init / base-ctor / d-init / derived-ctor`；C# 是**派生字段初始化器最先**（d-init → b-init → base-ctor → derived-ctor）。直接后果：基类构造器里的虚调用读派生内联字段得默认值（实测 `derived:0`，C# `derived:42`）。静默错值。
- 根因落点：`src/compiler/irgen.c` 构造器发射顺序。
- 探针：`_scratch/csaudit/objectmodel/{p02_ctor_chain,p03_ctor_virtual}.zan`。

### [ ] A517 析构器 `~X()` 是死语法：解析/检查过、从不发射、从不调用
- `~Node()`/`~Solo()` 编译通过、运行无任何析构输出；IR 里 `__zan_release_*` 只逐字段 release，析构体内容不存在。依赖确定性清理的迁移代码静默失效。
- 根因链：parser.c:3639 解析 ✓ → checker.c:5022 检查 ✓ → **binder.c 无 `AST_DESTRUCTOR_DECL` 分支**（grep 全文件 0 命中）→ irgen 无处理。
- SPEC §4.4（L297-300、L532-539）宣称 "Destructor (called when refcount reaches 0)" 与实现不符。
- 方向：binder/irgen 补分支（refcount 归零调析构体再释放），或编译期拒绝 + SPEC 改口径——不能停在"静默吞掉"。
- 探针：`_scratch/csaudit/objectmodel/p10_cycle_leak.zan`。

### [ ] A518 泛型方法调用点不检查 where 约束
- `static T FirstItem<T>(List<T> xs) where T : Entity` 传 `List<int>` **静默编译运行通过**（隔离探针亲验）；方法体用到约束成员时报错落在方法体解析（措辞/位置误导）。类泛型 `new Repo<int>()` 有检查但**重复报两次**。
- 根因落点：checker.c `check_generic_constraints`（:776）只挂 new 表达式（:3194）/var 声明（:3716）/返回类型（:4686）三处，调用解析路径缺失。
- 探针：`_scratch/csaudit/types/{p12_compile_rejects,p14_method_constraint}.zan`。

### [ ] A519 for 循环变量逐迭代新建（C# 的 for 头变量是共享单变量）
- 实测 `for:0/1/2`（C# 3/3/3）、闭包内赋值 `10/11/12`（C# 13/23/33）；foreach 每迭代新建与 C#5+ 一致、普通局部捕获一致（plain:9）。比 C# 安全但属未声明差异，C# 经典"共享循环计数"模式迁移即错。
- 根因落点：irgen for 语句发射（每次迭代实例化循环变量）。
- 附带文档漂移：SPEC §4.7"只读捕获创建时复制值"与实测（按引用共享）不符。
- 探针：`_scratch/csaudit/objectmodel/p06_for_capture.zan`。

### [ ] A520 静态构造器启动时急切全量执行（C# 首次使用触发）
- 实测 static-init/static-ctor/**unused-ctor-RAN**（从未引用的类）全部在 main-start 之前按声明序跑完；C# 惰性触发、未用不触发。启动副作用顺序、静态构造器异常时机（启动 vs 首用）随之改变。
- 根因落点：irgen 静态初始化发射（全量前置）。
- 探针：`_scratch/csaudit/objectmodel/p09_static_init.zan`。

### [ ] A521 foreach 枚举中修改集合静默放行（无失效检测）
- List 枚举中 Add：跟随 Count 增长继续枚举（真实程序=死循环，实测 count=103/guard=101）；Dict 枚举中 Remove：**静默跳元素**（k1,k2,k3 逐个 Remove → 只访到 k1;k3）。C# 两类都抛 InvalidOperationException。
- 根因落点：`irgen_stmt.c` foreach 发射（索引式枚举器，无版本号检查）。
- 探针：`_scratch/csaudit/collections/p07_foreach.zan`。

### [ ] A522 C# 硬错误的三个构造 Zan 放行
- ① finally 里 return：C# CS0157；Zan 接受且语义=覆盖 try 返回值（override-result=2），无警告。
- ② switch 非空 case 隐式贯穿：C# CS0163；Zan 接受且语义=**隐式 break**（隔离探针亲验：case 1 不落入 case 2）——Go 风格，C# 直觉漏写 break 静默变行为。
- ③ `if (x = 1)`：C# CS0029 硬错；Zan 仅警告，运行 C 风格真假（x=1 进分支，亲验）。
- 方向：三条升级为错误；switch 贯穿若刻意保留 Go 语义，SPEC 必须声明。
- 探针：`_scratch/csaudit/exceptions/{p03_finally_override,p13_implicit_fallthrough}.zan`、`operators/p13_assign_cond.zan`。

### [ ] A523 渲染与诊断输出口径族（与 .NET 不一致的小偏差集合）
- bool 全路径小写 true/false（五条渲染路径内部一致；C# True/False——A441 闭账口径=统一，非 .NET 口径，协议/日志大小写敏感场景有实害）。
- `char.ToString()` 返回码点数字 "65"（C# "A"；WriteLine(char) 与拼接却正确——渲染路径互相不一致）。
- `-0.0` 打印丢符号（C# "-0"）；float 打印按 double 展开（0.1f+0.2f→"0.30000001192092896"，C# "0.3"）。
- 未捕获异常：exit=1（C# 0xE0434352）、消息打 **stdout** 且不含异常 Message（C# stderr+Message+栈）；`e.ToString()` 裸 Message；无 `Exception.StackTrace`（编译期拒绝）。
- 探针：`_scratch/csaudit/{string/p08,numeric/p05,exceptions/,operators/p00}`。

### [ ] A524 多字节字符串的索引与 Length 按 UTF-8 字节（SPEC 未声明口径）
- `"中文"[0]` 返回首字节 0xE4（渲染 ä，C# '中'）；`"中文".Length`=6（C# 2）。char 为 8 字节码点 SPEC §2.5 已声明，字符串索引/Length 的字节口径 §3.1/§8 未写。
- 迁移影响：按字符处理文本的 C# 代码（索引/Length/Split 边界）在非 ASCII 文本上全部偏移；GBK/UTF-8 编解码 API 已就位（Zan.Text）。
- 方向：先补 SPEC 口径声明（廉价）；C# 口径需引入 UTF-16 语义或 per-codepoint API——独立战线。
- 探针：`_scratch/csaudit/string/p07_index.zan`。

### [ ] A525 API 面小缺口族（编译期显性暴露，迁移时需改写）
- 无 `StringSplitOptions`（Split 单参）；无 char 静态类（IsDigit/ToUpper）；`double.ToString("F2")` 无格式串；`"a"+null` 字面量拒绝（string null 变量可拼）；无 operator++/--（自定义类型）；`new` 隐藏修饰符解析拒绝（隐藏语义本身正确）；委托无 multicast（`d+=h` 拒绝、诊断已指路 UiEvent/List 替代）；无 Array 静态类（Sort/CopyTo）；Stack/Queue/HashSet 的 Count 是方法非属性；object==string 按值比较（C# 引用比较，引用同一性不可表达）。
- foreach-over-Dict 枚举变量是键 K 而非 KeyValuePair（取值需再 `d[k]`）；Dict 保插入序（比 C# 强的保证，未声明）。
- 特点：全部编译期显性（除 object== 与 foreach-dict 形状），不产生静默错值；按需求频度逐个补。
- 探针：`_scratch/csaudit/{string,collections,objectmodel}/` 各探针。

### [ ] A526 文档与诊断文案失真五条（本轮审计顺带查实）
- SPEC L174"decimal 是保留关键字，尚无对应类型"已过期——宿主已实现 m 字面量/四则/比较/System.Decimal。
- SPEC §2.6 运算符表漏 `>>>`（宿主支持；移位宽度语义归 A510）。
- SPEC §4.7"只读捕获创建时复制值"与实测不符（按引用共享——见 A519）。
- TOOLING.md「Runtime diagnostics」称默认 trap+exit 70——实测默认软模式继续（见 A507）。
- A380 的拒绝文案指向不存在的 `.Wait()`（checker.c:1593-1594；句柄 API 无 Wait，实际出口=await WhenAll/JoinWait）。另 `--api` 对内建成员覆盖不全（`List.Remove` 查不到但存在），与 A443 同族。

### [ ] A495 八条 conformance 用例在 `--check-leaks` 下报泄漏——所有权欠账浮出

- 现象（2026-10-22 合并测试孪生档时浮出）：这八条用例在 `--check-leaks` 下报
  "memory leak detected"，对象在退出时仍可达——
  `closure_cycle_warning`（4 个）、`null_guard_order`（1 个，
  `conformance/null_guard_order.zan:10`）、`oss_storage`（27 个）、
  `designer_html`（5256 个，JsonValue / PropertyGrid / Designer 一片）、
  `designer_prepared_async`、`game_data_table`、
  `gui_declarative_html_modern_test`、`sse_sink_take_co`。
- **不是插桩带来的假阳性**：`--check-leaks` 单独编译也报同样的数，
  `--arc-guard` 单独编译不报（`oss_storage` 在 `--check-leaks` 单独编译下还会
  崩，0xC0000005；双插桩下不崩）。普通版（无插桩）输出与 golden 逐字节一致
  ——功能面是好的，漏的是所有权。
- 为什么现在才看见：旧世界里这八条只红在 `leakcheck_*` 孪生档上，而那条档属于
  full 档，是发布门控、没人常规跑（AGENTS.md 规则 8）。2026-10-22 把四个孪生
  档合成一条 `case_*`（一次插桩编译、一次运行同时给 conformance/泄漏/ARC 三
  个结论）后，泄漏结论进了 standard 档，这八条当场浮出来。
- 现状：这八条按仓库既有机制（孪生档的 `_known_leaky` + WILL_FAIL）登记成
  "预期泄漏"——泄漏报告必须出现，修好了就翻红提醒把它从名单里拿掉；golden
  走未插桩的普通版（泄漏报告打在 stdout 上，插桩版比不过 golden）。
- 方向：这是所有权欠账，可能一半在 stdlib 容器/组件（闭包环、JsonValue 与
  Dictionary 的字段持有、Designer 的对象图、SseSink 的缓冲），一半在代码生成
  侧漏了释放，要逐条看站哪边。`closure_cycle_warning` 是**用例故意造环**，
  它的正确归宿是"预期泄漏"而不是"修好"；其余七条该修。
- 关联：`array_local_free` 是另一类——它量的是进程内存，插桩自己的记账
  （几十 MB）把它测的东西搅了（普通版 "sync ok"，插桩版 "sync leaked 25492KB"）。
  它的 golden 也走普通版，泄漏结论照常在插桩版上判。
### [ ] A468 宿主与自举在「`await` 一个非协程操作数」上分叉

- 现象：宿主把 `await <非协程>` **降级成普通求值**
  （`src/compiler/irgen_expr.c:10350-10358`，注释写明"不是帧就原样返回"——
  普通值往往也是 i8 指针，当成帧去读会读出面元指针的垃圾值，而且不报错）；
  自举则**直接报错**：`'await' expects an async call in the self-hosted
  compiler -- this operand is not lowered as a coroutine frame`
  （`src/selfhost/irgen_async.zan:618-632`）。
- 这**不是**宿主缺陷（2026-10-22 复核确认）：判据是"**这个 await 目标需不需要
  真挂起**"，不是"外层方法是不是 async"。await 一个普通值属于**能同步降级**的
  那一类，原样求值就是正确语义；宿主的行为被 golden 用例钉住
  （`tests/conformance/async_await_sync_delegate.zan:23` await 一个**同步委托**，
  文件头注释明写"await 的目标分两类——异步目标与普通目标"）。判据的完整说明见
  `docs/ASYNC_CPS_DESIGN.md` 的 "Where `await` may appear (the criterion)"。
  缺的是自举侧的**表达能力**：自举目前只能降级 async 调用，碰到普通值没有
  对应的"原样求值"形态。
- 方向（择一）：
  1. 自举补上"await 一个普通值 → 原样求值"这条降级路径（与宿主一致，差分门
     才不需要给这类程序开缺口）；
  2. 或者把这条分歧写进 `tests/selfhost/diff_known_gaps.txt` 并注明理由——
     但差分门只比"能编且 stdout 一致"的用例，"宿主能编、自举不能编"这类要
     另开一个 reject 清单条目才看得见。
- 关联：A467（已闭账：宿主补上「await 写在 lambda 里」的诊断）、
  `docs/ASYNC_CPS_DESIGN.md` §"Where `await` may appear (the criterion)"。

### [ ] A464 运行期分配量统计缺失——托管分配没有可读的计数入口

- 现象（2026-10-21 落地 Zan.Bench 时撞到）：基准测试要报"这次操作分配了
  多少字节"（BenchmarkDotNet 的 `Allocated` 列）做不到。`Zan.Bench` 因此
  只能报时间，README 与路线图 §1.24 都写明了这个缺口。
- 根因（三条叠在一起）：
  1. 托管分配的**唯一入口**是编译器生成的 `zan_rt_alloc`
     （`src/compiler/irgen.c` 的 `emit_new_expr` / 装箱路径），它内部调
     `malloc`——想知道"分配了多少"，只能在编译器生成的那一处挂钩子。
  2. 运行时**有**统计能力：`src/runtime/rt_mem.c` 的 `zan_mem_get_stats`
     （slab 数 / slab 字节 / 小块分配次数）与 `__wrap_malloc/calloc/realloc`；
     但 **stdlib 完全没有暴露这组 API**，包层拿不到。
  3. 含 `--wrap=malloc` 的 `zanrt_mem.obj` **只在 `--fast-alloc` 程序里
     链接**（wrap 作用于整个 image，不能只对一部分 malloc 生效），普通程序
     链接的是别的分配实现——统计口径因此不可比、也不总是存在。
- 影响：任何"这次改动是不是多分配了内存"的判断都做不了。ARC 没有 GC，
  分配量比 .NET 更该被观测，缺这一维让基准只能回答"多快"、回答不了
  "占了多少"。
- 方向（择一或组合）：
  1. **编译器侧挂钩**：让 `zan_rt_alloc` 在编译产物里带一个可选的计数钩子，
     配一个**总是参与链接**的运行时对象提供线程局部计数（ARC 下按线程计数比
     全局计数更准，也避免热路径上的原子操作）。
  2. **链接策略侧调整**：让 `zanrt_mem` 总是参与链接并 wrap 掉 malloc，
     再把 `zan_mem_get_stats` 通过 stdlib 暴露出来。
- 关联：A454（真装箱：值类型 → `object` 的堆对象）；`Zan.Bench` 见
  `docs/ECOSYSTEM_ROADMAP.md` §1.24。

### [ ] A465 泛型不能"按类型参数造对象"——没有 `new()` 约束，工厂只能由调用方给

- 背景（2026-10-21 修 A462 时从缺陷转成能力缺口）：A462 的**静默 null** 已修
  （两个后端都在编译期拒绝 `new T()`，措辞 `cannot instantiate a type parameter
  'T'`），但语言仍然**不能**用类型参数造目标对象——想要"由类型参数造对象"，
  只能把工厂（`Func<T>`）交给调用方。`Zan.Mapping` 的 `MapProfile` **目标工厂
  必填**就是这个能力边界的结果，不是库的偏好。
- 现状：`class Maker<T> { T Make() { return new T(); } }` 是**编译期错误**
  （两个后端一致）；`T[] buf = new T[3];` 仍然合法——数组的元素类型在实例化
  时被替换，路径是通的。
- 方向（能力扩展，要动约束系统）：
  1. 给泛型类加**"必须有无参构造器"的约束**（C# 的 `new()` 约束）：约束满足
     时按真实构造器发射 `new T()`；
  2. 或者允许**约束式工厂**（`where T : new()` 之外的形态，例如把工厂作为
     类型参数的一部分传进来）。
- 前提：检查器 / 绑定器要先支持泛型约束（现状见
  `tests/conformance/generic_constraints.zan`），这是一条独立的能力线。
- 关联：A462（已闭账：静默 null → 编译期诊断）、A449 / A454（装箱）。

### [ ] A457 类型身份按**名字**比较——限定名退化后的裸名与被 mangle 的真类型对不上

- 现象（2026-10-21 修 A438 后编译 `examples/security_guard` 实测）：
  `card.OnMouseDown(onMouseDownCard)` 报
  `cannot convert 'Action' to 'System_Action' in argument 1 of 'OnMouseDown'`。
  最小复现（两文件，各自都写了 `using System;`，与跨文件导入无关）：
  ```zan
  // p1.zan
  using System;
  public class P1 { public static void Hook(Action a) { } }
  // p2.zan
  using System;
  using Gui;
  public class P2 {
      public static void Main() {
          Gui.Action x = () => { };
          P1.Hook(x);
      }
  }
  ```
  编译 `zanc p1.zan p2.zan --auto-stdlib --package-project .` → 同样报错。
- 根因：`Gui.Action` **并不存在**（Gui 命名空间下没有 `Action` 委托），限定名
  解析失败后退化成"取最后一段猜"（`nsresolve.c` 的 `resolve_ref` dotted 分支，
  见 `zan-compiler-internals` 第 6 条），裸名 `Action` 交给 binder 软匹配到
  `System.Action`；但真类型因为与 `System.Linq.Action` 同名而被 mangle 成
  `System_Action`。于是**同一个类型**在参数位是 `System_Action`、在实参位是
  `Action`，检查器按**名字**比类型（`checker.c` 的 `checker_type_equal` 走名字，
  不比符号）→ 误判成两种类型。
- 与 A438 的关系：**不是** A438 修复引入的——上面这个最小复现里两个文件都显式
  写了 `using System;`，与"跨文件导入隔离"无关；限定名退化与 mangle 后按名字
  比较这两条路径都没被 A438 的修复动过。A438 只让这类问题**更容易暴露**
  （裸名不再被别的文件的导入抢走后，剩下的名字对不上的问题就显形了）。
- 影响：`examples/security_guard` 这类用"不存在的限定名"写回调的地方直接编不过；
  该示例不在任何 ctest 门禁里，所以一直没被发现。
- 方向（择一）：
  1. **类型相等按符号比**：检查器比较两个类型时用符号（`sym`）而不是显示名，
     名字只是诊断用的字符串——这是根上的修法。
  2. **限定名退化时同步改名**：退化到裸名后，把类型引用重写成目标类型的
     mangle 名（`System_Action`），让两处名字一致。
  3. 示例侧改成 `System.Action`（不存在的限定名本来就不该写）——只治这一处，
     编译器仍然会骗人。
- 关联：A438（裸名与同名类族）。

### [ ] A455 自举后端没有命名空间/`using` 解析层——裸名按"第一个同名类型"命中

- 现象（2026-10-21 修 A438 时查实）：C 宿主 `src/compiler/nsresolve.c` 有完整的
  命名空间解析层（`resolve_ref` / `resolve_static_receiver`，按当前命名空间 →
  祖先命名空间 → 本文件 `using` → 歧义诊断的顺序解析裸名）；自举后端
  `src/selfhost/binder.zan:68` 的 `FindType(name)` 只是**按简单名线性扫一遍**
  `types` 列表，命中第一个就返回——既没有命名空间概念，也没有 `using` 概念。
- 影响：自举后端编译多包同名类（`Window`、`Marker` 这类）时，谁赢取决于
  **声明顺序**，不是导入关系；C 宿主修好的"按成员命中消歧 / 报歧义"在自举
  侧没有对应物，两后端对同一份源码可能给出不同绑定。
- 方向：自举侧补最小一层——按"声明所在命名空间 + 本文件 `using`"过滤候选，
  多命中时报与 C 宿主同款 `ambiguous type 'X'; qualify it with its namespace`。
  注意自举侧的文件模型与 C 宿主不同（`src/selfhost/parser.zan:1656` 的
  `ParseUsing` 已经把 using 存进 AST，但 binder 还没消费它）。
- ⚠ `src/selfhost/irgen_expr.zan` 此刻正被另一会话在途修改，本轮不碰；
  A438 的 C 宿主修复已单独落地（见提交正文），两后端行为要等差分门能看见
  这类用例后再对齐。
- 关联：A438（已修，结论在 git 历史）。

### [ ] A451 自举后端没有一元运算符重载派发——同一份源码两个后端语义不同

- 现象（2026-10-21 实测，用例 `tests/conformance/unary_plus_overload.zan`，
  自举 gen1 按差分门同款流程编+链+跑）：
  - C 宿主：输出 `6 / 8 / 10`——一元 `operator +` 被**派发**，重载把值 +1。
  - 自举后端：输出 `5 / 7 / 9`——重载**根本没被调用**，操作数原样返回。
- 根因：`src/selfhost/irgen_expr.zan:156-196` `GenUnary` 只处理 `++/--`、`!`、
  `~`、`-`（内建），`TK.Plus()` 落到末尾 `return v`（恒等）；自举后端**没有**
  一元重载派发这一层（C 宿主是 `src/compiler/irgen_expr.c:4352`
  `emit_unary_op_overload`，按形参个数 1 区分一元）。一元 `-` 打在类/结构体上
  更糟：`sub i64 0, <指针>` → 非法 IR。
- 影响：两后端对同一份源码给出不同结果；`operator_unary_overload`（A438 落的
  用例）与 `unary_plus_overload` 都没进差分门清单，正是因为它们会分叉——
  差分门目前**看不见**这个缺口。
- 方向：自举 `GenUnary` 增派发：操作数是类 / 结构体时查 static `op_add` /
  `op_sub` 且形参个数为 1 的重载，找到就 `call`，找不到就报与宿主同款诊断
  （`type 'X' does not define a unary '+' operator overload`），两者都不许
  静默恒等。
- ⚠ `src/selfhost/irgen_expr.zan` 此刻正被另一会话在途修改，本轮无法同一次
  提交落地，等窗口。

### [ ] A444 `Convert.ToString(object)` 仍按 C 字符串读——object 里装类实例时依然越界读

- 现象：A4xx 那条修复（`Convert.ToString` 按静态类型分流）把类 / 结构体 /
  数组 / 委托都改成 fail-closed 诊断，但 **`object` 只能放行**：
  - `object` 是带标签的引用，编译期看不出里面装的是字符串还是类实例；
  - `object` 上**连通用 `ToString()` 都调不通**——`obj.ToString()` 报
    "this call could not be resolved to any callable"（builtin 成员表里
    `object` 列了 ToString，但那条调用链没有可发射的实现）。
  - 放行的代价：object 里装字符串时结果正确（老行为），装**类实例**时
    仍然拿 strlen 去量对象指针 → 越界读 + 内存信息泄露（与 A4xx 修掉的那条
    同款）。仓库里 `packages/Zan.Data/src/System/Data/Redis/RedisClient.zan`
    的 `SerializeObject(object obj)` 兜底就是这条路径。
- 影响：越界读可达（Redis 序列化兜底）；另外 object 装类实例时输出无意义。
- 方向（两条，都是大改）：
  1. 给内建 `object` 一个**通用 ToString**（运行期按对象头的类型 id 分派到
     真正的 ToString 方法；字符串负载直接拷贝）——顺带解决 `obj.ToString()`
     现在调不通的问题；
  2. 或者把 `Convert.ToString` 正式声明进 stdlib（A443），按实参类型分派，
     `object` 重载走上面的通用 ToString。
- ⚠ 两条都跨后端（C 宿主 + 自举后端），且 1 还要动运行时（对象头类型 id）。
- 同族残留（2026-10-21 记）：`Convert.ToInt32/ToInt64/ToInt/ToDouble` 与
  `int.Parse` 系列的类型守卫同样**放行 `object`**——`object` 装类实例时，
  这些内建仍会把对象指针当 C 文本去 `strtoll`/`strtod` 解析，静默得到 0。
  与本条同源：object 没有通用 ToString / 值语义就没法正确分流。
### [ ] A443 `Convert.ToString` 是编译器自造的接口——公开面（stdlib / 内建成员表）里根本没有它

- 现象：
  - `stdlib/System/Convert.zan` 里**没有** `ToString`（只有 Base64 / Hex 那几个）；
  - `src/compiler/builtin_api.c:92-96` 的 `members_convert` 里也只有
    `ToDouble` / `ToInt32` / `ToInt64`，**没有 `ToString`**；
  - 但编译器接受 `Convert.ToString(x)`（`src/compiler/irgen_call.c:1878` 的
    内建特判），而且全仓有 4100 多处调用。
  - 后果：`zanc --emit-symbols` 导出的 IDE 符号索引里，`Convert` 只有三个成员
    （实测符号表原文：`M Convert ToDouble M 1 double ToDouble(string text)` …），
    **IDE 补全补不出 `Convert.ToString`**。
- 根因：检查器对「作为被调者的成员访问」不报「无此成员」
  （`src/compiler/checker.c:4024` 的 `in_call_callee` 分支把诊断吞了），于是
  irgen 的内建特判成了这条 API 的唯一"声明"。
- 方向：把 `Convert.ToString` 真正声明进 `stdlib/System/Convert.zan`
  （按实参类型分派：int / long / double / bool / string / object），
  让公开面住在 stdlib：
  - 检查器会找到真方法 → 类型定型与重载解析交给常规路径；
  - 编译器内建特判的 `!zan_type_defines(g, "Convert", "ToString")` 守卫会自动
    让路（该守卫已经在了）；
  - IDE 补全随之补齐；
  - bool 的渲染口径（A441）也能在 stdlib 里一次定死。
- 同族：**`Math` 也是编译器自造的**。`src/compiler/builtin_api.c:74-82` 的
  `members_math` 声明了 Abs/Max/Min/Pow/Sqrt/Round/Floor/Ceiling 等成员，
  但 `stdlib/` 下**根本没有 `Math.zan`**（`stdlib/System/` 里只有 Convert.zan，
  且它也没有 ToString/ToInt32/ToDouble）。所以 IDE 补全同样补不出 `Math.*`。
  治本时应把 `Math` 与 `Convert` 一起声明进 stdlib，让公开面住在 stdlib、
  类型定型与重载解析走常规路径，编译器内建特判随之退化为兜底。
- ⚠ 跨后端：自举后端 `src/selfhost/irgen_expr.zan:4036` 的 `Convert.ToString`
  特判是**无条件拦截**的（不像 Console 那样有 `LocalIdx("Console") < 0` 守卫），
  必须同时加上守卫，否则自举侧不会调用 stdlib 方法。两端要同一次落地。
### [ ] A380 协程帧的释放仍只在类型侧收口——运行时没有幂等保护

- 现状（A379 已闭）：`Task.Run(lambda)` 的句柄被检查器定型成"无结果任务"，
  把 lambda 句柄当 `Task<T>` 用去读 `.Result`/`.Wait()` 会在**编译期**被拒绝
  （`tests/diag/task_run_action_result_type.zan`），那条运行期 double free 路径
  因此不可达。**当前没有可从用户代码复现的触发路径**（5 种探针形状都试过）。
- **2026-10-20 复核更正（原登记的前提只对了一半）**：
  - **默认路径根本没有守卫**。`src/compiler/irgen.c:1128 zan_emit_frame_free`：
    只有 `g->external_async_executor`（即 `--async-workers`）才调运行时的
    `__zan_co_frame_free`（`src/runtime/rt_io.c:4164`，在 `#ifdef ZAN_CO_DRIVER`
    段里，用帧首 8 字节的 `CO_DEAD` 位做幂等守卫）；**默认的进程内协程驱动
    走的是裸 `free(frame)`**（同文件 1134 行），连守卫都没有。
  - 即便有守卫也不可靠：小对象分配器回收时把用户块首 8 字节写成 freelist
    指针（`src/runtime/rt_mem.c:448`），守卫读到的已是垃圾 → 二次释放直接
    abort（`double free of block`）。
  - **原方向 a（活协程表仲裁）与当前协议不符**：活协程表里**摘掉**帧的是协程
    自己的完成路径（`__zan_co_untrack` → `zan_co_live_del`），而**释放**帧的是
    读结果的一方（读完 `.Result` 后 `zan_emit_frame_free`）。"谁摘掉谁负责 free"
    会在读者读结果之前就把帧释放掉 → UAF。仲裁必须是**独立于活协程表的所有权
    凭据**，不能拿活协程表当仲裁器。
- 方向（二选一，都要求 **C 后端与自举后端同一次改动**）：
  - **a. 所有权句柄**：每个帧配一个稳定的句柄单元（帧指针 + released 标志），
    所有释放点走 `handle_release()`，天然 exactly-once。代价：每帧多一次分配，
    句柄本身要么泄漏、要么再加一层引用计数。
  - **b. 隔离的帧分配器**：帧由运行时分配，释放后进隔离区（地址不还给通用分配
    器）→ `CO_DEAD` 位不被覆盖 → 第二次释放读到 DEAD → 直接返回。代价：内存
    延迟回收。
  - **不要用"指针 → 已释放集合"**：地址复用后无法区分陈旧指针与新帧——要么跳过
    一次合法释放（泄漏），要么释放一个活帧（内存损坏）。指针身份本身不够。
- 连带：`__zan_co_isdone` 在帧已被完成路径释放后仍会读它的 DONE 字段（先查活
  协程表、再读帧字段，中间有窗口）——4 字节 UAF 读，通常不炸但语义不严。
- ⚠ 为什么不能只改 C 侧：自举后端发的是同一套协议
  （`src/selfhost/irgen_async.zan:667/673/680/686/1037/1195/1645/2028/2035`
  都是 `call void @__zan_co_frame_free`），帧布局也各写一份；固定点门比的是
  两个后端的产物，单改 C 侧会让 `selfhost_fixed_point` 直接不绿。另外
  `toolchain/*/zanrt_io.o`（各平台预编译运行时）要跟着重出，非本机平台
  重建不了。

### [ ] A381 SPEC 的"尚未实现"清单 7 项一项没动

`docs/SPEC.md:766-776` 明列、实测仍不可用的：

- `checked` / `unchecked` 关键字是 **no-op**（溢出检查恒关，环绕语义）
- lazy iterators：`yield` 物化成 list，没有 `IEnumerable<T>` 协议
- tagged union 枚举（代数数据类型）
- `params` 之后的可选实参组合未系统验证
- `$@"..."` 组合字符串、`"""` 原始字符串
- COW 集合：`List` 赋值是引用共享，修改即共享
- `[CImport]`、`project.zan` 项目文件、`zan` CLI（实际是 `zanc`）

- 方向：逐项单独立项实现，每项带 conformance 用例；不做半截实现。

### [ ] A382 编译器缺三件基础能力：诊断编号体系、告警级别开关、Marshal

实测（`src/compiler` 全量 grep 0 命中）：

- 无诊断编号：诊断只有自然语言文本，没有 `E001`/`W001` 这样的稳定编号——工具链
  无法按编号定位/抑制，也做不了 `--warn-as-error` 这类开关
- 无 `--warn-level`：没有统一的告警级别开关
- 无 `#pragma warning`：无法在源码里局部调级或禁用
- 无 `Marshal`（`DllImport` 之外的互操作面）
- 附带一条语义偏差：越界检查是有的（`src/compiler/irgen_generics.c:1313`
  数组/string/span），但 `checked`/`unchecked` 开关不了它——只有全局
  `runtime_checks` 标志能整体关掉，与 C#"checked 上下文才检查"对不上。

- 方向：诊断编号是**破坏性变更**（所有诊断文本与编号都要定版，且影响 golden
  诊断输出），先定编号规范与兼容策略；`--warn-level` 与 `#pragma warning`
  跟在编号之后做。

- **编号规范（2026-10-20 定版，照此实现）**：
  - 形态：`[E123]` / `[W123]`，**编号 + 级别前缀 + 3 位十进制**；`E` = 错误、
    `W` = 警告。编号在同一个前缀内**只增不减、注销后不复用**。
  - 编号粒度：**一条诊断一处**（同一个语义点只编一次号），不按"每个报错函数"
    编号——否则同一条语义换个函数就换号，工具无法按号抑制。
  - 分段：0001–0999 语法/词法，1000–1999 名字/绑定，2000–2999 类型与约束，
    3000–3999 代码生成与后端，4000–4999 互操作与运行时契约；告警沿用同一
    段位（`W` 前缀独立计数）。
  - 文本格式：`[E1234] 源文件(行,列): 原有诊断文本`——**原有文本一字不改**，
    编号只加前缀；这样工具按号匹配、人类读文本都不受影响。
  - 兼容策略：编号**只加不改**：已发布的诊断文本不改写，只在前面加编号；golden
    诊断文件（`tests/diag/*.out`）随编号一次性重生成并入库，之后编号变更
    必须走"新增编号"而不是"复用编号"。
  - 落地次序：① 编号规范 + 全量编号表（`docs/DIAGNOSTIC_CODES.md`）+ 诊断
    文本加前缀 + golden 重生成；② `--warn-level`（按级别/编号前缀过滤）；
    ③ `#pragma warning`（源码内局部调级/禁用）；④ `Marshal`（与编号无关，
    可并行）。
- ⚠ 跨后端约束：自举后端自己发诊断（`src/selfhost/*.zan`），编号表要**两个
  后端同一次落地**，否则固定点门/差分门会不绿；所以这条不能只改 C 侧。

### [ ] A383 图表引擎整代语义差：官方 470 个 option 键里 278 个（59%）从不读取

- 事实（`docs/archive/CHART_CODE_GAP_LEDGER.md`）：对照 ECharts 6.1 的 596 个
  `.ts` 源，官方 option 用到的 470 个配置键里，引擎**从不读取**的有 278 个（59%）。
- 表现：用户照官方示例写 option，键被静默丢弃——不报错、不告警，图形少一块也
  不知道少了哪一块。
- 方向：先补"未知 option 键"的告警（廉价，立刻让缺口可见），再按官方示例
  覆盖度逐个补实现；不要一次铺 278 个键。

### [ ] A377 legend 页 16「将星图谱」页脚被挤成 14 设备高、文字上半被裁

- 现象（`LEGEND_OFFLINE=1` 走查页 16，设备像素 @150%）：页脚那条 `row`
  （`dock 2`）只有 **h=14**，而它自己的内容偏好高是 **30**（`star-pager` 的
  `prefH=30`、两个 `Label` 的 `prefH=32`）→ 「已点亮：0 / 35 说明」与分页
  控件上半被裁掉。
- 量化：body 可用 **682**（y 200..882），页内容定高 = 卡阵/分栏 **611**
  （y 212..823）+ 顶距 **36** + 页脚带宽 **45** + 页脚高 **30** ≈ **723**
  → 超 41 设备，第一趟布局按序排 dock 1..4 时后面的页脚只能吃残值 14。
- 方向：审计 §5.4b「坑 1」给过定死改法——撑条别用 `Grow()`
  （`dock=5` 走第二趟、不参与占位），要定高 `VGap(h)` 或 `DockBottom()`；
  同时需按参考图 `将星图谱.png` 实测该页**顶距与页脚带**（审计只给了卡阵
  180×255+价格条 34、行节距 316、左分栏 13 格，没给顶距/页脚），别拿现在
  的 36/45/30 三个数当准。
- ⚠ **别靠改窗口高来给页脚腾地方**：`Spec.WindowH()=625`（= 内容 610 +
  标题栏 32 − 17）是**用户点名缩过**的（`Spec.zan:35-38`：「整个窗口高度
  应该可以缩小一点，元素整体上收」，让卡阵在主区上下留白 57 设备与左右 44
  相等）。窗口比参考图矮 21 逻辑像素是有意为之，本页要从**页内**（顶距/
  页脚带）收，并保持卡阵居中留白。
- 同类风险页（同形状：定高卡阵 + 底锚页脚）：7 城池争霸、8 传说塔、
  19 至尊殿堂。

### [ ] A378 legend 导航带对齐校验脚本与 Spec 常量不一致，门已失效

- `test_navigation_layout.py`（随 legend 独立项目搬出，现位于
  `D:/project/games/legend/client/tests/test_navigation_layout.py`）是手跑的
  对齐检查器（不参与 pytest 收集，需喂 `dump tree` 的 JSON），它硬编码的是旧模型：
  图标 **22×22**、瓦片高 **32**（导航 30 号 48）、inset 4、gap 4。
- 当前 Spec 是 `NavIconW=Px(19)`、`NavIconH=Px(21)`、`NavTileH=Px(33)`
  （外框 32.7）、`Spec.Px(6)` 间隙；实测 dump（nav-0 瓦片 142×49、图标
  28×31、inset 12 设备、格间隙 9 设备、nav-30 49×49 纯图标）与 Spec 一致
  → **任何当前构建下该脚本必失败**，标准实际上处于无人校验状态。
- 另有一处矛盾要一并定：审计 §「顶部导航」写瓦片高 **30.7**（逻辑），
  Spec 注释写外框 **32.7**，差 2 逻辑像素（3 设备），需按参考图逐像素
  实测瓦片外框高定哪套是准。
- 方向：先定准绳（参考图实测外框高），再把脚本改成**从 `Spec` 取参**并补
  导航 30 号齿轮格，最后拿真 dump 跑绿；不要为了让它变绿去改脚本里的数字。

### [ ] A439 `--emit-ir` 的 IR 在 generic CPU 下仍无法合法化——Vector256 降级路径要重做

- 这是 A388 的**治本部分**（A388 的"让 .ll 自述目标特性"那半已做，见下）。
- 现象：`zanc <file> --emit-ir` 落出的 IR 直接喂给 LLVM 自带的 `llc` 会在
  `DAGTypeLegalizer::SplitVectorOperand` 处 fatal error：
  `LLVM ERROR: Do not know how to split this operator's operand!`
  （停在哪一步：*Running pass 'X86 DAG->DAG Instruction Selection' on function
  '@MemoryExtensions_SequenceEqual'*）。复现：
  `zanc tests/conformance/task_spawn_indirect.zan --emit-ir --no-packages > t.ll`
  然后 `llc -filetype=obj t.ll`（确定性必崩）。
- **2026-10-20 实测更正（此前登记的根因只对了一半）**：
  - 触发条件不是"`<32 x i1>` 本身"这么窄，而是**CPU 特性**。同一份 IR：
    `llc -filetype=obj`（默认 generic CPU）崩；`llc -mattr=+avx2` /
    `-mcpu=haswell` / `-mcpu=znver2` / `-mcpu=skylake-avx512` **全部通过**。
  - 编译自己不受影响：`src/compiler/irgen_emit.c` 的 `zan_bind_target_layout`
    建 TargetMachine 时 CPU 取 `x86-64`、特性取
    `+sse3,+ssse3,+sse4.1,+sse4.2,+crc32,+aes,+avx,+avx2,+fma,+bmi`，后端拿到
    的是 AVX2 档，所以 zanc 产出的二进制是对的；坏的是**给外部工具消费的 .ll
    文本**。
  - 此前"正常构建靠 globaldce 删掉死的 stdlib 函数才不炸"的说法是错的：live
    函数（`Program_Mask`，`Vector256.Equals` + `ExtractMostSignificantBits`）
    单独抽出来喂 llc 同样崩，只是正常构建里它被 globaldce 顺手删掉了。
  - 不可合法化的不止比较一处：把 `icmp eq <32 x i8>` 换成常量后仍崩，说明
    `Vector256.Create` 的字节广播（`shufflevector <32 x i8> ..., zeroinitializer`
    + bitcast 到 `<4 x i64>` + extractelement）同样不可拆。**凡是 256 位向量被
    拆回标量的地方都中招**：generic CPU 下 X86 要把 256 位算子拆成 2×128 位，
    而函数里同时存在 `llvm.x86.avx2.pmovmskb` 这类 AVX2 内建函数 → 256 位类型
    被判为合法 → `<32 x i1>` 这类 i1 向量留在图里 → `SplitVectorOperand` 没有
    兜底路径 → 报错。
  - 试过但**不管用**的写法（别再试）：函数级属性
    `attributes #0 = { "target-cpu"=... "target-features"=... }`（LLVM 20 的 llc
    不拿它做合法化依据）、文本 IR 的 `module attributes`（语法不支持）、
    `!llvm.module.flags` 里的 `target-features`。
- ✅ 已做（A388 的低成本那半，2026-10-20）：`--emit-ir` 的产物**自述目标特性**
  ——IR 头部注释 `; zanc-target-cpu:` / `; zanc-target-features:`，stderr 同时
  打印对应的 `llc` 命令行。实测：按提示的
  `llc -mcpu=x86-64 -mattr=+sse3,...,+bmi -filetype=obj` **通过**（出 210KB
  目标文件），不带特性的 generic `llc` 仍崩。文档见 `docs/ZANC_CLI.md` §1.5。
  这让产物**可用**，但没让 IR 在 generic CPU 下合法化——治本仍是本条。
- 方向：**重做 Vector256 的降级路径**——不要把 `<32 x i8>` 拆回标量，广播、
  比较、掩码全在 `<4 x i64>` 上做（移位/掩码的 SWAR 逐字节比较，不产生任何
  i1 向量），让 IR 在 generic CPU 下也能合法化；触及
  `src/compiler/irgen_expr.c` 的向量整段 lowering，风险高。
  ⚠ 自举后端也有一份向量 lowering（`src/selfhost/irgen_expr.zan`），两个后端
  要同一次改，否则固定点门不绿。
- ⚠ 不要用"先 `opt -O2` 再 llc"绕过：`opt -O2` 后能过，是因为优化器把死函数与
  冗余结构一起干掉了，编译器自己产出的 IR 仍然过不了自己的后端。

### [ ] A433 宿主有 `Binding<T>` 双向绑定原语降解，自举没有——赋给 `Binding<T>` 槽位的值没被包成 Binding

- 现象（2026-10-20 实测，`objinit_ctor_field_overwrite.zan`）：自举**崩**
  （0xC0000005）。看起来像"对象初始化器覆盖 RC 字段"的锅，其实不是——
  把该用例里的对象初始化器去掉（`new Card()`），自举照样崩。
- 根因：`Binding<T>` 是**泛型类**，字段 `Binding<string> title` 的槽位在自举
  里是"一个指针"。宿主有一条专门的原语降解：往 `Binding<T>` 槽位赋值时，
  - 赋的是**字段左值** → 合成一个 **live 绑定**（getter/setter 委托指向那个
    字段，读走 getter、写回 setter）；
  - 赋的是**其它表达式** → 合成一个 **const 绑定**（`constVal` 存该值）。
  宿主实现见 `irgen_expr.c` 的 `type_is_binding` / `emit_binding_value` /
  `emit_binding_acc_delegate`；检查器侧 `checker.c` 允许把 `T` 直接赋给
  `Binding<T>` 槽位。
  自举没有这一层：`title = seed`（seed 是 string）把**字符串指针**直接写进
  那个槽位，随后 `title.Get()` 把它当 Binding 解引用 → 崩。
- 关联：A421（泛型类字段布局）——live 绑定要合成 `BindGet<T>`/`BindSet<T>`
  委托（泛型委托的签名特化），与泛型实例化模型是同一件事。
- 方向：先做 **const 绑定**（赋值时合成一个 `Binding<T>` 实例，`constVal` 存
  值），再做 live 绑定（合成委托）。
- 现状规避：自举产物里不要用 `Binding<T>` 字段。

### [~] A435 控件的 `Kind()` 与"kind 名单"三处副本要对得上

- 现象（2026-10-20 实测更正）：先前记的"十九个控件没有 `Kind()`"不是
  主要问题——那十九个（`BoxContent` / `ChoiceGroup` / `Column` /
  `ContextMenu` / `Ellipsis` / `FormBuilder` / `Layer` / `Menu` /
  `Message` / `Popover` / `Prompt` / `QrEncoder` / `Ribbon` / `Scrollbar` /
  `ScrollView` / `Split` / `Table` / `Tooltip` / `WidgetDelegates`）是
  设计器与代码内部件，设计稿里的 kind 由设计器模型给
  （`FormField.KindForType`），不依赖 `Control.Kind()`。真正对不上的是
  **声明了 `Kind()` 却在名单外**的控件：`ButtonGroup` / `Center` /
  `Field` / `IconView` / `ListItem` / `RadioButton` / `RichText` /
  `StyledText` / `Typography` / `Wizard` / `GraphView` / `BandGrid` /
  `PropertyGrid`。
- 后果：编译期限定名单（`GenForm`）不认这些 kind → 生成代码发裸名，
  用户项目的同名全局类压过 stdlib（探针实证）；反向也一样——注册表里有
  而编译期不认的 kind（`Row` / `Spacer` / `FormGroup` / `FormItem` /
  `Modal` / `Drawer`）同样发裸名。
- **已修**（2026-10-20）：两处名单补齐——12 个库控件 + 6 个容器件进编译期
  名单；`Field` 进不可实例化名单（它是设计器的字段记录，没有零参构造，
  `new FormField()` 编译不过）。门禁：`scripts/component_audit.py` 的 R1
  规则（注册表 ↔ 编译期名单 ↔ 控件 `Kind()` 三处对齐）。
- 剩余：运行期按 kind 重建仍不支持那 12 个库控件（注册表会把它们拖进
  每个宿主，不注册是刻意的）；代码搭的树导出设计稿后能生成代码，但
  `Serialize` / `Html` 克隆会丢掉它们——真需要时再逐个开注册。

### [~] A436 样式解析的旁路：实例方法已清零，只剩静态 helper

- 现象：控件里手写的 `Style.Of(app, ...)` 已迁到基类入口
  （`Control.Box` / `BoxState` / `ResolveStyleAs`）。
- **已修**（2026-10-20）：剩下的 20 处**实例方法**直调（列表行、树行、
  滚动条轨道、弹层、分割线、停靠面板、虚拟列表根面…）收进新基类入口
  `Control.BareBox(app, type, state)`——部件表面的"裸基线"外观：不带本
  控件的 Class、不带 id、不套行内覆盖（套上会串味：列表写成 `.primary`
  就把每一行都染成主色）。门禁：`component_audit.py` 的 R4 规则
  （实例方法不得直调 `Style.Of` / `Style.Full`）。
- 剩余（不算违规）：**静态 helper**（`Badge.WidthOf/Pill/Dot/CountAt`、
  `Layer.DrawToast/...`、`Tag.*`、`Typography.Heading`、`StyledText.FontPx`）
  ——它们没有 `this`，解析的是显式的（类型, 类）组合（类由调用方传入），
  行内样式与控件缓存本来就与它们无关。
- 关联：见 A429（排版组溢出）——那条是布局放不下，本条是样式解析
  入口不统一。

### [ ] A434 泛型类的类型参数字段在**擦除上下文**里读到的是内联值的头 8 字节

- 现象：`class Box<T> { T value; T Get() { return this.value; } }`，
  具体实例 `Box<Pos>`（Pos = 16 字节结构体）**按具体路径**存取已经正确
  （A421 已修，见 `tests/conformance/generic_value_type_field.zan`）；
  但从擦除上下文调用同一个方法（`static T Peek<T>(Box<T> b) { return b.Get(); }`
  → `Peek<Pos>(box)`）时走的是**擦除版** `Box_Get`：它把槽位的前 8 字节当指针
  返回，而槽位里躺着的是内联的结构体 → 调用方按 Pos 解引用 → 野指针。
- 根因：类型参数字段的槽位是**按值内联**（`generic_field_slot` 按已知实例的
  最大 ABI 尺寸加宽成 `[N x i64]`），而擦除形态的 T 只能表示成指针——同一个槽位
  在两条路径上的表示不一致。引用类型实参时两者恰好都是 8 字节，所以一直没炸。
- 边界：这是**擦除表示的固有限制**，不是布局 bug：擦除版方法拿不到类型实参，
  既不知道值类型该占多少字节，也无法接管一份拷贝（值在调用方栈上，存进字段
  会悬垂）。改成"值类型一律装箱"也只是把故障从"野指针"换成"悬垂指针"。
- 方向（择一）：
  1. **按实例动态分派**：对象头里已有类型描述符（`__zan_desc_*`），可按它
     查到具体实例的方法入口，擦除版转调具体版——等于给用户泛型类补一层
     vtable。
  2. **编译期拒绝**：擦除上下文里访问"类型参数字段"时报诊断（要求用户把
     接收者具体化，例如 `Peek<Pos>(Box<Pos> b)`）。判定要能区分"实参是引用
     类型"（现状可用）与"实参可能是值类型"。
  3. **明文化限制**：文档写清"类型参数字段不可在擦除上下文访问"，并给检查器
     一条提示级诊断（当前是静默读错值）。
- 关联：A421（同一族：泛型实例化模型与类型实参的来源）。
### [ ] A450 自举驱动的命令行是 C 宿主的子集——不收目录、也没有 `@响应文件`

- 现象（2026-10-21 给 C 宿主加目录输入时查实）：C 宿主 `zanc` 现在接受
  **目录**（递归展开 `*.zan`）与 `@响应文件`；自举的驱动 `src/selfhost/main.zan`
  只认 `zanc <output.ll> <input.zan> [more.zan ...]`——没有目录输入，没有
  `@file`，也没有 `--auto-stdlib` / `-o` 这些旗标。
- 影响：用自举编译器编大项目时，文件列表同样只能摊在命令行上，同样会撞
  Windows 命令行长度上限。本轮没有一并改自举侧：改 `src/selfhost/main.zan`
  要重编自举编译器并重跑整个差分门（`tests/selfhost/diff_cases.txt`，200+ 用例，
  每个一次 gen1 编译 + 链接 + 两遍运行），代价远超这项便利的价值。
- 方向：在自举驱动里补同样的目录展开（`Directory.GetFiles` /
  `Directory.GetDirectories` / `Directory.ExistsOnDisk` / `Directory.IsSymlink`
  都已在 stdlib 里就位）与 `@file` 展开，与 C 宿主对齐。
- ⚠ 本轮只改 C 宿主 → 两个后端的**命令行面**有差异（**程序输出**无差异，
  差分门仍绿）。自举侧与 C 宿主同改时要注意：目录展开的顺序必须与 C 宿主
  一致（按路径字典序），否则同一批文件的 decl 顺序会分叉。
- 相关：A446（`pkg_sweep` 的假绿与目录输入）。

### [ ] A453 类型参数按名字全局注册——同名 `T` 先到先得，跨文件行为可能不同

- 现象（2026-10-21 修 A448 时顺带查出，未修）：类型参数在 binder 里是**按名字、
  先到先得**注册进**根作用域**的（`binder.c` 的 `register_type_param_list`：
  `if (scope_find(b->current_scope, tp->ident.name)) continue;`）。于是：
  - 根作用域里 `"T"` 只有**一个**，是第一个声明的类级/方法级类型参数；后续所有
    `"T"` 都解析到它；
  - `bind_members` 虽然把方法级类型参数注册进**类的作用域**，但 `scope_find` 沿
    parent 链会先命中根作用域的同名 `"T"` → 方法级类型参数常常根本没被注册；
  - 所有替换都是**按名字**匹配（`subst_method_tp` / `zan_binder_subst_named`），
    名字撞了替换仍然生效，但**类型实参个数**一旦对不上，逐位合一就错位 →
    绑不上 → 退化成擦除版。
- 结构性后果：**文件解析顺序会改变结果**。所有源文件（主文件 + stdlib + 包）被
  合并进同一个 AST、同一次编译（`main.c` 把各文件的 decl 依次 push 进同一个
  `ast`），`register_type_params` 按合并后的 decl 顺序遍历 → "同名 T 先注册谁"
  取决于**哪个文件先被解析** → 同一份源码放主文件里 vs 放进包里，绑定结果可以
  不同。
- 影响：目前没有独立的复现用例（A448 的真因是 `arg_base` 口径，已修），但这是
  一颗定时炸弹：任何"两个类型参数同名"的代码（类级 `T` + 方法级 `T`，或两个类
  各有 `T`）都可能踩到跨文件行为差异。
- 2026-10-21 排查记录（**仍未复现**）：两个最直白的形态都实测**正确**，说明
  "按名字替换"这条兜底路径在常见形态下够用：
  1. 类级 `T` + 方法级同名 `T`（`Box<T>` 里的 `T Same<T>(T x)`）→ 实测方法级
     形参按方法自己的实参定型（`Same<string>("s")` 得到 `s`），没被类级实参
     顶掉；
  2. 方法级 `T` 出现在**泛型实参位置**（`public List<T> Same<T>(T x)`）→ 实测
     返回 `List<string>`，类级替换没有先把 `"T"` 改写掉。
  剩下没验的是"**类型实参个数对不上**"那条（逐位合一错位 → 退化成擦除版），
  以及**跨文件顺序**（同名 `T` 先注册谁取决于文件解析顺序）——要复现得把两个
  同名类型参数分到不同文件里对照。
- 方向：把**方法级类型参数注册进方法自己的作用域**，并让"同名但不同层"的类型
  参数**按层隔离**（类级 `T` 与方法级 `T` 不再互相顶掉）。要动绑定期所有类型
  解析（`binder_type_params_equiv`、`nsresolve.c`、`checker.c` 里按名字比较类型
  参数的几处都要同步核对），影响面大，单独一条做。
- 关联：A448（真因不在这里，但同族）、A418。

### [ ] A454 真装箱：值类型 → `object` 的堆对象（带类型描述符）

- 背景（2026-10-21 A449 的遗留）：A449 走的是**编译期诊断**路线——值类型装不进
  `object`，编译期就报错。要让"值类型进 object"成为**可用**能力，需要补装箱
  设施。
- 可行路径（已探明，未实施）：
  - `rt_alloc(16 + payload_size, site_id, site_name)` 分配带头部的堆对象，载荷写在
    头部之后；
  - 为该**值类型**注册一个 site（`reserve_arc_site` + 名字写进
    `__zan_site_names_tbl`）——`is T` 的运行期检查读对象头 `-8` 处的 site 字 →
    查名字表，**天然可用**；
  - ARC 回收走 `__zan_site_dtor_tbl`（标量无托管字段 → 空析构即可）；
  - 需要新增的只有"为值类型注册 site"+"写载荷"+"取回时按类型读载荷"三件事。
- 要动的面：运行期内存布局、ARC 析构表、反射 / `ToString` / 序列化对装箱值的
  处理。属于独立战线，不与编译器缺陷修复混提。
- 影响：没有它，`object` 只能装引用——`Zan.Messaging` 的"统一请求-响应 + 擦除
  结果"做不了，结果类型必须由泛型端点一路保留（`QueryEndpoint<T, TResult>`）。
- 关联：A449、A434。

### [ ] A472 设计稿根（body）不认 `data-dock`——body 直接子级的停靠全部失效

- 背景（2026-10-21 coreforge 底部建造栏悬在屏幕中间时查实）：CSS
  `display:block` 在引擎里映射成**块流排版**（`StyleDisplay()==3`，
  `packages/Zan.Gui/src/Gui/Core/Control.zan` 的 `ArrangeFlow`），而
  `dock` 是**停靠排版**（`display==0`）的机制，两者是互斥的两条排版路径。
  设计稿的根是 `Gui.Element`（tag `body`，UA 样式表给了 `body { display:
  block }`），于是：
  - body 直接子级的 `data-dock` **全部被忽略**：`dock=5` 的填充层只拿到
    内容自然高，`dock=2` 的底栏贴在那个小盒子的底——表现就是"工具条跑到
    屏幕中间"；
  - 只有 body 这一层是块流：设计稿里的 `<div>`（`Gui.Widget.Panel`，
    style=3 → CSS 类型选择器 `stack`）走停靠排版，它们的子级 dock 生效。
- 旁证：`Form.StyleDisplay()` 已被强制成 0，注释写明"确保内部 Dock.Fill
  占满客户区、Dock.Bottom 稳固贴底，**严禁退化为块流垂直塌陷**"——同一个
  陷阱在窗口层已修，设计稿根层没修。
- 波及面（未逐个核实）：`templates/gui/*` 的设计稿都在 body 直接子级上写
  `data-dock`（ToolStrip=1 / StatusBar=2 / Panel=5），按当前语义它们的
  StatusBar 同样贴不住窗口底；IDE 的 `src/ide_zan/src/IdeForm.html` 亦然。
- 不能一刀切改成停靠排版：`tests/gui/genform_contract.html` 这类设计稿的
  body 里有裸文本与行内元素，需要块流排版（`FlowEntries`）。
- 可行方向（待定，需评估影响面）：设计稿根按内容自动选排版（body 只有带
  dock 的元素子项、且无流内文本时走停靠排版）；或给设计稿根一个显式开关
  （如 `data-root-layout="dock"`）；或让块流排版容器也尊重显式声明了 dock
  的子项。
- 已落地的项目侧绕行（不算修复，只是把 coreforge 摆正）：`client/src/
  main.zan` 把 HUD 根改成 flex 列 + Overlay `flex-grow:1`。
- 关联：coreforge 外部项目 `D:/project/games/coreforge`（仓库外）。
### [ ] A486 自举不判「枚举目标」的通用可赋值性——`Color c = "x";` 在自举里编译通过

- 现象：宿主报
  `cannot convert 'string' to 'Color' in initializer: no implicit conversion`；
  自举不报（枚举目标在自举的 `CheckAssignable` 里直接 return）。
- 机制：A477 把枚举目标划到"用户类型"一类（宿主靠类层次判定，自举的类型模型
  没有层次），于是枚举目标**整类**不参与通用可赋值性判定——连明显不匹配的
  `string` / `bool` 值也放过了。
- 影响：自举对枚举槽比宿主宽；同一个程序两边判定分叉。
- 方向：枚举目标可以单独判**原始类型**的值（string / bool / 浮点等），不必建模
  类层次——但要避开宿主的一个例外：`Color c = 5.0m;`（decimal 值）宿主**不报**，
  所以判的时候要把 decimal 排除掉。
- 关联：A485（枚举目标在窄化那一档已经判了）、A477。

### [ ] A487 自举把 `decimal` 类型映射成 `double`——目标侧无法区分 `decimal` 与 `double`

- 现象：宿主有真正的 `decimal` 类型，与 `double` **互不隐式转换**
  （`double d = 5.0m` 报 *cannot convert*，`decimal d = 5.0` 也报）；
  自举的 `parser.zan` 把 `TK.Decimal()` 映射成 `"double"`，AST 里 `decimal`
  目标与 `double` 目标**是同一个东西**。
- 影响：A485 的 16 条漏报里有 12 条来自这里：目标侧一旦涉及 decimal，自举就
  分不清该按 `double` 规则（拒 decimal 值）还是按 `decimal` 规则（收 decimal 值）。
- 方向：要么让自举保留 `decimal` 这个类型名（parser 不再映射，irgen 侧仍按
  double 发射但类型名留着），要么明确接受"decimal 目标侧一律放行"这条边界。
  前者更彻底，但会牵动 irgen 的类型判定。
- 关联：A485（本次的漏报清单）、A463（诊断里类型名不渲染可空标志，同属
  "自举的类型名比宿主粗"这一类）。
- A445 的连带影响（2026-10-22 补记）：宿主把 `decimal` 降成 struct，所以
  `Math.Sqrt(2.5m)` 被宿主拒绝（"requires an int or floating-point argument"）、
  `Convert.ToInt32(2.5m)` 宿主给 0；自举因为把 decimal 折叠成 double，
  `Math.Sqrt(2.5m)` **照收**、`Convert.ToInt32(2.5m)` 给 2。A445 只在自举侧
  补了"按静态类型分流"的守卫，这两条 decimal 差异要等本条目把 decimal
  还原成独立类型才会消失。
### [ ] A482 「void 臂的三元表达式」两后端都降成非法 IR（`phi void`）——2026-10-23 更正：宿主同病

- 现象（2026-10-21 修 A481 时被差分门照出来，用例
  `tests/conformance/nullable_user_type.zan`）：宿主编译运行正常；自举 gen1
  **解析通过**（A481 修好之后），但生成的 IR 过不了 clang：

  ```
  build/selfhost_diff_subset/nullable_user_type.ll:1577:15: error: void type only allowed for function results
    1577 |   %t127 = phi void [ 0, %L1 ], [ 0, %L2 ]
  ```

- **2026-10-23 审计更正（原"宿主没这个问题"只对了一半）**：宿主对
  **void 方法臂**的语句位三元同样编译失败——`flag ? A() : B();`（A/B 是
  void 方法）在宿主报同一 `phi void` 非法 IR
  （`_scratch/csaudit/operators/p07b_void_ternary.zan` 实测，
  "Instruction has a name, but provides a void value!"）。当时宿主验证的
  形状是**赋值臂**（`flag?(n=1):(n=2);`，该形态宿主至今正确）——两臂是
  void 方法调用才是共同触发条件。本条从"自举侧缺陷"改为**两后端同病**。
- 根因：两后端的 irgen 降三元表达式时对 void 臂仍建结果 phi。宿主落点
  `src/compiler/irgen_expr.c` 三元合并；自举落点
  `src/selfhost/irgen_expr.zan` 同类分支。
- 影响：编不出「语句开头的三元且两臂是 void 调用」的程序——而这正是最
  常见的一种三元写法（`ok ? Log() : Log2();`）。
- 方向：两后端在两臂类型是 void 时**不建 phi**（直接按分支走），同一提交改。
- 验证：把 `tests/conformance/nullable_user_type.zan`（void 臂）加进
  `tests/selfhost/diff_cases.txt`，差分门跑它应 PASS。
- 关联：A481（自举侧同一条战线的前瞻洞）、A469 / A479（宿主侧解析）。

### [ ] A489 GPU 3D 片元着色器把光照写死成固定键光——没有材质与光源 uniform 通路

- 现象：`src/runtime/zan_game.c` 的 3D 片元着色器（四份：plain / color /
  inst / inst-color）里键光方向是**写死的常量**：
  `float lam = clamp(dot(normalize(v_normal), normalize(vec3(0.4, 0.8, 0.6))) * 0.5 + 0.5, 0.0, 1.0);`
  `float shade = 0.55 + 0.45 * lam;`
  再 `frag = vec4(tex.rgb * uColor.rgb * shade, tex.a * uColor.a);`。
  而 `zan_game_draw3d(surface_id, mesh_id, mvp, color, texture)`（同文件 589 行）
  的参数里**没有任何材质 / 光源 / uniform 槽位**。
- 影响：GPU 档的光照**不可配置**——场景里放几盏灯、材质金属度多少，画面都
  一样。`Zan.Game.Render3D` 只能在 CPU 侧算一遍着色当 tint 传进去，于是画面
  是"CPU 着色 × 固定键光"两套光照叠加。
- 方向：给 3D 管线补 uniform 通路（材质槽 + 有限盏灯的方向/位置/颜色/强度）。
  注意**实例化批次路径每实例只带 20 个 float**（mvp 16 + tint 4），光源只能
  走 uniform 数组或每批次一次的提交，塞不进实例属性。
- 关联：`Zan.Game.Render3D`（`docs/ECOSYSTEM_ROADMAP.md` §1.30）。
  `src/runtime/**` 在并发会话的在途区域，本轮未动。

### [ ] A490 3D 实例化批次依赖 GL 3.3 / GLES 3.1 的 core 特性——移动端可用性未实测

- 现象：3D 批次提交（`zan_game_draw3d_batch`）用 `gl->VertexAttribDivisor`
  与 `gl->DrawElementsInstanced`（`src/runtime/zan_game.c` 719 / 724 / 768 行）。
  这两个是 **GL 3.3 core / GLES 3.1 core** 的特性；GLES 3.0（同文件 168 行的
  `ZAN_GAME_GLSL_HEAD_GLES` 用的是 `#version 300 es`）只有 ANGLE 扩展
  `ANGLE_instanced_arrays` 才有等价物。单发路径（`zan_game_draw3d` →
  `glDrawElements`，同文件 634 行）**没有**这个限制。
- 影响：`Zan.Gui` 的 EGL/GLES 上下文路径是有的（`src/runtime/gui_gl_context.c`
  的 `zan_egl_ctx_t`），移动端**单发 3D** 可用；但 `Gui.MeshBatch` 的
  **实例化批次**在 GLES 3.0 设备上没有等价实现——而 `Zan.Game.Render3D` 的
  绘制层走的正是批次路径。
- 未实测：路线图 §3 表里"Android/OHOS 是 stub"这句到底是旧结论还是现状，
  本机没有设备/模拟器，**无法验证**。`SetRenderBackend(1)` 返回 0 的真实
  原因是"这台机器没有可用的 GL 上下文（远程桌面 / 虚拟机 / CI）"，见
  `packages/Zan.Gui/src/Gui/Core/App.zan` 的 `SetRenderBackend` 注释——
  不能据此判定移动端不可用。
- 方向：Android / OHOS 真机实测（记 GLES 版本），再决定是补
  `ANGLE_instanced_arrays` 扩展路径，还是把移动端批次降级成逐实例
  `DrawMesh3D`。
- 关联：`Zan.Game.Render3D`（`docs/ECOSYSTEM_ROADMAP.md` §1.30）。

### [ ] A493 发布的 hello world 有 84% 的 .text 是运行时——已改归档成员收口大半，win/android 交叉待工具链

- 现象（2026-10-22 重定体积预算时撞到）：`size_budget_publish_host` 实测
  `.text = 199,752`，而这份 hello world 只打印一行字。linux-x64 侧
  `.text = 189,074`——两侧同步，说明是**通用运行时载荷**，不是平台特有。
- 载荷构成（实测）：宿主运行时对象（`build/zanrt_*.obj`，CMake 现场编）的
  `.text` 合计 **167,488**——占 hello world 总 `.text` 的 **84%**；程序自身的
  代码只有 ~32 KB。所以要瘦的是**运行时**，不是生成的代码。
- 根因（2026-10-23 钉死）：**不是"函数全可达"，是"链接的是裸 .o 而不是归档"**。
  - `src/runtime/rt_timer.c` 末行是 `#include "rt_hw_accel.c"`（**文本包含**），
    而 rt_timer.c 里 **0 处 `zan_hw_*` 调用**。文本包含 ⇒ crypto 与定时器同 TU
    ⇒ 必然整体进 `.o` ⇒ 必然整体进每个产物。`rt_hw_accel.c` 单独编出来
    `.text` = **53,408**，占定时器对象 74,128 的 **72%**。
  - 关键对照（同一 TDM gcc / 同一 `--gc-sections`）：
    | 链接方式 | 结果 |
    |---|---|
    | crypto 作为**裸 .o** 直接进链接行 | `zan_hw_=63` 全留，`.text=156,640` |
    | crypto 打进**静态库** `libcrypto.a` | `zan_hw_=0`，`.text=103,232` |
    | 裸 .o + `--gc-sections` + `-ffunction-sections` | 仍 `zan_hw_=63` |
    → **裸 .o 进链接行 = 整体载入，section GC 对它无效**；只有归档成员才按
    "符号缺才拉"。这也推翻了"补 `-ffunction-sections`"那条处方——那个实验做在
    **裸 .o** 上，前提就不成立。`-fdata-sections` 更是**地雷**：1.4 MB 的
    `.bss` 静态表被搬进 `.data`，发布产物 **245,248 → 1,821,184 字节（×7.4）**。
    **这两个开关都别加。**
  - 交叉侧同因同解：交叉链接本来就带 `--gc-sections`（`ld.lld -static ... -s
    --gc-sections`），**带了照样剪不掉**，反证根因不是"缺 GC 开关"。
- 落地（2026-10-22 收口）：
  - 宿主：`rt_timer.c` 去掉文本包含；crypto 由 CMake 编成 `libzancrypto.a`
    （`zanrt_crypto` 规则），zanc 在 **13 处**链接点注入该归档（原生 / 交叉 /
    dylib / DLL / wasm 全覆盖）。
  - 交叉：5 份配方（`build_linux_rt.sh` / `build_macos_rt.sh` /
    `build_ohos_rt_zig.sh` / `build_win_rt.sh` / `build_cross_rt.cmd`）统一改成
    "编 timer + 编 crypto + 打归档"；linux/macos/ohos/wasm32 的 timer 与
    `libzancrypto.a` 已用 zig 0.15.1 重出并提交。
  - **顺序纪律**：归档必须排在 timer 对象**之后**进链接行——归档成员只为"仍然
    未定义"的符号解引用，timer 若已定义同名符号则成员不会被拉进来（不会重复
    定义）；反过来排则同一符号被定义两次。
  - 归档与 timer 对象必须**同一代**重出：旧的 timer（内含 crypto）+ 新的归档
    同链接会重复定义，新 timer + 旧归档则会缺符号。
- 实测收益：
  | 产物 | 改动前 .text | 改动后 .text | 差 |
  |---|---|---|---|
  | 宿主发布 hello（`--publish --no-icon`） | 199,752 | **146,344** | −53,408 |
  | 交叉 linux-x64 hello | 189,074 | **137,458** | −51,616 |
  - 按需拉取（**不剥符号**的链接实测：`--publish` 会剥掉符号表，数符号必须用
    未发布产物）：hello 的 `zan_hw_*`/`zan_aes_*` = **0**；用 crypto 的程序 =
    **95**（含 `zan_hw_sha256`）——归档成员按需拉取成立。macOS arm64/x64 与
    wasm32 同样成立（hello 0 / crypto 83~95）。
  - WSL 实跑交叉产物：hello 输出 `hi`；crypto 输出 SHA-256("abc") 标准向量
    `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`。
  - 回归：`ctest -R crypto` **14/14** 通过（含 determinism 孪生）；
    `ctest -R "timer|decimal|async_await|case_hello"` **32/32** 通过。
- 顺带修掉的三个真缺陷（都在本条落地时撞到，根因各自独立）：
  - `src/runtime/rt_crash.h`：musl 目标缺 `sigaltstack` / `SS_DISABLE` 声明
    （musl 把它们藏在 `_BSD_SOURCE/_XOPEN_SOURCE/_GNU_SOURCE` 后面），配方在
    现代 zig 下**根本编不过 `rt_timer.c`** —— 交叉对象因此长期停在旧代码
    （已提交的 linux timer 对象里连 altstack 代码都没有）。
  - `toolchain/linux-musl/` 缺 compiler-rt 内建（`__udivti3` / `__umodti3`）：
    128 位除法链不过，交叉 linux 任何用到 `System.Decimal` 的程序都断（已用
    zig 的 `lib/compiler_rt` 补出 `libgcc.a` 装进工具链）。
  - `toolchain/linux-*/` 缺 `zanrt_decimal.o`（decimal 运行时只补了 macOS/Android）。
- 剩余（未闭）：
  - **win-x64 / win-arm64 / android-\*** 的 timer 与归档待各自的工具链
    （MSYS2 clang / Android NDK）重出——本机没有这两个工具链，配方已改好，
    CI/持工具链的人跑一次即可。
  - 归档目前**只有一个成员**（整块 crypto）：用到任一符号仍拉进全部 63+ 个
    `zan_hw_*`——按特性再切见 A499。
- 关联：`docs/archive/embedded_runtime_assessment.md`（同一条，文档里"补
  `-ffunction-sections`"那句已被本次实测推翻，已一并更正）、A491（预算重新定
  基线的直接原因）、A499（归档按特性再切的后续）。

### [ ] A499 crypto 归档只有一个成员——用到任一族仍拉进全部 63+ 个 `zan_hw_*`

- 现象（2026-10-22 A493 落地时留的尾巴）：`libzancrypto.a` 现在**只有一个成员**
  （整块 `rt_hw_accel.c`）。归档机制本身按"符号缺才拉"生效，但粒度只到"整块
  crypto"：程序只要用到其中一个符号（如 `zan_hw_sha256`），整簇 **63+ 个**
  `zan_hw_*` / `zan_aes_*` 仍被拉进来（实测交叉产物 95 个符号）。
- 切分可行性已量：29 个外部入口，族分 = aes 26 / cpu 探测 23 / 其他 84 /
  pixel 4 / sm3-sm4 3 / sha 3 / base64 3 / rsa-x25519 3 / json 2 / ghash 2 /
  crc32 1；**族间仅 aes↔ghash 5 条交叉调用边**（core 依赖：aes→core 15 /
  sha 6 / sm 4 / crc 2 / ghash 2）⇒ 几乎正交，可以按族切成员。
- 方向：把 `rt_hw_accel.c` 按族拆成多个 .o 打进同一个归档（sha / aes /
  sm3-sm4 / rsa-x25519 / pixel / crc / base64 / json / cpu 探测），让"只算
  CRC32 的程序"不必背 AES。切分要保证同族内部互根的函数仍在同一成员里
  （`zan_hw_gate` 的函数指针分派表按族分组，别把 aes↔ghash 拆散）。
- 关联：A493（归档成员机制的落地，本条的直接来源）。

### [ ] A494 `BaseRepository<T>` 直接实例化会**静默**返回默认值——只有继承才能用

- 现象（2026-10-22 查 A491 时撞到）：`packages/Zan.Data/src/System/Data/Orm/BaseRepository.zan`
  是公开 API，`Orm/Models/ModelColumn.zan` 的用法注释也写着"使用
  `BaseRepository<T>` 泛型仓储"。但**直接实例化**它（`new BaseRepository<U>(zsql)`）
  时，编译通过、运行不报错，结果全是默认值：`Insert` 返回 0、`FindById` 返回
  null、`SelectAll` 返回 null（后续访问直接空引用崩）、`Count` 0、`Any` false、
  `DeleteById` 0。改成**继承**再用（`class UserRepo : BaseRepository<U>`）则
  完全正常：`insert=1` / `found=alice` / `all=1` / `count=1` / `any=true`。
- 根因：`BaseRepository<T>` 是**开放泛型模板**——编译器只在派生类里特化出
  真身（`zan_parser_specialize_generic_bases`），模板体本身只留**运行期转发
  桩**（见 `src/compiler/irgen_call.c` 的 `call_receiver_is_open_generic`）。
  而编译期 ORM 代码生成器只在**类型实参是具体实体类**时才改写
  `Insert<T>` / `Select<T>`；模板里的 `T` 是类型形参 → 改写不了 → 落到桩上
  → **默认值**。真身里 `T` 已换成具体实体 → 改写成功 → 一切正常。
  所以"接口该不该补 `Insert`/`Select`"这个问题本身是错的：**它们从来不是
  接口方法**，补接口只会多一份要同步的空契约。
- 影响：任何按文档"直接 new 一个仓储"的用户会拿到**静默的错误结果**
  （写入 0 行、查不到数据），且没有一行报错——比编译失败更危险。
- 方向（按代价排序）：
  ① **运行期响亮失败**：转发桩在"没有被特化"时抛异常，而不是返回默认值
    （落点：`src/compiler/irgen_call.c` 的桩发射 + `src/runtime/` 的桩实现）；
  ② **让 `BaseRepository<T>` 不能被直接实例化**（抽象类 / 私有构造），把
    "必须继承"这条契约写进类型系统；
  ③ 至少在包文档与 `ModelColumn.zan` 的用法注释里写明"必须继承使用"。
- 关联：A491（同一处代码的另一面：A491 问"接口要不要补方法"，答案是不要）；
  `packages/Zan.Data/src/System/Data/Orm/BaseRepository.zan`。

### [ ] A496 自举的 `ToCStr` 不认可空值——可空 bool 走拼接路径生成非法 IR

- 现象（2026-10-22 随 A492 修复时查实，自举后端 `src/selfhost/irgen_expr.zan`）：
  可空 bool（`bool?`）走**拼接**路径（`"x" + b`）时，自举生成的 IR 是
  `icmp ne i64 <装箱指针>, 0` —— 拿**装箱后的对象指针**当 i64 比较，
  IR 非法，编译死在 LLVM 校验上。
- 根因：自举的 `ToCStr` **没有可空分支**。`Console.WriteLine` 那条路自己拆箱，
  拼接这条路没有；于是可空值直接以「装好的对象」的形态进了取串逻辑。
- 为什么自举不 fail-closed：宿主侧这类错误会报诊断，自举侧直接生成非法 IR，
  死在后端校验而不是告诉程序员哪儿错了。
- 障碍：自举**没有真装箱原语**（A454）——拆箱要先能落地。当前只有「按类型
  分流」这一半可做。
- 方向：给自举的 `ToCStr` 补可空分支，判据与宿主一致（可空先拆箱再取串；
  拆不出来就报诊断，不生成非法 IR）。
- 关联：A492（同一族取串路径的宽度/类型判据）、A454（自举缺真装箱原语）。

### [ ] A498 宿主把 `bool` 当**有符号**整数用——`Math.Sqrt(true)` 得 NaN、`Convert.ToInt32(true)` 得 -1

- 现象（2026-10-22 随 A445 补自举侧守卫时实测，宿主 `build/zanc.exe`）：
  - `src/compiler/irgen_call.c:701` `math_arg_is_numeric` 判的是**降级后的
    LLVM 值类型**：`bool` 降成 `i1`，`LLVMIntegerTypeKind` → 算"数值"；
    紧接着 `math_arg_to_double` 对 i1 做 `LLVMBuildSIToFP`（**符号扩展**），
    `true` 被当成 -1 → `Math.Sqrt(true)` = `sqrt(-1.0)` = **NaN**。
  - `src/compiler/irgen_call.c:1974` `Convert.ToInt32/ToInt64/ToInt` 对整数
    实参做 `LLVMBuildSExt`（**符号扩展**），`i1` 的 1 被扩成 -1 →
    `Convert.ToInt32(true)` = **-1**。
  - C# 的口径：`Convert.ToInt32(true)` = 1（bool→0/1 是有定义的转换）；
    `Math.Sqrt(true)` 则是编译错误（bool 没有到 double 的隐式转换）。
- 影响：两条路径都把 `false`/`true` 当成 0/-1 或 0/NaN，静默给出与 C# 不同的
  值。**自举侧没有这个毛病**：`src/selfhost/irgen_expr.zan` 的
  `ConvertIsIntegral` 按 0/1 处理、`MathNumBadType` 也放行 bool，
  `Math.Sqrt(true)` = 1.0、`Convert.ToInt32(true)` = 1 —— 是宿主错、自举对，
  两侧在这里分叉。
- 方向（宿主侧修，两侧同步）：
  ① `Convert.*` 数值路径对 `i1` 改**零扩展**（`LLVMBuildZExt`）或显式
     bool→0/1，得到 1（贴 C#）；② `Math.*` 把 `i1` 逐出数值白名单，报
     "Math.Sqrt requires an int or floating-point argument"（也贴 C#）。
  宿主改 ① 之后自举不用动（自举本来就是 0/1）；改 ② 之后自举的
  `MathNumBadType` 要同步把 `bool` 逐出白名单。
- 关联：A445（自举侧的同款守卫已补齐）、A444（`object` 的同类白名单问题）。

### [~] A501 跨目标全量扫描：13 个目标的 console 与 GUI 交叉链接体检

- 起因（2026-10-22）：被问"跨平台交叉编译现在都是全绿的吗"。此前只逐个目标试过
  单个程序，没有**全目标 × 多特性**的扫描，于是漏掉的东西整整一批。
- 扫描装置（`_scratch/xsweep/`，一次性）：3 个 console 探针（hello / crypto /
  decimal）+ 1 个 GUI 探针（GUI + decimal）× 13~16 个目标，逐个记录链接返回码、
  `.text` 与 `zan_hw_*` 符号数。**探针必须按特性选**：只跑 console 探针的话，
  iOS（GUI-only）与 GUI 目标专属的注入路径一条都照不到。
- 扫描暴露并已修掉的缺陷（五个，各有独立根因）：
  - `src/compiler/main.c`：交叉路径复用 `rt_crypto_buf` 这块**共享缓冲区**，
    归档找不到时仍留着指向它的指针 → 链接器拿到一个不存在的交叉路径
    （android/ios 直接报错）。改为交叉分支**无条件赋值**
    （`zan_file_exists(...) ? buf : NULL`），原生分支加 `if (!cross_compiling)`。
  - **wasm32 的链接分支根本没注入 decimal 运行时**：别的目标都有
    `if (irgen.uses_decimal_runtime)` 注入 `zanrt_decimal.o`，wasm 没有 →
    任何用 `System.Decimal` 的 wasm 程序链接期报一串
    `undefined symbol: __zan_decimal_*`。已补注入 + 补对象
    （`toolchain/wasm32/zanrt_decimal.o`）。
  - `src/runtime/rt_decimal.{c,h}`：对外 ABI 用了 `size_t`（长度/容量），
    而编译器后端按 Zan 的 `long`（**恒 64 位**）发调用 → wasm32 上 `size_t` 是
    32 位，`wasm-ld` 报 *function signature mismatch*（链接过了，模块运行时坏）。
    改为定宽 `int64_t`（其余 `__zan_decimal_*` 早就是定宽的）。
  - **ohos / win / ios / wasm 目标缺 `zanrt_decimal.o`**：decimal 运行时当初只补了
    macOS/Android/linux，用 `System.Decimal` 的交叉程序在这些目标上链接红。
  - `toolchain/ohos-{x64,arm64}/zanrt_sync.o` 停在 **10-05**，比 `rt_sync.c` 老，
    缺 `zan_ui_thread_set` → OHOS 的 **GUI** 程序链接红（console 程序照不到）。
    已用 zig 按 `build_ohos_rt_zig.sh` 的配方重出。
- 顺带修掉的**自相矛盾**：`zanc --list-targets` 是一张写死的表，把本机构建
  **发不了码**的目标也 advertise 出去（mozbuild LLVM 没有 RISC-V 后端，
  `zanc --target riscv64` 只会得到一句笼统的 "code generation failed"）。
  现在按 **LLVM 自己的注册表**过滤：目标表里没有的 triple 不出现在列表里，
  `--target` 直接点名"这个构建没有该 triple 的 LLVM 后端"（链接了带 RISC-V
  后端的 LLVM 时这些目标照旧可用）。后端注册要先做
  （`zan_irgen_init_llvm_targets()` 从 irgen_emit.c 提成对外函数），
  否则查表时注册表是空的，会把**所有**目标都判成不可用。
- 现在的体检结论：13 个目标 × console 全绿；GUI 探针 10 绿 3 红（红的是
  android-x64 与 ohos 两个平台，原因见 A502 / A503）。**OHOS 的 GUI exe 现在
  给的是一句"为什么 + 怎么办"**（静态链接装不下驱动要的 EGL/GLES/FreeType，
  GUI 请用 `--emit-lib`），不是一串 undefined symbol；OHOS 的 GUI 走
  `--emit-lib` 实测**链接成功**。
- 关联：A493（归档成员机制）、A502、A503、A499。

### [~] A503 OHOS 的 GUI 交叉链接缺 EGL/GLES——静态 exe 形态装不下预编译驱动的 GL 依赖

- 现象（2026-10-22 A501 扫描）：`--target ohos-x64` / `ohos-arm64` 链接 GUI
  程序报一串 `undefined symbol: eglMakeCurrent / eglDestroySurface /
  glCreateShader / glShaderSource / glCompileShader ...`（外加 20 多个
  `FT_*`），引用方是
  `packages/Zan.Gui/src/Gui/drivers/ohos-<arch>/static/libzan_gui.a` 里的
  `zan_gui_present`。
- 根因（2026-10-22 钉死）：**OHOS 的 exe 走全静态链接**
  （`ld.lld -static ... libc.a libunwind.a libclang_rt.builtins.a`），静态模型里
  没有 EGL/GLES/FreeType 的位置；而 OHOS 的 GUI 驱动直连 EGL/GLES
  （实测需要 10 个 `egl*` + 34 个 `gl*`；`FT_*` 是编进驱动归档的，所以不报
  undefined）。⇒ 任何 OHOS GUI 程序走 **exe** 这条路都链不过，**不是配置问题，
  是链接形态选错了**。
- 更正一处此前的误判（2026-10-22 复核 `scripts/build_gui_ohos.sh` 时发现）：
  "驱动是预编译产物、没有源码" —— **不对**。OHOS 驱动由
  `scripts/build_gui_ohos.sh` 从 `src/runtime/gui_runtime.c`（`-DZAN_GUI_OHOS
  -DZAN_GUI_FREETYPE`）+ freetype 源码编出来（用 DevEco Studio 自带的 OHOS
  SDK），再 `llvm-ar` 成 `static/libzan_gui.a`、编成 `libzan_gui.so`。
  **源码就在仓库里**，只是要一台装了 DevEco 的机器。
- **再更正一处（同日）**：本条上一版写着"**本机没有 DevEco**，这一支只能在那台
  机器上跑"——**也是错的**。DevEco Studio 就在这台机器上
  （`C:\Program Files\Huawei\DevEco Studio`，带
  `sdk/default/openharmony/native/{llvm,sysroot}`，sysroot 里有
  `x86_64-linux-ohos` / `aarch64-linux-ohos` 两套 `libEGL.so`/`libGLESv3.so`），
  freetype 源码也在（`D:/project/firefox/modules/freetype2`）。于是
  `scripts/build_gui_ohos.sh all` **在这台机器上跑通了**（2026-10-22 实跑）：
  两个架构的 GUI/image/audio/game 驱动全部重出。
  **教训：判"这台机器补不了"之前，先把常见安装位置查一遍**——凭印象下结论会
  把"能补"误写成"补不了"。
- 重出后暴露的两处**仓库缺口**（2026-10-22 实跑）：
  ① **committed 的 `libzan_gui.so` 是拆分前的旧产物**：它还带着 nanosvg / libwebp
     的符号（`nsvgCreateRasterizer` / `kVP8Log2Range` / `IsValidColorspace`），
     而当前代码已把图像解码拆到独立的 image 驱动（重出的 GUI 驱动导出 673 个
     符号，旧的 1005 个，多出来的正是那批解码符号），并且**缺**新加的
     `zan_gui_gpu_*` / `zan_gui_blit_*` 入口 ⇒ 仓库里那个 .so 是**陈旧**的。
  ② **image / game 驱动的二进制从来没进过仓库**：只跟踪了 `.libs` 与
     `.bundle`，而 `.bundle` 里写着 `libzan_image.so` / `libzan_game.so`
     ⇒ **发布出去的 OHOS 应用会缺这两个驱动**（audio 驱动的 .so/.a 是跟踪着的，
     image/game 不是）。
- 正确的形态（2026-10-22 实测）：OHOS 的 GUI 程序应当产出 **HAP**
  （`zanc --target ohos-x64 --emit-lib`）——**实测链接成功**：HAP 本身不调用
  EGL/GLES（那是驱动 .so 的事），驱动 .so 随应用进 bundle
  （`zan_gui.bundle` 里列了 `libzan_gui.so`），设备系统提供 EGL/GLES 真库。
  **静态 exe + GUI 驱动**这个组合在 OHOS 上本来就不成立。
- 补法（2026-10-22 已落地）：
  ① **静态 exe 带 GUI 驱动时给明确错误**（已改 `src/compiler/main.c`）：说明
     "静态链接装不下驱动要的 EGL/GLES、OHOS 的 GUI 请用 `--emit-lib`（HAP
     形态）"，不再吐 60 多个 undefined symbol 的瀑布。
  ② **HAP 形态按需链 EGL/GLES**（同上）：`<sys>`（=toolchain/ohos-<arch>/）里有
     就链上（记 DT_NEEDED），没有就点名缺哪个、去哪拷。**只列 EGL/GLES 两项**：
     OHOS 的 FreeType 是**静态**链进驱动的，设备上不存在 `libfreetype.so`
     ——列进去只会让人去找一个哪都没有的文件。
  ③ **配方改成拷真库**（`scripts/build_cross_rt.cmd` 的 OHOS NDK 块）：从
     `<OHOS_NDK>/native/sysroot/usr/lib/<triple>/` 拷 `libEGL.so` /
     `libGLESv3.so` 到 `toolchain/ohos-<arch>/`；原先那两个**空壳**（导不出任何
     符号，只记录 DT_NEEDED，连同 `scripts/ohos_stub.c`）已删——**空壳比缺库
     更坏**：它让人以为支持着这个目标。
  ④ **让 OHOS 的对象别再停在旧代码**：`toolchain.yml` 新增 `ohos-objects`
     job（zig 路径，`scripts/build_ohos_rt_zig.sh`），源码一改就重出并提交。
  ⑤ **驱动本身重出并补齐跟踪**（2026-10-22，本机有 DevEco）：GUI 驱动的 .so/.a
     重出（旧产物是拆分前的形态），image / game 驱动的 .so/.a **补进仓库**
     （`.bundle` 指着它们，仓库里却没有）。
- 剩下的：**真机 / 模拟器上的运行验证**（DevEco 模拟器装在这台机器上，
  `C:\Users\QQ\AppData\Local\Huawei\Emulator26.0`；仓库历史里也有 DevEco 模拟器
  跑通 GUI 画廊的记录 `e59be75a`）。**构建这一环已经不再是缺口。**
- 关联：A501、A502、A504。

### [~] A504 CI 的"配方产出"与"提交清单/触发路径"必须一一对应——漏一处，对象就永远停在旧代

- 现象（2026-10-22 查 A502 时顺着查出来的）：`.github/workflows/` 里重建
  交叉运行时对象的 job 有三处**清单与配方脱节**：
  - `drivers.yml` 的 win job：配方产出 `zanrt_decimal.o` / `libzancrypto.a` /
    `zan_embed_api.o` / `zan_inflate.o`，**提交清单一个都没有** ⇒ CI 每次把它们
    编出来又丢掉，仓库里的对象永远停在旧代（这正是 A493 之后 win 的 timer
    仍然背着一整块 crypto 的原因）。
  - `toolchain.yml` 的 linux/macos job：清单只有 `zanrt_*.o` +
    `libzancrypto.a`，**漏了 `zan_embed_api.o` / `zan_inflate.o`**（配方产出，
    清单不提交）。
  - `toolchain.yml` 的**触发路径**：只列了 `rt_io.c` / `rt_sched.c` /
    `rt_sync.c` / `rt_timer.c` / `rt_hw_accel.c`，**漏了 `rt_file.c`、
    `rt_decimal.c/h`、`zan_embed_api.c`、`zan_inflate.c`** ⇒ 改了这些源，CI
    根本不触发重建，交叉对象照旧是旧的。
- 另一头：**没有任何 job 重建 android / ohos / wasm32 / ios 的对象**（只有
  linux×3 + macos×2 + win×2 有）。ohos 已接上（zig 路径，`ohos-objects`
  job）；android 已在 `android-objects` job 里接上（NDK 路径）；wasm32 与 ios
  还没（wasm32 的配方是 .cmd、GUI 那一支还依赖外部 freetype checkout）。
- 补法（2026-10-22 落地）：三份清单补齐 + 触发路径补齐（21 条）+ 新增
  `ohos-objects` / `android-objects` 两个 job + `stale-check` 依赖 ohos job。
- **macOS 这条腿整个是死的**（2026-10-22 顺着查 macOS 的 decimal 查出来的）：
  - `runtime-objects` job 的检查步骤 `python3 scripts/check_macos_rt.py` 从
    **2026-09-20**（`e35521ad7` 让 `gui_compat_mac.c` 开始 import
    `zan_gui_draw_text` / `zan_gui_draw_polyline` / `zan_gui_font_height`，
    而检查器只认 libSystem.tbd 与同目录对象）起就是红的 ⇒ **job 在提交步骤
    之前就中止** ⇒ 一个月里没有任何 macOS 对象被 CI 刷新过。检查步骤红了
    就等于腿没了：检查在提交前面，红了根本到不了提交。
  - macOS 的 decimal 对象**四处皆无**（配方 / CI 清单 / ARTIFACTS 表 / 跟踪
    的文件）：`main.c` 的 macOS 分支会给用 decimal 的程序注入
    `toolchain/macos/<arch>/zanrt_decimal.o`，而新克隆的仓库里没有这个文件
    ⇒ **macOS 交叉编译一个用到 decimal 的程序必然失败**（报
    "bundled macOS runtime objects not found"，与"目录不存在"一样的措辞，
    容易误判成环境问题）。普通 console 程序不用 decimal，所以这条红灯平时
    看不见。
  - 补法（2026-10-22 落地）：配方（`build_macos_rt.sh` + `build_cross_rt.cmd`
    的 macos 块）加 decimal 编译；ARTIFACTS 表加两条；两个对象用 zig 重出
    并**跟踪进仓库**；检查器改成**从驱动 dylib 里读导出符号**当白名单
    （`packages/Zan.Gui/.../macos-<arch>/libzan_gui.dylib`，写死清单会让
    驱动一加新入口就又变红——这正是它当初变红的原因）；触发路径补
    `scripts/check_macos_rt.py`（检查器自己也要被 CI 验）。
- 留下的纪律（比清单本身更重要）：
  ① **"配方产出的东西"与"CI 提交的东西"是同一
  个集合**。加一条配方产物时，提交清单与过时检查的 ARTIFACTS 表要同步加，
  否则它会静默停在旧代，而且**构建是绿的**。
  ② **CI 腿的每一步都要单独验**：检查步骤红了会吃掉后面的提交步骤，整条腿
  静默停摆——而"腿停了"和"配方没产出"在仓库里长得一模一样（对象停在旧代）。
  ③ **白名单要从产物里读，不要写死**：允许集写成代码里的常量，就会在产物
  变化时过期。
- 补法（2026-10-22 第二轮落地，针对 win + 过时检查）：
  ① **win 的运行时对象由 CI 托管**（`drivers.yml` 的 windows job：MSYS2 的
     MINGW64 / CLANGARM64 clang 编 mingw ABI 对象，源一改就重出并提交），所以
     18 条 win 条目从 `manual` 改到 `runtime`；android 的 16 条同理（NDK 腿
     已跑通）。ios/wasm32 没有腿，改到 `manual`（只报不卡）。
  ② **`build_linux_rt.sh` 补产 `zanrt_mem.o`**（linux-musl 的小对象分配器内存
     对象）：过时表列了它、CI 提交清单用 `zanrt_*.o` 通配也覆盖得到，唯独配方
     没产 ⇒ 它永远停在旧代 ⇒ 门永远红。
  ③ **`build_win_rt.sh` 改成"先编到暂存目录、全成了再一次性换入"**：半路失败
     的重建原本会留下**半新半旧**的一包对象，而 CI 的提交步骤会把工作树里
     看到的**照单提交**——新 `zanrt_io.o` 配旧 `zanrt_timer.o` 就这么进了仓库
     （2026-10-22 实测：工作树里那份 win timer 带着 59 个 `zan_hw_*`，仓库里
     CI 出的那份是 0 个；用前者链 win hello，`.text` 168,528，用后者
     **112,336**，差 56 KB）。
  ④ 配方末尾加**自检**（有 nm 时）：`zanrt_timer.o` 里若又出现 `zan_hw_*`
     就直接失败并说明原因——那是 A493 的体积不变量被打破（`rt_timer.c` 又
     文本包含了 `rt_hw_accel.c`）的信号，让它在 CI 里当场炸，而不是让每个产物
     悄悄胖 53 KB。
- 关联：A502（android 的具体症状）、A503（ohos 的具体症状）、A493（归档拆分
  后 win 变胖的根因）、A501。

### [ ] A506 语言服务冷启动 11~13 秒——`initialize` 同步索引了整座包仓库

- 现象（2026-10-22 修 IDE 崩溃时顺带测出）：IDE 打开工程后状态栏长时间停在
  「语言服务：连接中」，实测冷启动 **10292 ms**，点重连再 **12260 ms**；
  独立跑 `build\zan-lsp.exe` 走一次 initialize 应答要 **10803 ms**（默认
  stdlib 11068 ms，热了再跑 11842 ms）。
- 判据：把 `zan-lsp.exe` 单独复制到一个没有 `../stdlib`、没有 `../packages`
  的空目录里，同样的握手只要 **5 ms**。差值全花在"索引"上，不是协议开销。
- 根因（`src/lsp/lsp_main.c`）：
  - `handle_initialize()`（:954）在应答 initialize **之前同步**调
    `ensure_project_indexed(s)`（:1135）；
  - `ensure_stdlib_indexed(s)`（:1108）调 `zan_pkg_all_source_roots(project_dir, ...)`
    （`src/compiler/package.c:1427`），这个函数按 **zan-lsp.exe 所在目录**解析
    包仓库——`build/../packages` 就是仓库的 `packages/`；
  - 于是仓库里 **8491 个 .zan** 全被 `intel_index_project()`
    （`src/lsp/intellisense.c:2911`）索引，只有 `Zan.Sdk.*` 被排除。
    stdlib 才 167 个 .zan，约 2 秒；包仓库吃掉剩下 10.8 秒。
- 已做的部分：IDE 侧握手预算本来就够（`d.waitBudget = 240` ×
  `RecvTimeoutMs() = 250` = 60 秒），不是客户端超时问题；已把连接超时从
  20 s 放宽到 60 s（按实测冷启动留 4 倍余量），并把实际耗时暴露成探针
  字段 `lsp.connectMs`。
- 方向（推荐）：**只索引工程实际依赖的包**——从工程文件/源码的 import 出发
  推出依赖闭包，`zan_pkg_all_source_roots` 换成"按需解析"而不是"扫全仓库"，
  启动快与补全不缺才能同时成立。备选：initialize 先应答、索引转后台并发
  进度通知（要处理 C 端线程安全）；或持久化索引缓存（要处理失效判定）。
- 关联：本轮已提交的 `feat(ide): 语言服务状态常驻状态栏`（把"连接中"如实
  显示出来并给一键重连）。
- 坑出处：探针的等待时长必须按被测服务端的**实测**耗时定——本轮最初按 10 s
  等，真实冷启动 13 s，直接判成假失败。
