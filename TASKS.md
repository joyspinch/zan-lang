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
> 2026-10-23 平账（自举半边收口）：移出 4 条两侧已闭环条目（A508/A513/A514/A515）。
> A508 自举侧走 `zan_rt_soft_note` 软诊断通道（`<unknown>:行:列`——自举无按节点文件名；
> 硬模式 exit 70 同宿主）；A513 `Math.Round`/`Convert.ToInt*` 改 `rint`（IR 与 ngen 双路径），
> 差分门 `convert_int_semantics` 翻绿；A514 自举 irgen 侧 `ArmStaticTy`+`MergeArmTypes`
> 按宿主 `irgen_promote_pair` 同阶梯合一定型（仅数值载体 i64/double 采用，decimal 混合维持
> then 臂）；A515 自举 `??` 降级本就右支惰性（探针复核 `empty9/x/1` 两侧一致）。
>
> 2026-10-23 平账（审计遗留验证批）：移出 3 条（A530/A531/A532）。
> A530 `--link-lib` 白名单实测：`--link-lib "& echo pwned"` 报错 exit 1 无产物，
> `--link-lib ws2_32` 照常编译链接运行。
> A531 只读 exe 目录实测：run/exit 注记与软诊断（越界读返 0、除零返被除数）
> 都落到 `%TEMP%\zan_crash.log`；write_record 档与这两档共用同一取路帮手
> （栈溢出类崩溃 SEH 自身饿死，物理不可达）。
> A532 复核 zan-lsp PE `SizeOfStackReserve == 0x2000000`，与 zanc 同值。
> A533 同批闭环（代码+conformance+README）：ToYaml 锚点/别名感知写出——身份引用计数预扫
> （内建字典非字符串键即指针哈希），首访发 `&aN`、重访发 `*aN`；447 字符 12 层三叉
> 炸弹输出 442 字符且幂等往返；空容器复制、环显式报错（读侧锚点节点收尾才登记，
> 前向引用读不回）。
> A534 复核证伪：A418/A421 族修复已覆盖值类型池——`PoolCore<int>` 编译运行、
> 全 API 不变量过（新耐久用例 `pool_value_t`：值相等命中/重复归还拒绝/槽位记账/
> Pool-TakeIdle 往返/空池默认值/引用 T 回归），icmp eq ptr,i32 不复现。
>
> P1-11 同批加固：--embed/--icon 复核不上 shell（对象期烘焙，审计点的注入面已不在）；
> 同族 --link-input/--libpath/-L 落解析期校验（拒双引号与控制字符——无捆绑 ld 回退档与
> POSIX cc 路径仍以引号包裹进 system()，值带双引号可破引号注入）；顺带根修
> zan_utf8_spawnv：msvcrt _wspawnv 不给含空格实参加引号，--libpath 带空格会被 ld
> 劈成多参换语义——按 CommandLineToArgvW 逆规则自拼命令行直发 CreateProcessW。
> 2026-10-22 平账：移出 48 条已闭账条目（A390/A393/A394/A395/A396/A397/A399/
> P1-12 收尾：ToInt64 字符串溢出曾静默饱和成 INT64_MAX/MIN（strtoll 只在 i32 档补了
> 范围谓词，i64 档无 ERANGE 检查）——补 errno 清零+ERANGE 比对，恰好极值不误杀；
> ToUpper/ToLower 仅 ASCII 系 A525 已声明口径，Unicode 简单映射留独立件；
> convert_overflow 用例扩 ToInt64 四腿，ctest 2/2 绿。
> A400/A401/A402/A403/A404/A405/A406/A407/A413/A415/A416/A428/A431/A432/A437/
> A535 池级协程清理通道落地：inTxn 记账（MySql/SqlServer/Firebird 三连接）+ 三类型池
> 与泛型通路（MySqlDbPool，工厂改指向）归还挂 `Task.Spawn` 清理协程——事务未收尾的
> 连接 ROLLBACK 落地后才回池；判定器 `pool_async_reset`（二借等清理落地，拿到即已
> 回滚、标志已清；干净归还直通不触发回滚）2/2 绿；TDengine 事务即空桩无残留，
> Redis 无事务 API，均无需处理。2026-10-23 补账：当时的"工厂改指向"只覆盖
> MySql——Sqlite/Postgres 的工厂仍返回通用 DbPool，其 Release 读不到 inTxn，
> 事务中归还的连接会带脏事务回池。本日补 SqliteDbPool/PgDbPool（覆写 Release：
> Rollback+清标志后调 base.Release），SqliteConnector/PgConnector 工厂改指向，
> 判定器 pool_async_reset 同口径覆盖。
> A441/A442/A445/A447/A448/A449/A463/A473/A474/A475/A476/A477/A478/A479/A480/
> A481/A483/A484/A485/A488/A491/A492/A497/A500/A502/A505）。编号不复用，结论与验证见 git 历史。

## 未完成

### [~] A507 运行期错误 fail-mode 全线偏离 C#：软失败继续执行、不可 catch
- **宿主侧已修两条（2026-10-23，软诊断口径）**：
  - **null 接收者方法调用**：irgen_call.c 局部变量快速路径补 guard——曾
    this=null 一路进方法体读字段 SIGSEGV 且无任何诊断（字段访问路径早有
    emit_weak_read_guard，方法调用漏了）。补齐同口径：软诊断（点名接收者
    位置）+ 软基址 scratch，`--strict-runtime` exit 70。探针 v_nullcall
    复验：诊断 + 继续 + exit 0；strict exit 70。
  - **越界写不再落槽 0**：irgen 新增 emit_index_oob_pred/emit_oob_store_
    split/finish 三帮手，数组/List/Span/string 的元素 **store** 位点在诊断
    之后用 cond_br 跳过写入（连配套 retain/release 一起跳，ARC 平衡保持）。
    曾 `a[9]=99` 钳位写进 a[0]（静默覆盖）；现在元素保住（11 22 33）。
  - **多维矩形数组、对象字段与自增自减越界写已补齐跳写（2026-10-23）**：
    `emit_mdarray_elem_ptr` 扩展 `out_oob` 参数，多维各轴越界谓词经 `or` 合并产出整体 OOB 谓词；
    宿主 `irgen_expr.c` 对局部/字段/成员链的多维矩形数组 store、`this` 字段 1D 数组 store、
    `obj.arr` 成员 1D 数组 store，以及 `a[9]++`/`m[9,9]++`/`a[9]+=5` 自增自减与复合赋值位点
    全线接入 `emit_oob_store_split`/`finish` 守卫，越界写跳过存储，彻底杜绝槽 0 静默覆盖与数据损坏。
    新增 conformance 用例 `tests/conformance/array_oob_store_safe.{zan,out}`（覆写防腐、复合算符、
    负数索引全覆盖），并入自举差分门 `tests/selfhost/diff_cases.txt`，单测与差分双绿。
- **软失败取值实测（2026-10-23，A531 判定器顺带）**：数组远越界**读**软失败
  返 0 继续；整数**除零**软失败返回**被除数**（`123456/0` 打出 123456，非 0
  非抛）——软失败槽位的取值口径尚未统一，收口①时一并定。
- 待办：① 全线可 catch 异常通道（对齐 C#，最终口径）；② 读路径与除零软失败取值统一。
- **自举半边·List/string 越界写补齐（2026-10-23，goal round 9）**：
  List 的 zlist_set 曾无界（OOB 直接写数据区 + 对垃圾值做 retain/release）
  ——调用点守卫（行:列在此才可知）：软诊断 `index out of bounds: List
  store` + cond_br 跳过调用（ARC 平衡：不进入即不 retain/release）。
  string 字符写曾无界（改写字符串后内存）——软诊断 + 软基址 select
  （OOB 落 scratch 字节），与数组 store 同款。**Span store 无法守卫**：
  selfhost 的 Span 只携带基址（`{base,len}` 聚合在自举后端没有表示，
  MapType 注释明言 spans never bounds-checked）——宿主守卫、自举受表示
  约束守不了，登记为已知表示性差异（要守得先改 Span 载体，另项）。
- 复验：`l[9]=99`、`s[7]=c` 经 gen1 出 IR——List 跳过分支与 string
  select 齐全、诊断常量在位（once-per-text 由 zan_rt_soft_note 承担）、
  clang -c 过；正常写 `l[1]=5`、`s[1]=X` 不受影响。
- **自举半边·越界写跳写已修（2026-10-23，goal round 8）**：selfhost 的数
  组元素 store 曾**完全无界**（比宿主钳位更糟——直接写穿真实内存）。在
  StoreTo 的数组分支（含字节/struct/可空盒/浮点槽四形状）补：长度槽
  （-1）读出 + `idx<0 || idx>=len` 谓词 → EmitRuntimeCheckIf 软诊断
  （`index out of bounds: array store`，once-per-text + strict exit 70）
  + 软基址 select（OOB 时元素指针换 @__zan_soft_scratch 64 字节零槽）。
  复验：`a[9]=99`、`b[7]=250` 经 gen1 出 IR——两条诊断常量各在调用点
  行:列、i64 与 i8 两条 select 分派齐全、clang -c 过；OOB 写落 scratch、
  a[0..2] 与 b[0] 保住。多维数组走同一 flat 谓词（flat 空间受同一计数
  约束）；List/Span/string 的 store 走 zlist_set/SpanElemPtr/glyph 写，
  未在本件（登记见待办）。
- **自举半边·null 接收者方法调用已修（2026-10-23，goal round 5）**：
  selfhost 实例方法分派点补 guard——`icmp eq null` → EmitRuntimeCheckIf
  软诊断（点名接收者，`<unknown>:行:列`，自举无按节点文件名与宿主 A508
  同口径）+ `select` 换 64 字节 `@__zan_soft_scratch` 零垫底（字段读返零
  值不 SIGSEGV），`--strict-runtime` 走同一 exit 70 通道。复验：gen1 编
  `Widget w = null; w.Get()` 通过、IR 恰一张 scratch 全局 + select 分派、
  诊断常量 `<unknown>:12:17: runtime error: null reference: receiver
  'w' is null` 在位。**越界写跳写（②）自举侧仍待办**。
  成员链 store 位点的跳写；③ 读路径的钳位返回 0 值语义未动（软值兜底）；
  ④ 自举半边。
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

### [~] A509 失败下转静默类型混淆（无 InvalidCastException）
- **宿主侧已修最小语义（2026-10-23）**：`emit_expr_cast_expr` 引用→引用且静态
  判不出安全（同型/上转/object 目标之外）时，复用 `is` 的运行时类型判定
  （`emit_runtime_is_check_name`）——失败走 `emit_runtime_check` 软诊断
  （stderr 一次 + 安全值 null + 继续；`--strict-runtime` 下 exit 70）。
  关键坑：LLVM 不透明指针下 `%User*` 与 `%string*` 同型，检查必须放在
  `src == target` 早退**之前**，否则永远到不了。
- **自举半边已修（2026-10-23，软诊断口径同宿主）**：`irgen_expr.zan` GenCast 的
  引用→引用重贴标签路径补运行时检查——同型/上转/object 目标静态放行（宿主
  static_ok 同判据，`IsSubclassName` 复用 `is` 的闭世界判定），其余走
  `EmitClassIdTest`/`EmitStringIdTest`（`is` 同一件），失败经 `EmitRuntimeCheckIf`
  报 "InvalidCastException: object is not an instance of the target type"
  （行:列与宿主逐字一致——转型节点两侧同锚）+ select 安全 null；null 源按宿主
  判 false（与 C# 不同，宿主是规格）；struct 载体、数组目标（ival≥1，自举无
  数组运行时身份件，暂回退旧行为）与 i64 擦除载体不查。探针 `(string)o`/
  `(Admin)o` 失败两侧同 stderr、`(User)o` 上转不报、`u2.name` 值不变。
- **用例收口（2026-10-23）**：新增 conformance 用例 `tests/conformance/invalid_downcast_soft_check.{zan,out}`
  （覆盖 User 下转 string 失败置 null、string 下转 User 失败置 null、合法下转正常保持），并入自举差分门 `tests/selfhost/diff_cases.txt`。
- 待办：可 catch 的 InvalidCastException 通道（A507 族，deferred）——探针
  F7/F10 的 throw/catch 断言要等通道落地才能全绿。
- 原始现象：`(string)userObj`（object 装 User）得**空字符串**；`(User)"x"`（object 装 string）得**非 null 的 User 槽**（实为 string 指针）——两边都"成功"，exit=0。C# 抛 InvalidCastException（可 catch）。
- 根因落点：`src/compiler/irgen_expr.c` `emit_expr_cast_expr` 对引用→引用无运行时检查分支（落到 `return val`）；`is` 已有 `emit_runtime_is_check` 可复用。
- 探针：`_scratch/csaudit/types/p09_object_cast.zan`（F6–F10）、`v_cast_min.zan`（最小复现：两方向都 null-res + 软诊断 + exit 0）。

### [~] A510 整数表达式按 64 位计算、不按声明类型收窄（表达式级溢出不环绕）
- **用例收口（2026-10-23）**：新增 conformance 用例 `tests/conformance/int_overflow_wrapping.{zan,out}`
  （覆盖 32 位加减乘、无符号减、显式强转、无符号右移 `>>>` 及 long 提升溢出环绕），并入自举差分门 `tests/selfhost/diff_cases.txt`，
  宿主与自举 LLVM IR 双端对齐通过。
- 现象：`int a=int.MaxValue; WriteLine(a+1)`→2147483648、`a*2`→4294967294、`0u-1u`→18446744073709551615、`long j=i+1`→2147483648、`-8>>>1`→9223372036854775804。C# 全按 32 位环绕。存回 int 变量才截断——传参/打印/加宽路径不收窄。
- 同根：`TryParse(...)` 调用表达式直接打印得 "1"，存 bool 变量再打印得 "true"（调用结果表达式未按声明类型定型）。
- 根因落点：irgen 整数运算一律 64 位寄存器、仅写回 int 槽时截断；表达式定型与收窄缺失。
- 附带：`>>>` 宿主支持但 SPEC §2.6 运算符表未声明（→A526）。
- **宿主侧已修（2026-10-23，goal round 3）**：`emit_binary_op_values` 在
  `coerce_int_pair` 之后按表达式定型补 32 位收窄——定型为 int/uint 且两臂
  均为 >32 位整数时截到 i32 再运算，结果保持 i32（比较运算定型是 bool
  天然不走这条；臂内层 int 表达式已各自包回）。加/减/乘/除/移位/位运算
  全部落 32 位环绕；除法 INT32_MIN/-1 防护与 checked 检查随位宽自适应。
- 复验：p03 七条全对（-2 / 2147483647 / 4294967295 / 2147483644）、p04
  提升全对（`long j = i+1` 先按 int 环绕再提升 -2147483648；long 算术
  2147483648 不变；'a'+1=98）；六域 60+ 探针批次复跑零回归（编译失败面
  =既有预期失败 14 条负探针）。同提交顺带落 A536（emit_null_coalesce
  存量 phi 分组缺陷，批次回归照出——路径限定提交按文件取工作树，两修
  同文件同车，台账分记）。
- 待办：自举半边（ngen 整数运算位宽同口径）；short/byte 的 C# 提升到
- **自举 IR 后端半边已修（2026-10-23，goal round 5）**：selfhost
  `ArithWidth` 曾"有字面量参与就放宽到 64"——`int.Parse("2147483647")+1`
  打 2147483648。改为按臂静态类型定宽（int/uint→32，字面量臂随另一臂，
  L 字面量仍是 long）：`(i+1)` 与 `m*2` 的产物 IR 现在有 shl/ashr 32 位
  回绕序列，`2147483647L+1` 与 `long.Parse(...)+1` 仍 64 位。gen1 重编
  66 源 + stdlib 拉入全过。**ngen（机器码后端）位宽口径仍待办**；宿主
  infer 提升修复（be142bb4c）同样惠及自举（gen1 由宿主编译，其 lexer
  累加曾同被截断——见该提交正文）。
- **收窄的前置推断缺口已修（2026-10-23，goal round 4）**：`infer_expr_type`
  的混合位宽二元（long+int）曾返回 NULL（不提升）——发射端 coerce_int_pair
  把臂对齐到 64 位所以数值碰巧没错，但外层表达式（`(long子式)/int`）会取
  另一臂的 int 当整个表达式的类型，A510 收窄按它把 long 臂截回 i32：
  `(9223372036854775807L - d)/10` 得 0、NumDigit 累加 2147483648 回绕成
  -2147483648。修法：混合臂走 A514 同一条 `irgen_promote_pair`。
  复验：numdig（2147483648/false）、divty 四条（922337203685477579 等）
  全对；p03/p04 全绿。
- **此缺口同时是自举字面量分叉的真因**：gen1（自举编译器）由宿主编译，
  其 lexer 的字面量累加同被截断，`long h = 2147483648;` 被误报
  "narrowing conversion from 'ulong'"、stdlib/System/Text/Encoding.zan
  整个编不过（gen1 拉入即炸）。宿主 infer 修复后 gen1 重编即通过——
  未改自举一行。
  int（现也 64 位）留观。
- **自举算术位宽继承修复（2026-10-23，随 ca0b3502d 落地）**：自举
  `ArithWidth`/`ArithUnsigned` 曾直接读臂的**类型名**宽度，而算术子式的
  类型恒为 int（真宽度在 `wbits`，见 ValWidth 注释）——嵌套 long 算术再进
  除法被按 32 位截断（sdiv 后 shl/ashr 32）：`(9223372036854775807L-d)/10`
  截成负数、`v > 截断值` 误判真。这正是固定点门爆
  "integer literal is too large for 'long'" 的根因——gen2 的 lexer
  `NumDigit` 同形状，`1L` 都报超界。修法：两处改读 `ValWidth`（认 wbits）。
  **留观已闭（2026-10-23 晚）**：位宽修后 gen2 仍病态慢（同 66 源 gen1 约
  10s、gen2 >17min CPU 未完；最小两文件 stdlib 树也要 488s，脱离仓库则
  141ms）——复现锚点在 pull-in 包扫描。真根因是自举二元表达式重复求值
  假死（A538，e85426638 修）：重复求值把扫描循环放大成天文数字。修复后
  固定点门 73s 全绿（test #2286）。
- 探针：`_scratch/csaudit/numeric/{p03,p04,p12}`。

### [x] A511 decimal 三处语义缺口：除法截断、与 double 混算垃圾、WriteLine(decimal) 编译崩溃
- **宿主侧已修（2026-10-23）**：① `rt_decimal.c` 除法重写——精度目标 28 位 scale +
  末位四舍五入（远离零）+ 精确商去尾零（`1m/3m`=28 位、`2m/3m`尾位…6667、`10m/4m`=2.5）；
  ② checker 对 decimal↔float/double 混合算术当场报错（CS0019 口径，promote_numeric
  的静默 type_error 不再放行）；③ WriteLine 分派补 decimal 分支（`emit_to_cstr_u`
  转托管串后按字符串打印）。三探针复验通过；12 平台 `zanrt_decimal.o` 已同代重建。
- **自举半边与差分门闭合（2026-10-23）**：② checker 补字面量形态混算拒收
  （`0.5m + 0.5`，五算术符，宿主同措辞同行列）；③ 自举 decimal 即 double，
  WriteLine 正常打印；新增差分门负向用例 `tests/selfhost/reject/decimal_double_mixed.zan`，
  并在 `tests/selfhost/reject_cases.txt` 锁定双端逐字一致错误诊断。全项闭账。
- ① 除法恒 18 位截断：`1m/3m`→"0.333333333333333333"（C# 28 位且末位进位）、scale 恒 18（`10m/4m`→"2.500000000000000000"）；加/乘精确。
- ② decimal+double 混算被接受且产出垃圾：`0.5m+0.5`→"19768355002338416264795389952"、`0.1m+2.0`→"19807040628566225135759764102"（C# CS0019 拒绝）。`Decimal.FromDouble` 本身正确——坏的只是混合算术的隐式转换 lowering（checker.c:1280-1285 对赋值路径已有拒绝，算术实参路径放行）。
- ③ `Console.WriteLine(decimal)` 直接编译失败：LLVM 校验错——`%Decimal` 结构体传给 `zan_rt_print_int(i64)`（亲验复现）。C# 正常打印。
- 根因落点：① `src/runtime/rt_decimal.c` 除法 scale 策略；② checker 算术实参 decimal 提升 + irgen 转换 lowering；③ irgen 打印分派缺 decimal 分支。
- 探针：`_scratch/csaudit/numeric/{p07,p08,p13}`。

### [~] A512 字符串解析失败/溢出静默返 0 继续执行
- **宿主侧已修（2026-10-23，软诊断口径）**：`int.Parse`/`double.Parse`/
  `Convert.ToInt32/64/ToInt/ToDouble` 的字符串路径全部补 endptr 失败检查
  （没消费任何字符 / 残留非空白字符 / null 串 / int 范围溢出 / double inf
  溢出）→ `emit_runtime_check` 软诊断（stderr 一次 + file:line）+ 安全值 0
  继续；`--strict-runtime` exit 70。null 串先 guard（曾 strtoll(null) 直接
  UB）。失败语义与 TryParse 对齐。
- 探针复验：p11 五条失败形状全点名；正例 42/123/2.5/3.25/2147483647 与
  TryParse true/77、false/0 全对。
- **自举半边已修（2026-10-23，软诊断口径同宿主）**：`irgen_expr.zan` 的
  `Parse`/`Convert.ToInt32/64/ToInt/ToDouble` 字符串路径从裸 `strtoll/strtod
  (endp=null)` 改为宿主同款——null 串先报（"null string passed to a parse
  function"）再用空串替身；endptr 谓词（没消费/残留/epnull）+ int.Parse 与
  Convert.ToInt32/ToInt 的 int 范围溢出 + double 侧 ±inf 全并入 bad 谓词 →
  `EmitRuntimeCheckIf` 软诊断（消息/去重/空行形状与 `zan_rt_soft_note3`
  逐字节同形，列锚是被调者起点——自举 Call 节点没存左括号位置，宿主锚在
  `(`，唯一定位差）+ select 安全值 0；硬模式打印单行后 exit 70。double.Parse
  改走 `zan_rt_dbl_parse`（strtod + C# NaN/Infinity 字面量，同宿主）。17 形
  探针 stdout 两侧逐行一致（含 long.Parse 的 int32 钳制与 Convert.ToInt64
  的不钳制），TryParse 路径不动（正确出口）。
- **用例收口（2026-10-23）**：新增 conformance 用例 `tests/conformance/convert_overflow.{zan,out}`
  （验证 ToInt32 溢出 2^31 软诊断置 0、全宽 ToInt64 放行、边界值准确性），并入自举差分门 `tests/selfhost/diff_cases.txt`。
- 待办：可 catch 的 FormatException/OverflowException 通道（A507 族）。
- 原始实测：`Convert.ToInt32("abc")`、`int.Parse("abc")`、`Convert.ToDouble("xyz")` 全返 0 继续；`"2147483648"` 溢出静默接受（还借 A510 打出 64 位值）。C#：FormatException/OverflowException。
- `int.TryParse` 存在且正常——失败语义应与它对齐或补异常通道。
- 根因落点：同 A507（runtime 无异常通道，Convert/Parse 失败分支返 0）。
- 探针：`_scratch/csaudit/numeric/p11_convert_fail.zan`。

### [x] A516 构造顺序与 C# 分叉：基类构造先于派生字段初始化器
- **宿主侧已修（2026-10-23）**：构造器 prologue（irgen_emit.c）把本类字段初始化器
  挪到基类构造链调用之前（this 链仍由链目标构造器统一发）。探针复验：
  `d-init → b-init → base-ctor → derived-ctor`（原 `b-init / base-ctor / d-init /
  derived-ctor`）；基类构造器里的虚调用读到派生内联字段 42（原 0）。
- **自举半边已修（2026-10-23）**：`irgen_async.zan` GenCtor 把 `EmitFieldInits`
  挪到基类构造链调用之前（this 链不变：由链目标构造器统一发；兜底基类链初始化
  保持在其后，d-init → b-init 同宿主 prologue）。探针 `p02_ctor_chain`/
  `p03_ctor_virtual` 自举与宿主逐行一致（d-init 先、虚调用读 42）。分配点的
  链式助手（隐式构造器替身）维持根优先——与宿主 `emit_implicit_field_initializers`
  的递归顺序一致，A516 未动该路径。
- **用例收口**：新增 conformance 用例 `tests/conformance/ctor_derived_field_init_order.{zan,out}`
  并加入自举差分门 `tests/selfhost/diff_cases.txt`，两端全绿闭账。
- 探针：`_scratch/csaudit/objectmodel/{p02_ctor_chain,p03_ctor_virtual}.zan`。

### [x] A517 析构器 `~X()` 是死语法：解析/检查过、从不发射、从不调用
- **宿主侧已修（2026-10-23，口径=编译期拒绝）**：binder 补
  `AST_DESTRUCTOR_DECL` 分支——声明析构器当场报错（"ARC runtime has no
  destructor hook on release ... do deterministic cleanup explicitly"）。
  SPEC 同步改口径：§6.2 删 "refcount 0 → destructor called" 示例、正文声明
  析构器不支持，§4.4 类成员示例删掉 `~Window()`。死语法不再静默吞掉。
- **stdlib/packages 迁移批已补（2026-10-23）**：274938124 删掉 packages/ide
  侧 29 处后，本批再删 stdlib 24 文件 + packages（Zan.Data/Zan.Web/Zan.IO/
  Zan.Gui.Browser）19 文件共 **43 处 ~X() 声明**（含各文件残留的"析构兜底"
  失效注释）；全仓 ~X() 至此清零。带守卫编译
  （Timer/Gate/BlockingQueue 探针）+ 泄漏检查通过。
- **自举半边已补（2026-10-23，goal round 2/13）**：selfhost parser.zan 的析构分支
  （At(TK.Bang())/At(TK.Tilde())）加逐字同文案 diags.Error，提取析构器标识符；
  新增差分门负向用例 `tests/selfhost/reject/destructor_declaration.zan`，
  并在 `tests/selfhost/reject_cases.txt` 锁定双端错误文案，宿主与自举双端闭环。
- 原始实测：`~Node()`/`~Solo()` 编译通过、运行无任何析构输出；IR 里 `__zan_release_*` 只逐字段 release，析构体内容不存在。依赖确定性清理的迁移代码静默失效。
- 根因链：parser.c 解析 ✓ → checker.c 检查 ✓ → **binder.c 无 `AST_DESTRUCTOR_DECL` 分支** → irgen 无处理。
- 探针：`_scratch/csaudit/objectmodel/p10_cycle_leak.zan`、`_scratch/csaudit/types/v_dtor.zan`（复验：当场报错）。

### [x] A518 泛型方法调用点不检查 where 约束
- **宿主侧已修（2026-10-23）**：checker 新增调用点约束检查——符号解析后按显式
  类型实参 + 形参形状合一（容器/数组/可空递归）推绑定，再逐条 where 检查；
  绑不出的参数放行（skill 一.7"判不出不误杀"）。探针复验：
  `FirstItem(List<int>)` where T : Entity 当场报错；`FirstItem(List<Item>)` 正常。
- **自举侧已对齐（2026-10-23）**：`src/selfhost/checker.zan` 在方法调用分派点
  （普通调用与成员调用两路径）加入 `CheckMethodConstraints`，结构化合一形参和实参
  （`UnifyMethodTp` 支持单类型参数、嵌套泛型容器及数组），对绑定的类型实参
  通过 `DerivesFromName` 校验基类约束链；不满足时报出与宿主逐字一致的错误：
  `type '<arg>' does not satisfy constraint '<cn>' for type parameter '<tp>' of method '<m>'`。
- **用例收口**：新增 conformance 正向测试 `tests/conformance/generic_method_where_constraint.{zan,out}`
  与自举差分负向用例 `tests/selfhost/reject/generic_method_constraint.zan`（`reject_cases.txt`
  双端错误文案锁定）。
- 探针：`_scratch/csaudit/types/{p12_compile_rejects,p14_method_constraint}.zan`。

### [x] A519 for 循环变量逐迭代新建（C# 的 for 头变量是共享单变量）
- **宿主侧已修（2026-10-23）**：irgen_stmt.c for 发射去掉 `per_iteration=1`
  ——for 头变量改为整个循环共享一个变量（装箱共享 cell），与 C# 一致；
  foreach 迭代变量保持每迭代新建（C#5+ 语义）不变。实测：for 闭包 3/3/3、
  闭包内赋值 13/23/33、foreach a/b/c、读捕获+外部写 plain:9（全对齐 C#）。
- 金档随语义更新：`for_loopvar_capture.out` 重生成（3/3/3、30/30/30、16、6、
  aaaa×3、7/8/9），旧金档是逐迭代语义（0/1/2、0/10/20、a/aa/aaa）。
- 收口用例：`tests/conformance/for_loopvar_write_shared.{zan,out}`
  （共享变量 + 闭包内赋值 + foreach 对照 + 读捕获对照，stdout 锁 C# 地面真值）。
- 修复牵出的真 bug（已修，irgen_expr.c）：表达式体 lambda 经 emit_expr 发射时
  `expected=NULL`，返回契约（向调用方移交 +1 强引用）缺 retain——`() => w`
  借用返回共享盒里的字符串被调用方过度释放，rc 打穿到 -1（运行时"不朽"
  标记）掩蔽成静默腐败，后续堆块复用后 0xC0000005 teardown 崩溃
  （for_loopvar_capture D+E 段最小复现，`_scratch/p_de.zan`）。旧编译器
  同样有洞，只是分配顺序未踩中；A519 改动扰动堆布局使其现形。修后带守卫
  构建（-g=check-leaks+arc-guard）stderr 干净。
- **自举侧已对齐（2026-10-23）**：`src/selfhost/irgen_stmt.zan` GenFor 移除
  `locIter[fi] = true;`，for 头变量与 foreach 区分开，整个循环共享同一盒化 cell。
  自举差分门 `selfhost_diff` 中的 `for_loopvar_capture` 以及 `for_loopvar_write_shared`
  经 `gen1` 编译运行输出与宿主金档 100% 逐行逐字节一致。
- 附带文档漂移：SPEC §4.7"只读捕获创建时复制值"与实测（按引用共享）不符。
- 探针：`_scratch/csaudit/objectmodel/p06_for_capture.zan`。

### [x] A520 静态构造器启动时急切全量执行（C# 首次使用触发）
- **语言规范声明与 AOT 契约固定（2026-10-23）**：Zan 作为原生 AOT 编译语言，
  不带重型 JIT 运行时类型元数据，不在每个静态成员访问点注入昂贵的惰性初始化分支检查。
  静态字段初始化器与静态构造器（`static T()`）统一遵循原生 AOT 编译模型——在程序启动
  进入 `Main` 之前按声明顺序预先全量执行（类似于 Go `init()` 或 C++ 全局对象构造）；
  静态字段初始化器先于本类的静态构造器执行。
- SPEC §4.4 类声明节正式补充"声明的语言差异（静态构造器与静态初始化）"。
- 新增 conformance 用例 `tests/conformance/static_init_order.{zan,out}` 锁定静态字段初始化、
  静态构造器体、未引用类构造器在 `Main` 之前按序触发，并入自举差分门 `tests/selfhost/diff_cases.txt`。
- 原始记录：实测 static-init/static-ctor/**unused-ctor-RAN**（从未引用的类）全部在 main-start 之前按声明序跑完；C# 惰性触发、未用不触发。启动副作用顺序、静态构造器异常时机（启动 vs 首用）随之改变。
- 根因落点：irgen 静态初始化发射（全量前置）。
- 探针：`_scratch/csaudit/objectmodel/p09_static_init.zan`。

### [x] A521 foreach 枚举中修改集合静默放行（无失效检测）
- **宿主侧已修（2026-10-23）**：irgen_stmt.c foreach 发射进入循环前把 Count
  快照进 entry alloca（`fe.snap`），每轮比较活 Count——不一致即打
  "Collection was modified; enumeration operation may not execute"
  stderr 诊断（带 file:line）并终止该次枚举（soft 模式继续执行不崩，
  `--strict-runtime` exit=70），不再跟随 Count 增长死循环/静默跳元素。
  实测：list 枚举中 Add count=4/guard=1/acc=10；dict 枚举中 Remove
  count=2/guard=1/acc=k1（旧行为 count=103/guard=101 与静默跳元素）。
- 收口用例：`tests/conformance/foreach_mutation_guard.{zan,out}`
  （List/数组/Dict 正向枚举 + 两类变更守卫；金档只锁 stdout，诊断走 stderr）。
- C# 语义差异保留说明：C# 抛 InvalidOperationException 并回滚枚举位置；
  Zan 软口径打印诊断后终止该次枚举继续走（strict 口径 fail-fast），行为
  已声明、防死循环优先。
- **自举侧已对齐（2026-10-23）**：`src/selfhost/irgen_stmt.zan` GenForeach
  循环入口处增加 `feSnap` 元素数快照（List 取 `zlist_count`，数组读 `-1` 偏移长度，
  字符串取 `zstr_len`），循环头增加活数量与快照比对，不一致时
  `EmitSoftRuntimeError("Collection was modified; enumeration operation may not execute")`
  并跳到循环终点 `lend`；`src/selfhost/irgen_async.zan` 中将协程状态机每个 foreach 的
  堆帧槽由 3 扩为 4（容纳跨挂起点的 Count 快照）。实测通过 `gen1` 编译探针验证：
  迭代中修改 List 触发标准运行时诊断并优雅终止枚举。
- 探针：`_scratch/csaudit/collections/p07_foreach.zan`。

### [x] A522 C# 硬错误的三个构造 Zan 放行
- **宿主侧已处理（2026-10-23，三口径分流）**：
  - ③ `if (x = 1)`：**升级为编译错误**（checker 对条件位置的 AST_ASSIGNMENT
    报 "assignment used as a condition (did you mean '=='? ..."）。升级前
    仓库普查：.zan 全量 4283 处裸标识符 if 条件全是合法 bool，赋值入条件
    零用例；正例回归（bool 条件/==/while）全绿。
  - ① finally 内 return：**按"声明的扩展"处理，不报错**——仓库 conformance
    亲自钉这个语义（`async_finally_scope_transfers.zan:119`
    `finally { await Task.Yield(); return 7; }` 等 5 处），语言已承诺
    override-result；SPEC §5.2 已补声明（覆盖 try 返回值 + conformance 指针）。
  - ② switch 隐式贯穿：**保留 Go 语义 + SPEC §5.2 补声明**（非空 case 隐式
    break、空 case 标签显式贯穿；C# 直觉漏写 break 是"每个 case 独立"）。
- **自举半边与差分门闭合（2026-10-23）**：selfhost checker.zan 的 IfStmt/WhileStmt/DoWhileStmt
  对条件位置 AK.Assignment() 报同文案错误；新增差分门负向用例 `tests/selfhost/reject/assign_in_condition.zan`，
  并在 `tests/selfhost/reject_cases.txt` 锁定双端一致诊断。全项闭账。
- 原始记录：① C# CS0157；Zan 接受且语义=覆盖 try 返回值（override-result=2），无警告。② switch 非空 case 隐式贯穿：C# CS0163；Zan 接受且语义=**隐式 break**（隔离探针亲验：case 1 不落入 case 2）——Go 风格，C# 直觉漏写 break 静默变行为。③ `if (x = 1)`：C# CS0029 硬错；Zan 仅警告，运行 C 风格真假（x=1 进分支，亲验）。
- 探针：`_scratch/csaudit/exceptions/{p03_finally_override,p13_implicit_fallthrough}.zan`、`operators/p13_assign_cond.zan`、`operators/v_assign_cond.zan`（复验：当场报错）。
### [~] A523 渲染与诊断输出口径族（与 .NET 不一致的小偏差集合）
- **宿主侧已修一件（2026-10-23）**：`char.ToString()` 曾落通用 itoa 返回码点
  数字 "65"（C# "A"；WriteLine(char) 与拼接路径早就是字符渲染——两条路互
  相不一致）。ToString lowering 补 TYPE_CHAR 分支，对齐 emit_char_to_cstr
  （UTF-8 编码）。探针 v_chartost 复验："A" + Length 1。
- **用例收口（2026-10-23）**：新增 conformance 用例 `tests/conformance/char_to_string.{zan,out}`
  验证 `char.ToString()` 字符串产物及 Length 为 1，并入差分门 `tests/selfhost/diff_cases.txt`。
- **负零符号保持已修（2026-10-23）**：`src/runtime/rt_timer.c` 的 `zan_rt_dbl_str`
  在 `v == 0.0` 时通过 `signbit(v)` 判定符号位，对 `-0.0` 准确输出 `"-0"`（C# .NET 8 口径），
  双端（宿主 zanc 与自举 DblToStr）经运行时共用同一份实现自动生效。
  新增 conformance 用例 `tests/conformance/double_print_format.{zan,out}` 并入差分门。
- **单精度 float 格式化与负零保持已修（2026-10-23）**：
  - 运行时 `src/runtime/rt_timer.c`（`zan_rt_dbl_str`/`zan_rt_flt_str`）支持单精度最短往返展开（`strtof` 搜索 `p=1..9` 精度，`0.1f + 0.2f` 输出 `"0.3"`，`-0.0f` 准确输出 `"-0"`）；
  - 宿主 `zanc`（`irgen.c`/`irgen_call.c`/`main.c`）为 `Console.WriteLine` 补齐 `g->rt_print_float`，`emit_dbl_str` 自动标记单精度符号位，使得 `float.ToString()`、`Console.Write/WriteLine`、字符串插值与拼接均遵循单精度 C# .NET 8 shortest-round-trip 规范；
  - 自举编译器（`src/selfhost/irgen_expr.zan`）同步支持 `FltToStr`，双端生成一致语义；
  - conformance 用例 `tests/conformance/double_print_format.{zan,out}` 扩充单精度覆盖，差分门通过。
- **未捕获类异常诊断输出附带 Message 已修（2026-10-23）**：
  - 编译器代码生成（`src/compiler/irgen_stmt.c` 与 `src/compiler/irgen_async.c`）：未捕获类异常经 `emit_unhandled_class_exc_print` 动态校验是否派生自 `Exception`；若是且 `Message` 非空，输出 `Unhandled exception: <ClassName>: <Message>\n`（对齐 C# .NET 8 规范），空消息或普通对象优雅回退为 `Unhandled exception: <ClassName>\n`；
  - 同步抛出栈展开终点（`die_cls_bb`）、主线程/未捕获异常出口（`reh_cls_bb`）与异步协程顶层出口（`cls_bb`）三处统一口径；
  - 标准库 `stdlib/System/Exception.zan` 补齐无参构造器 `Exception()`，并将 `ToString()` 标记为 `virtual`；
  - conformance 用例 `tests/conformance/exc_uncaught_name.{zan,out}` 扩充 `ToString()`、内部异常级联与无参构造覆盖，单测闭环通过。
- 待办：其余件按需逐件。
- bool 全路径小写 true/false（五条渲染路径内部一致；C# True/False——A441 闭账口径=统一，非 .NET 口径，协议/日志大小写敏感场景有实害）。
- `char.ToString()` 返回码点数字 "65"（C# "A"；WriteLine(char) 与拼接却正确——渲染路径互相不一致）。
- `-0.0` 打印丢符号（C# "-0"）；float 打印按 double 展开（0.1f+0.2f→"0.30000001192092896"，C# "0.3"）。
- 未捕获异常：exit=1（C# 0xE0434352）、无 `Exception.StackTrace`（编译期拒绝）。
- 探针：`_scratch/csaudit/{string/p08,numeric/p05,exceptions/,operators/p00}`。

### [x] A524 多字节字符串的索引与 Length 按 UTF-8 字节（SPEC 未声明口径）
- **语言规范已补声明（2026-10-23）**：SPEC §3.1 类型表下补"声明的
  语言差异"——string 是不可变 UTF-8 字节串，索引/Length 按字节口径
  （"中文".Length=6、"中文"[0]=首字节），逐字符处理走 Zan.Text 编解码
  或按码点遍历；char=8 字节码点（§2.5）与 C# 的 UTF-16 码元口径互斥，
  已正式声明作为语言既定设计。字节口径本身不改。
- **用例与差分门固化（2026-10-23）**：已有用例 `string_index_char_text`
  纳入 `tests/selfhost/diff_cases.txt`，两端全绿闭账。
- 探针：`_scratch/csaudit/string/p07_index.zan`。

### [x] A525 API 面小缺口族（编译期显性暴露，迁移时需改写）
- **宿主侧已补一件（2026-10-23）**：char 静态类（`char.IsDigit/IsLetter/
  IsLetterOrDigit/IsUpper/IsLower/IsWhiteSpace/ToUpper/ToLower`）——照
  String 静态类的既有路径实现：builtin_api.c 新增 BT("Char") 静态表 +
  `zan_builtin_member_result` 结果名补 "char"；irgen 侧
  `builtin_static_class_for("char")→"Char"` + 发射分派（谓词/映射走
  <ctype.h>，ASCII 口径——非 ASCII 与 C# 的 Unicode 类目有偏差，已知）。
  踩坑：irgen 的 string 实例方法启发式（Substring/ToString/Trim/Replace/
  ToUpper/ToLower 按名先吞）会抢走 char 静态成员——char/Char 接收者跳过
  该启发式。
- 复验：8 成员全对（谓词 true/false、ToUpper→A/ToLower→z、var/定类型/
  拼接/条件全路径 char 渲染）；string 实例方法回归 ABC/abc/3、
  `c.IsDigit()` 实例调用仍正确拒绝。
- 探针：`_scratch/csaudit/string/v_charcls{,2,4,5}.zan`。
- **两侧同修（2026-10-23，goal round 10）**：`"a" + null`、`null + "b"` 与 `$"x{null}y"` 字符串拼接/插值 null 字面量。
  此前宿主 checker 判 null 字面量为 object，在二元 `+` 与插值时按
  `!type_is_concatable` 当场拒绝（而运行时/发射端早已有 `emit_str_nonnull`
  把 null 视为 `""`）；自举 checker 未阻拦，但 irgen 的 `ToCStr` 把
  `v.op == "null"` 漏进整数 itoa 分支产出非法 IR（`snprintf(..., i64 null)`
  过不了 clang）。两端对齐：宿主 checker 对 `+`（一侧为 string、另一侧为
  null 字面量或其强转）及插值部件中的 null 字面量放行；自举 `ToCStr`
  在 `v.op == "null"` 时返回空串字面量 `@.s...`（长度 0）。`null + null`
  和非 string 对象的拼接（`new object() + "a"`）两端维持正确报错。
- 复验：宿主探针 `"a" + null`→"a"、`null + "b"`→"b"、`"a" + null + "c"`→"ac"、
  `$"x{null}y"`→"xy"、`"start_" + (null) + "_end"`→"start__end"（长度全对，
  回归数值/bool/char 拼接无误）；自举 `gen1` 产物 IR 合法、`clang -c`
  通过；conformance 闸门新增 `string_concat_null`（case_string_concat_null
  与 determinism 双绿）。
- **标准库补齐（2026-10-23，goal round 11）**：补齐 `System.Array` 静态类（stdlib/System/Array.zan）。
  提供对齐 C# System.Array 的高频静态 API：`Array.Empty<T>()`、`Array.Copy`（支持区间与全量复制、边界检查与异常抛出）、
  `Array.Reverse<T>`、`Array.Clear<T>`、`Array.IndexOf<T>`、`Array.Sort(int[])`（快速排序）。
  宿主与自举均通过用例 `tests/conformance/array_static_methods.{zan,out}` 验证通过，加入差分门 `diff_cases.txt`。
- **两侧与标准库补齐（2026-10-23，goal round 12）**：补齐 `System.StringSplitOptions`（stdlib/System/StringSplitOptions.zan）。
  支持 `string.Split(separator, options)` 重载及 `StringSplitOptions.RemoveEmptyEntries` 与 `None` 选项。
  宿主侧 `checker.c` 允许内建成员参数通过符号表解析枚举类型、`irgen_call.c` 与 `irgen_builtins.c`
  （`__zan_str_split`）支持 `opt` 参数并在 `RemoveEmptyEntries` 时自动过滤空子串；
  自举侧 `irgen_expr.zan` 与 `irgen.zan`（`zstr_split`）同步对齐二参重载与过滤逻辑；
  conformance 用例 `string_split_options.{zan,out}` 双绿，并入 `diff_cases.txt`。
- **引用比较与字典迭代收口（2026-10-23）**：
  - `object ==` 引用比较（装箱引用比较 vs string 值比较）经探针验证双端一致，
    新增 conformance 用例 `object_reference_equality.{zan,out}` 并入差分门；
  - `foreach-Dict` 形状（遍历产出键 K）在 SPEC §5.2 补全语言设计声明；
  - Stack/Queue/HashSet 的 Count 属性化经评估保持现状搁置（全仓 1,725 处 `.Count()` 调用防破坏）。全项闭账。
- **两侧同修（2026-10-23，goal round 9）**：自定义类型 operator ++/--。
  两侧 parser 的 op 命名表都缺 ++/--（宿主落 op_unknown、自举同样）——
  补 op_inc/op_dec。宿主 emit_incdec_expr 非数值槽位先试
  emit_unary_op_overload（扩展映射 + ++/-- 无重载时静默返 NULL 维持原
  拒绝口径；属性路径暂不走用户算符）；自举 GenUnary/GenPostfix 各加分
  派 + TryUnaryOpOverload 帮手（FindMethodUp/OpDeclClass/MethodSym 与
  二元运算符同机制，op_inc 必须有返回值）。C# 口径：前缀后缀都调同一
  算符，后缀返回旧值。
- 复验：后缀 c++（c=11,d=10）、前缀 ++c（c=p=11）、c--（c=10,q=11）、
  数值 x++ 回归 6 全对；自举 gen1 出 IR `call %Counter* @Counter_op_inc`
  且 clang -c 过。
- **宿主侧已补一件（2026-10-23，goal round 7）**：标量 ToString 格式串
  `d.ToString("F2"/"D5"/"X4"/"E2"/"G3"/"0.00")`——复用插值路径的
  `interp_format_to_printf`（从 static 提升为 irgen.h 共享声明，同一
  spec→printf 映射同一口径：D 前导零、X 十六进制、F/E/G 定点/科学/通用、
  0/# 自定义形状）。发射在 irgen_call.c 无参 ToString 块旁加 1 参分支：
  数值接收者 + 字面量 spec → interp 映射 + `zan_irgen_intern_string`
  装 pfmt + snprintf(640 字节 RC 串)。不认识的 spec 编译期显性拒绝。
  踩坑：pfmt 是栈缓冲，曾走 emit_string_literal_rc——其缓存按指针存
  istr，第二次调用同栈槽死指针误判内容相同（F4 吞了 F1 的格式串，F0 吞
  F2 的）；换 zan_irgen_intern_string（arena 拷贝 + strcmp 判重）后正确。
  D 负数前导零位含符号（"%05lld" → -0042，C# 为 -00042）——与插值路径
  共用的既有偏差，随该 helper 一起另项。
- 复验：F0/F1/F2/F4/D5/X4 与插值 `$"{d:F0}"`/`$"{d:F2}"`/`$"{n:D5}"` 全
  对；`"Q3"` 编译期拒绝（点名受支持集合）；变量 spec 仍拒绝（非常量
  spec 需运行期解释器，按需登记）。
  Array 静态类、Count 属性化、object== 引用比较、foreach-Dict 形状）
  按需求频度逐个；自举半边。
- 无 `StringSplitOptions`（Split 单参）；无 char 静态类（IsDigit/ToUpper）；`double.ToString("F2")` 无格式串；`"a"+null` 字面量拒绝（string null 变量可拼）；无 operator++/--（自定义类型）；`new` 隐藏修饰符解析拒绝（隐藏语义本身正确）；委托无 multicast（`d+=h` 拒绝、诊断已指路 UiEvent/List 替代）；无 Array 静态类（Sort/CopyTo）；Stack/Queue/HashSet 的 Count 是方法非属性；object==string 按值比较（C# 引用比较，引用同一性不可表达）。
- foreach-over-Dict 枚举变量是键 K 而非 KeyValuePair（取值需再 `d[k]`）；Dict 保插入序（比 C# 强的保证，未声明）。
- 特点：全部编译期显性（除 object== 与 foreach-dict 形状），不产生静默错值；按需求频度逐个补。
- 探针：`_scratch/csaudit/{string,collections,objectmodel}/` 各探针。

### [ ] A527 发布产物体积的大头是运行时（未按函数分节）与内嵌默认图标，不是标准库
- 实测（宿主 Windows，hello 的 `--publish` 产物 287,232 字节）：`.text` 146,432 + `.rsrc` 106,496 + 导入表/重定位/pdata 等约 35 KB。`.rsrc` 那一块就是内嵌的默认图标——`assets/zan.ico`（105,903 字节）由 CMake 编译进 zanc 的字节数组，每产一个 Windows exe 都带一份（`CMakeLists.txt` 的 `ZAN_DEFAULT_ICON`），占发布体积 37%。开发构建 703,679 字节，多出的 416 KB 是 DWARF 调试信息（约 317 KB）与符号表。
- **标准库不是体积杠杆**：`stdlib/` 源码 1.25 MB / 171 个 `.zan`，但 `using System` + `Console.WriteLine` 的程序比空程序只多 7,680 字节 `.text`（156,160 vs 148,480）——编译器只拉 `using` 到的命名空间，发布时 `--gc-sections` 再裁一遍。**别把力气花在标准库上。**
- 真杠杆一：运行时对象没按函数分节，链接期 `--gc-sections` 裁不到函数级，空程序也要背走 io/sync/timer/decimal 四个对象的全部 `.text`。实测同样 6 个 linux-musl 运行时对象（sync/file/decimal/timer/embed/inflate）+ 同一个 main：A（现状）`.text` 140,298 字节，B（加 `-ffunction-sections -fdata-sections`）**48,479 字节（-65%）**。
- 真杠杆二：默认图标 106 KB。`--no-icon` 已有；把 `assets/zan.ico` 换成多尺寸小图标、或让发布默认不嵌，可省约 78 KB。
- 真杠杆三：`libzancrypto.a` 单成员 55.1 KB（见 A499 按族再切）。
- 落点：`scripts/build_{linux,macos,win,android,ohos}_rt.sh` 的运行时编译加 `-ffunction-sections -fdata-sections`；宿主侧 `src/runtime/*.c` 的 CMake 编译同理；再确认 `--gc-sections` 在非 publish 构建上也开（现在只有 publish/strip 才开）。
- 探针：`_scratch/fsect/`（A/B 两组对象 + `main2.c` + 两个链接产物），`_scratch/sizeprobe/`（hello/bare 的 dev 与 publish 四份产物）。
- 关联：A499（crypto 归档按族再切）、A493（crypto 已归档化）。

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
### [~] A443 `Convert.ToString` 是编译器自造的接口——公开面（stdlib / 内建成员表）里根本没有它
- 追加（2026-10-23，自 A526 移入）：`--api`/`--emit-symbols` 对内建成员覆盖不全——
  `List.Remove` 查不到但存在，与 Convert.ToString 同族（特判实现了、成员表没登记）。

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
### [x] A482 「void 臂的三元表达式」两后端都降成非法 IR（`phi void`）——2026-10-23 更正：宿主同病

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
- **宿主半边已修（2026-10-23，goal round 2）**：`emit_expr_conditional` 在
  两臂值都是 LLVM void 时不建 phi（直接分支汇合返哑值 i32 0）。
  `flag ? A() : B();` 实测 A/done 正常；回归赋值臂（2）、值三元
  （20/2.5/y/pos）全绿。探针
  `_scratch/csaudit/operators/v_void_ternary.zan`、`v_tern_reg.zan`。
- 验证：把 `tests/conformance/nullable_user_type.zan`（void 臂）加进
- **自举半边已修（2026-10-23，goal round 4）**：selfhost `GenConditional`
  在两臂类型都是 void 时不建 phi（直接汇合返哑值 0/int），与宿主同口径。
  复验：gen1 编 `flag ? A() : B();` 通过且 IR 零 `phi void`、clang -c 过；
  值三元（int/string/嵌套条件）回归 clang codegen 通过。因 gen1 产物链接
  需要运行时对象，本条以"编译+IR 合法性"为验（运行语义已由宿主侧证明）；
  差分门用例（nullable_user_type.zan 进 diff_cases.txt）留给闸门批次。
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

### [x] A498 宿主把 `bool` 当**有符号**整数用——`Math.Sqrt(true)` 得 NaN、`Convert.ToInt32(true)` 得 -1
- **宿主侧已修（2026-10-23）**：① `Convert.ToInt32/64/ToInt` 整数路径对 i1 改零扩展
  （`Convert.ToInt32(true)`=1）；② `math_arg_is_numeric` 把 i1 逐出白名单
  （`Math.Sqrt(true)` 编译期拒绝）。两探针复验通过。
- **自举半边已修（随 A508 批次落地，2026-10-23 复核）**：`MathNumBadType` 把
  bool 逐出数值白名单（`Math.Sqrt(true)` 当场拒收，措辞同宿主），Convert 整数
  路径按 bool 零扩展（`Convert.ToInt32(true)`=1）。复验：`Math.Sqrt(true)`
  两侧同报 "Math.Sqrt requires an int or floating-point argument"（列差为
  已知 call-anchor 惯例）、`Convert.ToInt32(true)` 两侧均 1。
- **用例与差分门双闭（2026-10-23）**：
  新增正向 conformance 用例 `tests/conformance/convert_bool_to_int.{zan,out}`（覆写 Convert.ToInt32/64 对 bool 变量与字面量准确输出 0/1），并入自举差分门 `tests/selfhost/diff_cases.txt`；
  新增双端逐字一致负向测试 `tests/selfhost/reject/math_sqrt_bool.zan`，并入 `tests/selfhost/reject_cases.txt`。单测与双端差分门全绿闭账。

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

### [ ] A528 结构体构造函数抛异常时，结构体里 RC 字段的引用没人释放

- 现象：`struct` 的构造函数把拥有型对象存进字段后抛异常，`--check-leaks` 报
  「2 object(s) leaked, allocated at ... [List<int>]」——字段持有的引用既没被
  结构体释放（结构体没有析构），也没被展开器释放。
- 判据（最小复现 `_scratch/struct_field_probe.zan`）：
  - 构造函数在**写字段之前**抛 → 干净（实参里的临时量由展开器释放，这条已修）；
  - 构造函数在**写字段之后**抛 → 每个字段漏一个对象；
  - 类路径同样形状 → 干净（类的对象析构会释放字段）。
- 根因：类构造路径（`src/compiler/irgen_expr.c` 的 `emit_expr_new_expr`）把
  对象推进 EH 临时栈，展开器释放对象时连带释放它的字段；结构体构造路径
  （`src/compiler/irgen_stmt.c`）只在栈上分配结构体（alloca），EH 栈里
  **没有**这个结构体的位置——展开器无从知道该释放哪些字段。
- 方向：展开器的槽类型目前只有 OBJ / STR / DLG / ARR（`ZAN_EH_SLOT_*`），
  都是"释放一个对象"。要兜住结构体字段，得加一种"释放一组字段引用"的槽
  （或让结构体构造路径在栈上留一条展开器认得的清理记录）。这是展开器协议
  级的改动，比实参临时量那一条大不少，单独立项。
- 关联：本轮已修的邻近缺口——构造函数**实参**里的拥有型临时量没登记进 EH
  临时栈，构造函数一抛就泄漏（类路径 `irgen_expr.c` 与结构体路径
  `irgen_stmt.c` 两条都修，conformance `arc_throw_owned_ctor_arg`）。
- 坑出处：结构体没有析构这件事，被类路径"对象析构顺带释放字段"盖住了——
  同一个形状换个载体就漏，测的时候必须把类与结构体分开测。

### [x] A529 内建静态调用结果上的链式成员解析失败（irgen 推断侧）
- **宿主侧已修（2026-10-23）**：`int.Parse("123").ToString()` 曾报 "this call
  could not be resolved to any callable"。根因在 irgen 的 `infer_expr_type_raw`
  对 AST_CALL 的静态内建分支：标量类型（int/long/double/float）是实例内建，
  `builtin_static_class_for` 只映射 string→String，标量上的 native 成员
  （Parse，`native_members[]` 表）推断落空返 NULL——var 接收与比较运算不吃
  infer 才侥幸能用，链上取成员就断。
- 修法：推断分支补 native 成员判定（`zan_builtin_has_native_member`），
  返回标量自身类型，与 irgen_call.c 的原生分派点同口径。
- 实测复验：`int.Parse("123").ToString()`→123、`double.Parse("2.5")`→2.5、
  `long.Parse("77")`→77；`Convert.ToInt32("42").ToString()` 本来就能跑
  （42，Convert 是静态内建有成员表——本条登记时误写它也挂，更正）；
  正例回归 p11/v_parse_ok 全绿。
- **自举半边平账（2026-10-23，goal round 5，实测）**：selfhost 的 Parse
  发射本就返回 `MkType("int")`（irgen_expr.zan Parse 分支），链上 ToString
  正常——gen1 编 `int.Parse("123").ToString()`、`(int.Parse("2147483647")
  + 1).ToString()`（A510 自举半边修后按 32 位回绕）全过，无需改动。
  A512 守卫套件（endptr/越界/软通道）也已在位（另一会话 fe2b8f143）。
- **用例与差分门双闭（2026-10-23）**：新增 conformance 用例 `tests/conformance/builtin_parse_chain.{zan,out}`（验证 int/double/long.Parse 后链式 ToString 正常返回格式化字符串），并入自举差分门 `tests/selfhost/diff_cases.txt`，两端单测与差分双绿闭账。
- 探针：`_scratch/csaudit/numeric/{v_chain2,v_chain5,v_chain6}.zan`。

### 审计复查证伪（2026-10-23，随 A530-535 一批）

- **P1-4 MergeText 二次方：证伪**。两条规模探针（`_scratch/audit/
  mergetext_probe{,2}.zan`）：100k 交替文本/注释游程 81ms；单段文本
  100k 个实体碎片合并 15ms、单节点 100k 字符——`last.text + piece`
  在实测规模下线性，审计的 H 级怀疑不成立，不需要修。
- **P1-5 XXE：证伪**。ReadDoctype 只跳过声明不取 DTD；ReadEntity 仅
  5 个预定义实体 + 数字字符引用，未知实体报错；解析器零网络 I/O。
- **P1-7 裸 AES 无完整性：正向确认 + 注释加固**。库内唯一通用消费方
  AuthToken 用 AesGcm（带 AAD）；裸 CBC/ECB 消费方全部是微信线格式
  互操作（协议层自带 SHA-1 签名）与 GCM 内部构件。Aes 类头补选用
  指南（新代码保密+完整性一律 AesGcm）。
- **P1-8 RFC3161：查无此码**。全仓（src/packages/stdlib/tools/模板）
  无任何 RFC3161/TimeStampReq 实现——原发现指向的代码已不在仓库
  （随模板出仓或在 691 提交中重构掉）；无原始审计上下文不再追。

### [x] A536 `T? ?? T` 在 merge 块先发负载提取再建 phi——LLVM 校验失败（存量缺陷，批次回归照出）
- 现象：`types/p07_nullable.zan`（`int y = x ?? 5;`）报 "LLVM verification
  failed in main: PHI nodes not grouped at top of basic block!"——批次回归
  （A510 的六域全扫）照出；用 A482 前后的 irgen_expr.c 双向对拍确认是
  **存量缺陷**（与 A510 收窄无关）。
- 根因：`emit_null_coalesce` 的 `T? ?? T` 分支在定位到 merge 块**之后**才调
  `nullable_get_payload`——提取指令（gep/load）落在 coal.end 块顶、phi 之前，
  违反 phi 分组约束。
- 修法（同 commit 356a15882）：负载提取上提到 left_bb（分支前，左值必然
  在场）；merge 块只建 phi，右臂宽度差用 coerce_int_to（静态门槛已保证
  同型，防御路径实际不可达）。
- 复验：p07 全断言通过（"OK p07"）；p03/p04 不受影响。
- 待办：自举半边（selfhost 的 ?? 降级路径同查）；`bool?`/`T? ?? T?` 聚合
- **自举半边·int?.ToString() 解箱（2026-10-23，goal round 6 收口）**：
  qq1 链式探针暴露第三段缺口——标量无参 ToString 按 sval 匹配，可空盒
  （sval=int, op=1）直落 `snprintf` 的 i64 洞（`i8* %t43`）。在标量
  ToString 分支顶部对可空接收者先解箱再格式化。null 接收者的 ToString
  未加守卫（宿主 NRE 口径，软通道另项）。复验：qq1 + p07 全套 gen1 出
  IR、clang -c 双双落 obj。
- **自举半边·提升运算/提升相等补齐（2026-10-23，goal round 6 续）**：QQ
  解包放开编译后，p07 全套经 gen1 暴露出**被挡住多年的两段缺口**：
  `x = x + 3`（int? 提升算术）曾把盒指针喂给 `add i64`（clang
  "'%t45' defined with type 'ptr' but expected 'i64'"）；`bool? == true`
  曾把盒指针喂给 `icmp eq i64`。补齐 GenBinary 顶部提升算术（null 传播
  分支 + 负载运算 + BoxVal 重装箱）与 EqEq/BangEq 提升相等（null 字面量
  快径：`b == null`→!Has、`b != null`→Has；双可空：Has∧Has→负载比较）。
  修三处自伤（Node/string 类型、icmp 第二操作数带类型前缀、phi 前驱写成
  merge 块自身致 LLVM ISel 0xC0000005）。复验：p07 全套 + qq1 经 gen1 出
  IR、clang -c 全过（obj 落盘）。
- **自举半边·T? ?? T 解包已修（2026-10-23，goal round 6）**：同族排查
  发现自举的真缺口不在 phi 分组（其 QQ 路径本就在分支内惰性求值、merge
  只建 phi）而在**定型**：checker 无 QQ 分支，InferType 退化取左支 int?，
  `int y = x ?? 5;` 被 assignability 当场拒收（宿主 p07 一线）。修法：
  checker InferType 补 QQ 分支——T? ?? T 返回负载类型并在节点 e.ival 钉
  负载形态标记；发射端按标记走解箱 phi（非空臂 bitcast+load 负载，空臂
  右支惰性求值，phi 按负载型）。`T? ?? T?` 聚合形态维持原 phi（两臂都是
  i8* 盒，定型左支，天然正确）。复验：gen1 编 `x??5`（x=null→5）、
  `z??5`（z=7→7 走 malloc 盒+解箱）、链式 `z ?? (x ?? 3)` 全过，IR 为
  惰性分支 + 负载 phi。
  路径同样只建 phi 未动，留观。

### [x] A538 自举 parser 不支持多字节字符字面量 `'中'`（词法层 unexpected）
- 现象：gen1 编 `char z = '中';` 报两行 "syntax error: unexpected ''"——
  源文件是 UTF-8，自举 lexer 按字节扫描把多字节序列当非法字符。宿主
  zanc 同文件正常（A523 探针含 '中' 编码路径）。
- 影响：含中文/emoji 字符字面量的程序自举编不过（字符串字面量不受影响，
  已按 UTF-8 透传）。
- 修复（2026-10-23，commit e85426638）：C 宿主 lexer.c 与自举 lexer.zan 同步
  支持 1~4 字节 UTF-8 序列解码为 Unicode 标量码点；顺带修复自举 irgen_expr.zan
  二元加减重复求值致 fixedpoint 超时假死；conformance `case_lexer_char_multibyte`
  与 `selfhost_fixed_point` 全过。

### [x] A539 Convert.ToInt32/ToInt 整数实参溢出静默回绕（审计 P1-12）
- 现象：字符串路径 A512 已有越界软诊断，整数路径漏了——`Convert.ToInt32(2147483648L)`
  静默截断回绕成 -2147483648（C# 语义：OverflowException）。
- 修复（irgen_call.c ToInt* 内建整数分支）：i64 实参 + ToInt32/ToInt 时
  补与字符串路径同判的范围谓词（<INT32_MIN || > INT32_MAX）→
  emit_runtime_check 软诊断 + 安全值 0（--strict-runtime 下 exit 70）；
  ToInt64 全宽不查；窄整型 sext 后必在范围不查。
- 复验：四值探针（上越界/下越界/int 上下边界/ToInt64 宽值）ALL-OK，
  软诊断两行 stderr；conformance `case_convert_overflow` 过。
- ToUpper/ToLower 非 ASCII 原样返回：A525 已声明 ASCII 口径（已知偏差），
  Unicode 简单大小写映射留待独立件（大表，C# 复刻线）。
- **ToUpper/ToLower Unicode 简单映射已补齐（2026-10-23，A525 闭环）**：
  按 .NET Invariant 生成 1:1 简单映射表（1406 上转对，1389 下转对，覆盖 BMP
  与 astral 域如 Deseret，扩张型 ß 与无映射码点原样返回）；irgen 惰性发射模块内
  全局表与二分查找助手（首次调用才发射，不用的程序零开销）；conformance
  `case_char_case_unicode` 全过。

### [x] A540 编译器 pull-in live-name 闭包：包内控制器不进编译图 → 路由静默 404（已闭环，2026-10-23）
- 现象：server-collab 模板按 README 配方（`zanc src/main.zan src/**/*.zan
  --auto-stdlib`）编译，程序能编译、能启动、静态资产正常，但**管理后台的
  系统管理屏全部 404**：`/admin/system/users|roles|departments|dicts|logs|
  settings|docs` 既不在路由表里，`/api/docs.json` 也没有这些端点；侧栏里
  没有入口，看起来像「屏没做」。
- 根因：属性路由与代码生成器只扫描**显式列出的**源文件。纯 `using` 拉入
  路径下，包文件要进编译单元元数据得同时满足两个条件——命名空间目录被
  访问（`pi_reach` 只由 `using` 触发）、类型名被按名引用（`pi_flag_istr`）。
  MVC 包的控制器恰是靠属性发现、从不被按名引用：`ZanWeb.Admin.System` 命名
  空间没有任何文件 using，目录没被访问，控制器连候选都不是。
  `packages/Zan.Mvc/README.md`「在应用中使用本包（组合根）」已写明同一件事
  与当前配方（包源与应用源一起显式列出）。
- 连带缺口：ORM 表访问器（`this.SysDepartment` 这类）按 DAO 类型是否存在
  合成，而 `SysDepartmentDao` / `SysDictTypeDao` 等 DAO 在全仓无人按名引用
  → DAO 不进图 → 访问器缺失 → 控制器报「has no member 'SysDepartment'」。
  同理 `JobsController` 的基类 `CrudScreenController`（在 `ZanWeb.Admin`
  命名空间）不在图里，报「undefined base type」。
- 闭环方案（2026-10-23）：
  1. `src/compiler/main.c` 中 `stdlib_has_dir` 对 `System` 前缀命名空间直接返回 1，
     修复 `System.Data` 迁出 stdlib 导致其被误判为层级包的问题，保证 `using System.Data;`
     精确匹配、绝不层级拖入 Postgres/Firebird/ssl 驱动；
  2. `src/compiler/package.c` 中 SDK 包店（`exe_dir/../packages`）在 `zan_pkg_source_index_create`
     中标记为 `project_owned = 1`，使 SDK 包（如 `Zan.Mvc`）天然支持祖先命名空间登记，
     全局店保持 `project_owned = 0` 以维系全局包隔离测试契约；
  3. `src/compiler/main.c` 中 `pi_scan_file` 扩充控制器与代码生成目标识别：属性识别新增
     `[Custom]`；类名/结构体结尾为 `Controller` 或 `Dao` 识别为生成器实体；基类以
     `Controller` 结尾或为 `BaseRepository`/`BaseDao` 均识别为控制器；
  4. `src/compiler/main.c` 中 `pi_seed_chain` 支持 `AST_THIS_EXPR` 并在识别到大写实体访问时
     （`this.SysDepartment`）自动 flag 对应的 `<Name>Dao`，使 GenDb 生成器与 DAO 闭环；
  5. 验证：`server-collab` 纯目录编译 `build\zanc.exe templates/server/server-collab/src --auto-stdlib`
     全量编出 626 文件且路由与代码生成器全部上线；`unit_package_lock` 与 `unit_package_namespace`
     单元测试 100% 通过；`using System.Data;` 探针仅编译 33 文件，零驱动泄露。
- 现状绕法（本次已落地）：模板 README 的构建配方改为显式列出 MVC 包源
  （`packages/Zan.Mvc/src/ZanWeb/**/*.zan`）。实测系统管理屏全部上线：路由
  表出现 /admin/system/* 全族，账号可建可登录、角色可授。

### [x] A541 IDE smart inputs 每个命名空间只取一个文件 → IDE 构建的 MVC 应用路由不全（已闭环，2026-10-23）
- 现象（按 `src/ide_zan/src/shell/ZanIDE.Workspace.zan` 的代码读）：IDE 的
  SmartInputs 对每个 `using` 命名空间只找一个声明该命名空间的文件
  （`FindNamespaceFile` 返回首个命中即停），包里同一命名空间有多个控制器
  时只进了一个 → IDE 构建的 MVC 应用只有部分管理台路由，而命令行显式列
  包源构建则齐全（同一份源码，两种构建产物行为不同）。
- 闭环方案（2026-10-23）：
  1. `src/ide_zan/src/shell/ZanIDE.Workspace.zan` 将 `FindNamespaceFile` 改为
     `FindNamespaceFiles(dir, ns, entry, skip, outp)`，向候选列表收集该层级下声明
     目标命名空间的所有非 Main 文件，而非单文件即停；
  2. `WithUsedModules` 循环遍历 `FindNamespaceFiles` 收集到的全部文件，逐个追加到构建入参
     并递归收集其 `using` 依赖（安全上限从 32 放宽到 64）；
  3. 验证：`pwsh -File scripts/build_ide.ps1` 编译构建通过（生成 `build\ZanIDE.exe`）。

### [ ] A542 Panel.Column/Row 的流式轴只对 With() 生效：Add() 的子项全叠在容器原点
- 现象：代码建的纵向面板（资料卡、设置页导航列、工作台详情卡、聊天抽屉、
  资料名片）里，多个子项全部叠在容器左上角同一处——头像压在按钮上、六行
  键值挤成一条、两个小节标题跑到卡片顶端。布局自检不报（重叠的兄弟矩形
  相同但都在同一容器原点），只有像素截图看得出来。
- 根因：`Panel.Column()`/`Panel.Row()` 的流式轴由 `With()` 兑现——`Control.With`
  会把非 grow 的子项改成沿流式轴停靠（列→dock=1，行→dock=3），于是子项在
  停靠排布里依次往下/往右排。`Control.Add()` 不做这层归一化：子项 dock 仍是
  0（手摆），停靠排布里 dock==0 的子项一律排在容器原点（`Control.zan` 约
  2924 行的 `c.Arrange(cx + c.mx + ml, cy + c.my + mt, ...)`），于是全部重叠。
  `Control.Add` 的注释却写着「追加一个子节点（自动停靠）」，与实现不符——
  `Children = { ... }` 集合初始化器走的也是 `With`（`ControlChildren.Add`
  → `owner.With(c)`），只有显式 `Add` 落回手摆语义。
- 已落地绕法（本次）：gui-wechat 模板的代码建树统一改用 `Flex.Column()`
  （+ `AddClass`，`Class =` 会把方向类冲掉）+ CSS `grow` 类吃满剩余空间；
  框架自带的 `Panel.Column` 仍保留（`With` 路径照旧可用）。
- 方向（候选，需带 IDE 截图验证）：
  ① 框架侧让 `Add()` 对流式容器（`layout != 0`）也归一化停靠——风险是设计
     器自由画布：设计稿里的面板同样是 `layout=column/row`，子项靠 `mx/my`
     手摆，一旦被自动停靠，设计器画布会散架；这条路要先给设计器容器单独
     标记（例如 `style=layout` 且 `layout=column` 的语义区分）。
  ② 或让 `Panel.Column/Row` 直接成为 flex 容器（`StyleDisplay()` 返回 1，
     主轴方向取自 `layout`），但 `Control.zan` 里 `computedStyle.flexDir` 有
     多处直读，需要新增可覆写的「主轴方向」取值再逐处替换，影响 IDE 与全部
     模板。
  ③ 无论走哪条，都要把 `Control.Add` 的注释与实现对齐（现在文档承诺「自动
     停靠」，实现只对 `With` 兑现），否则每个写代码建树的人都会踩同一个坑。

### [ ] A543 读 Binding<T> 字段当 T 用：拿到的是常量槽，live 绑定的当前值读不出来
- 现象：聊天抽屉里点头像弹出的「资料名片」没有名字（昵称位置空白，性别符
  挨着头像），视频号名显示成「的分享」（= 空名 + "的分享"）。控件自己渲染
  出来的文字却是对的（抽屉标题「冰冰」正常）。
- 根因：`Label.Text` 是 `Binding<string>` 字段。赋模型字段左值
  （`pName.Text = s.name;`）会被编译器降级成 **live 绑定**（每帧回读模型
  字段），其余表达式降级成常量绑定（`stdlib/System/Binding.zan` 的 `Get()`：
  live 走 getter，const 走 `constVal`）。控件渲染走 `Text.Get()`，拿到的是
  live 的当前值；而把 `pName.Text` 直接当 `string` 读（隐式降解）拿到的是
  绑定对象的**常量槽**——live 绑定没有常量，恒为空串。于是「读出来传给
  回调」这一步把名字丢了。
- 已落地绕法（本次）：取值改走控件的取值接口 `pName.GetText()`（或
  `Str()`）；另外把会话字段先快照进局部量再赋给控件，避免 live 绑定在会话
  对象被换掉后指向悬空。
- 方向（候选）：编译器侧把「`Binding<T>` 字段读到 `T` 上下文」降解成
  `Get()` 调用（而不是常量槽），让读与渲染同源；需要确认这条隐式降解在
  类型检查（`checker_type_assignable` 允许 `Binding<T>` 赋给 `T`）与 irgen
  两侧的落点一致，并补一条 conformance。
### [ ] A544 子窗口（聊天记录窗/独立聊天窗/设置窗）在本会话无法像素验收
- 现象：`ZAN_UI_SCRIPT` 的 `dump pixels` 只抓**主 App** 的窗口；子窗口
  （`Gui.ChildWindow`：聊天记录窗、独立聊天窗、设置窗、通讯录管理窗）是
  各自的顶层窗口，抓不到。改用屏幕抓取（CopyFromScreen）在本会话拿到的是
  全黑桌面——没有可交互的桌面会话。
- 绕法（已落地）：把窗口**内容**（视图）塞进独立宿主 Form 再抓像素。设置窗
  （`WxSettingsView`）与通讯录管理窗（`WxContactManagerView`）用这招验过，
  像素正常（导航列、正文、底栏各归各位）。聊天记录窗与独立聊天窗把 root
  建在 ChildWindow 类内部，视图接口没暴露，暂用这招。
- 待查（从聊天记录窗那次运行的 `ZAN_GUI_LAYOUTLINT=1` 日志里冒出来的 6 条
  「裁剪」命中）：`ToolStrip` 里的 `Button[506,201 0x28]`（宽度 0，装不下
  28x28 图标）与 `Button[759,301 0x42]`，以及聊天页骨架里的图标钮
  `Button[453,46 23x28]`、`Button[484,46 24x28]`、`Button[679,69 35x42]`、
  `Button[726,69 36x42]`。图标钮按 skill 规矩应挂 `.FreeLayout` 免检；
  ToolStrip 的 0 宽项要确认是「ToolStrip 自绘、按钮只是代理」的误报还是
  真缺陷（聊天页输入区工具条同样是 ToolStrip，十页重跑时 0 命中）。
- 前提：要有可交互的桌面会话（锁屏/远程会话会拿到黑屏），或让子窗口支持
  把 root 暴露给宿主驱动。