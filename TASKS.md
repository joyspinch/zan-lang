# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：`[ ]` 未完成 · `[~]` 进行中 · `[-]` 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 `[x]` 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出

## 未完成

### [ ] A452 一元运算路径的 `type_error` 哨兵不发诊断——`-s` 落到 LLVM 校验失败，`~s` 把编译器打崩

- 现象（2026-10-21 实测，探针见 `_scratch/probe_unary_types.ps1`）：
  - `string s = "x"; Console.WriteLine(-s);` → rc=1，报的是
    `LLVM verification failed in main: Integer arithmetic operators only work with
    integral types!`——不是人话诊断，用户完全看不出是自己写错了。
  - `string s = "x"; Console.WriteLine(~s);` → **编译器自己崩**（退出码
    0xC0000005 访问冲突）。
  - `string s = "x"; Console.WriteLine(+s);` → rc=0 **静默通过**（恒等，输出字符串）。
  - 用**不含**本轮改动的共享 `build/zanc.exe` 复测 `~s` 同样崩 → **既有缺陷，
    不是本轮引入**。
- 根因：`src/compiler/checker.c:2446-2474` 的 `AST_UNARY` 分支对非数值操作数
  只 `return c->binder->type_error`，而这个哨兵**本身不发诊断**，调用点也没补
  "invalid operand type" → 错误一路漏进 irgen：
  - `-s` → 对字符串指针做整数减法 → LLVM 校验失败；
  - `~s` → 对字符串指针做 xor → 生成非法 IR 的过程中就把编译器打崩；
  - `+s` → `src/compiler/irgen_expr.c:4452` `case TK_PLUS: return operand;`
    恒等 → 静默通过。
- 影响：写错一个一元运算符，用户拿到的是"编译器崩了"或 LLVM 内部报错，而不是
  "cannot apply unary '-' to 'string'"。
- 方向：checker's `AST_UNARY` 分支对非数值 / 非类结构体操作数**就地发诊断**
  （`zan_diag_emit`），不要只返回 `type_error` 哨兵。自举侧 `src/selfhost/
  checker.zan:250-253` 同样无条件 `return InferType(e.a)`，且 `GenUnary` 对
  字符串会生成 `sub i64 0, <指针>` / `xor i64 <指针>, -1` → 两边都要补守卫，
  否则差分门分叉。
- ⚠ 跨后端语义改动，必须同一次提交落地两边的守卫；`src/selfhost/irgen_expr.zan`
  此刻正被另一会话在途修改，改 checker 侧不碰它可以先做，但要重跑差分门。

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

### [ ] A446 `packages/Zan.Sdk.Wechat` 引用了不存在的枚举 `WechatMpMediaType`

- 现象（2026-10-21 全包扫描实测）：`packages/Zan.Sdk.Wechat/src/Sdk/Wechat/Mp/
  WechatMpMediaSpecialApi.zan:12` 与 `.../Models/Mp/WechatMpSpecialProtocol.zan:10`
  都把 `WechatMpMediaType` 当类型用，但**全仓没有任何地方声明它**
  （`rg "enum WechatMpMediaType"` 零命中）。编译这两个文件报
  "undefined type 'WechatMpMediaType'"。
- 影响：该包整体编译不过；同包的其它文件也引用不到这个类型。
- 之前没被发现的原因：`scripts/pkg_sweep.ps1` 把文件列表直接摊在命令行上，
  `Zan.Sdk.Wechat` 有 5618 个文件 → 超出 Windows 命令行长度上限 → **进程根本没
  起来**，而脚本报错文本里没有 ": error"，于是被判成 **green**——一个从未被编译
  的包看起来是健康的。脚本已改用**目录输入**（编译器自己递归展开 `*.zan`，
  命令行恒长）并把"启动失败"判成 RED（见同批提交），但这条枚举缺失仍待补。
- 方向：按微信素材管理接口的取值补一个枚举（image / voice / video / thumb 等），
  或把这两处的类型换成包内已有的实际类型——需要按微信 API 文档核对语义，
  属于包内容工作，不在本轮编译器修复范围内。

### [ ] A445 自举后端 `Math.*` / `Convert.ToInt32` / `Convert.ToDouble` 与 C 宿主同款缺陷

- 现象（2026-10-21 随 C 宿主的类型守卫一并查实）：
  - `src/selfhost/irgen_expr.zan:3884` `ToDbl`：实参不是 double 就一律
    `sitofp i64 <v> to double`。类实例是**指针** → 造出 `sitofp i64 i8*`，
    LLVM 校验失败（非法 IR）。`GenMathCall`（同文件 `:3897`）的所有分支
    （Sin/Cos/Tan/Log/Exp/Atan/Asin/Acos/Sqrt、Pow/Atan2、Floor/Ceiling、
    Abs、Max/Min）都走 `ToDbl` → `Math.Sqrt(<类实例>)` 在自举后端同样是非法 IR。
  - 同文件 `:4094` `Convert.ToDouble`：实参既不是 string 也不是 double 时，
    同样落到 `sitofp i64 <v> to double` → 同款非法 IR。
  - 同文件 `:4107` `Convert.ToInt32` / `ToInt`：无条件
    `call i64 @atoi(i8* <a.op>)` —— 类实例的**对象指针**被当成 C 字符串交给
    `atoi`，运行期读对象字节当文本，静默得到 0（与 C 宿主修复前
    `strtoll` 读对象首字节同款）。
- 影响：同一份源码，C 宿主现在给人话诊断，自举后端仍是编译期 LLVM 校验失败
  或运行期读对象内存。差分门（`tests/selfhost/diff_cases.txt`）目前是绿的——
  现有用例没有覆盖"给 Math/Convert 喂类实例"。
- 方向：与 C 宿主同款守卫，按**静态类型**分流：类 / 结构体 / 数组 / 委托 →
  报与宿主一致的诊断；字符串 / 数值走原有路径。守卫要落在 `ToDbl` 与
  `Convert.*` 的**分派入口**，不要只补 `GenMathCall` 的某一个分支。
- ⚠ `src/selfhost/irgen_expr.zan` 此刻正被另一个会话在途修改，本轮无法同一次
  提交落地，等窗口。
- 相关：A443（`Math` / `Convert` 公开面缺失）、A444（`Convert.ToString(object)` 残留）。

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
### [ ] A442 自举后端 `Convert.ToString(<引用类型>)` 输出指针地址——与 C 宿主的诊断不一致

- 现象（2026-10-20 随 A441 一并查实）：C 宿主已改成对引用类型 fail-closed
  （报 "Convert.ToString: cannot convert 'Box' to a string"），自举后端仍走
  `src/selfhost/irgen_expr.zan:4036` 的 `Convert.ToString` 特判 → `ToCStr`
  （同文件 `:933`）→ 非字符串/非数值就落进 `snprintf("%lld", ptr)`，**把对象
  指针当整数打出来**（例如 "140737488xxxx"）。
- 影响：同一个源文件，两个后端一个报错、一个输出地址。地址泄露本身也是
  信息泄露（ASLR 绕过），且用户拿到的是无意义的输出。
- 方向：`Convert.ToString` 特判里按**静态类型**分流——引用类型与结构体报
  与宿主一致的诊断；字符串/数值走原有路径。⚠ 与 A441 的 bool 分支、
  A443 的 stdlib 声明是同一处代码，**建议三件事一起做**（都在
  `src/selfhost/irgen_expr.zan` + `src/compiler/irgen_call.c` 两处）。
- 现状：C 宿主那侧已经修好（提交见 git 历史）；自举侧等窗口。

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
### [ ] A441 布尔值→字符串的渲染有三条口径：`true` / `1` / `x1`

- 现象（2026-10-20 探针 `_scratch/bool_str.zan`，C 宿主 `build/zanc.exe` 实测）：
  ```zan
  bool b = true;
  Console.WriteLine(b);                 // true
  Console.WriteLine(b.ToString());      // true
  Console.WriteLine(Convert.ToString(b)); // 1    ← 不一致
  Console.WriteLine("x" + b);           // x1    ← 不一致
  Console.WriteLine($"{b}");            // true
  ```
  同一个 `bool`，三条路径给出三种字符串。
- 根因：**没有统一的「bool→字符串」内建**，每条路径各写各的：
  - `src/compiler/irgen_call.c:1878-1924`：`Convert.ToString(x)` 的编译器内建特判
    里**没有 bool 分支**——bool 落到末尾的 `emit_itoa_into`，按整数格式化 → `"1"`。
  - `src/compiler/irgen_generics.c:2439-2491` `emit_to_cstr_of`（字符串拼接用的
    取串路径）：同样没有 bool 分支，落到 `emit_to_cstr_u` → 整数格式化 → `"1"`。
  - `src/compiler/irgen_call.c:1074-1081`（`Console.WriteLine`）与
    `src/compiler/irgen_expr.c:6453-6461`（插值）：**有** bool 分支 →
    `select "true"/"false"`。
  - 自举后端同病：`src/selfhost/irgen_expr.zan:933-956` `ToCStr` 没有 bool 分支
    → `"1"`；但同文件 `4273-4274` 的标量 `ToString` 有 bool 分支 → `"true"`。
    所以**两个后端当前输出一致**（都是 "1"），差分门是绿的——这不是后端分歧，
    是两边各自都有的语义缺口。
- 影响：`Convert.ToString(b)` 与 `"x" + b` 拿到的不是 `"true"`；按字符串比对、
  落库、拼消息的代码会踩坑，而且**同一个值在不同上下文里长得不一样**。
- 方向：统一到 **`true` / `false`**（与已有的 `ToString()` / `Console.WriteLine`
  口径一致）。C 宿主侧已有现成的 bool→串内建
  `emit_value_as_cstr`（`src/compiler/irgen_generics.c:2022-2038`，内部已做
  `select "true"/"false"`），照它补分支即可：
  1. `src/compiler/irgen_call.c:1878` 的 `Convert.ToString` 特判：加 bool 分支，
     复用 `emit_value_as_cstr` 的 `select "true"/"false"` 写法。
  2. `src/compiler/irgen_generics.c:2439` `emit_to_cstr_of`：加 bool 分支
     （拼接路径），可用 `expr_is_bool(g, ast, locals)` 判型。
  3. 自举后端 `src/selfhost/irgen_expr.zan:933` `ToCStr`：加同样的 bool 分支。
- ⚠ 3 步必须**同一次提交**落地：只改 C 宿主会让差分门
  （`tests/selfhost/diff_cases.txt`）打红——两个后端当前都输出 "1"，改一边就分叉。
- 影响面：改完要 grep 全部用 `Convert.ToString(<bool>)` / `<bool>` 参与拼接的
  用例与 golden（现有 golden 已把 "1" 固化在部分用例里）。
### [ ] A393 自举调用 `async void` 方法不启动协程——方法体根本不执行

- 现象（2026-10-20 差分门探针实测，探针在 `_scratch/sd/p_asyncvoid.zan`）：
  ```zan
  static async void Main() { Work(); await Task.Delay(50); Console.WriteLine("done"); }
  static async void Work()  { await Task.Delay(1);  Console.WriteLine("work"); }
  ```
  - C 宿主：`work` 然后 `done`——**方法体会执行**（async Main 的调度器在跑）。
  - 自举：只有 `done`——**方法体被整个丢掉**，`Work` 当普通方法调用。
  - 同步 `Main` 里调 async void 方法时，两边都不执行（C 宿主也没有调度器）。
- 根因：自举后端在**调用点**没有区分 async 方法——没有生成"分配帧 + 挂起 +
  调度"的启动序列，只有 async `Main` 自己是被特殊处理的（见 0f063c2e）。
- 方向：调用点遇到 async 方法时，按 `GenAsyncMethod` 的 ramp 形态生成协程启动
  （分配帧、填 resume/cleanup 指针、交给调度器），而不是直接 `call`。
- ⚠ 与 async 帧布局耦合（A385），改完必须重跑 `selfhost_fixed_point` 与差分门；
  另外 async 方法的返回类型 `Task<T>` 自举还不认识（`unknown type 'Task'`），
  只能先覆盖 `async void`。
- ✅ 已修（2026-10-20，提交 `c5421633`）：调用点新增 `Irgen.EmitAsyncDetach`，
  语句位置的 async 调用把帧交给 `zan_co_ready(frame, resume)`（resume 入口从**帧槽 5**
  读，ramp 写在那儿）。「真 async 调用」用 `ExprVal.asyncCall` 标记区分，**不能**
  用「类型是 `__task`」——降级路径也带 `__task` 类型但值是常量 0，detach 它会生成
  `bitcast i8* 0 to i64*`。用例 `tests/conformance/async_void_call.zan` 钉住。

### [ ] A394 自举在**同步**上下文里遇到 `await` 不 fail-closed——生成读地址 0 的非法 IR

- 现象（2026-10-20 探针实测，探针在 `_scratch/sd/p_capparam.zan`）：
  ```zan
  static void Main() {            // 没有 async 修饰，但函数体里有 await
      A1(3);                       // A1 是 async void
      await Task.Delay(80);
      Console.WriteLine("done");
  }
  ```
  - C 宿主：把 `Main` 当协程处理，程序正常跑完。
  - 自举（HEAD 与修复后都一样）：`Main` 仍按**同步**方法生成，但 `await` 落到
    `EmitAwait` 的非 async 分支，它把 await 的操作数当**协程帧指针**用：
    `bitcast i8* 0 to i64*` → 读地址 0 的槽 5 → clang 直接报
    `integer constant must have integer type`（链接失败）。
- 根因：自举的 await 降级假设「被 await 的东西是**协程帧**」（读帧的槽 5 拿
  resume 入口）。`Task.Delay` 是 C 宿主的**内建**（`builtin_api.c`），自举不认识
  它 → 调用**静默降级成常量 0** → `bitcast i8* 0 to i64*` → 非法 IR。
- 方向（**注意：原方向写错了**）：不是「同步方法里的 await 一律拒绝」——
  **同步方法里 await 一个 async 方法是合法的**（C 宿主也接受，自举的
  `tests/selfhost/prog1.zan` 就靠 `int av = await P.AsTwice(21);` 跑出 42：
  同步上下文会把被 await 的协程跑完再取结果）。真正要 fail-closed 的是
  **被 await 的操作数不是协程帧**。
- ✅ 已修（2026-10-20）：`EmitAwait` 检查操作数的 `ExprVal.asyncCall` 标记——
  不是协程帧就报诊断（"'await' expects an async call in the self-hosted
  compiler"），不再生成非法 IR。配套：检查器拒绝 **async 方法里 lambda 内部的
  await**（lambda 不是协程，在协程里跑同步 await 会重入调度器）。
  回归门：差分门新增 **reject 列表**（`tests/selfhost/reject_cases.txt` +
  `tests/selfhost/reject/`），这类"必须拒绝"的程序由门断言"非零退出且不落 IR"。
- 遗留（另立）：自举不认识 C 宿主的内建方法（`Task.Delay` / `Task.Yield` /
  `Math.*` …）时**静默返回 0**——这是"能力缺口必须 fail-closed"的另一处违反，
  要单独做（要么给自举一份内建表，要么对认不出的调用报诊断）。
- ⚠ 这条与 A393 无关：A393 修的是 **async 方法的调用点不启动协程**；
  两者都在 async 路径上，但根因不同，修完都要重跑 `selfhost_fixed_point`
  与差分门。

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

### [ ] A390 自举的「数组 → string」是复制打包，不是共享 view

- 现象（2026-10-20 修完 string 长度语义后实测）：
  - `byte[] raw; string v = raw;` 在自举里是**复制**（把每个元素的低 8 位
    拷进新的托管串），C 宿主是**共享 view**（改 `raw[0]`，`v[0]` 跟着变）。
    原因：自举的数组是「每元素一个 8 字节槽」，不是紧凑字节，没法直接
    当字符串指针共享。
- 方向：
  (1) 数组 → string 要真正共享，得让自举的 `byte[]` 变成紧凑字节布局
      （每元素 1 字节）——这是数组表示级改造，会波及所有数组代码；
  (2) 或者承认自举的数组是 8 字节槽，把「共享 view」在文档里记为**已知
      语言分歧**，只保证长度与内容正确。
- ⚠ 动数组布局会动自举固定点的 gen2 运行时包，改完必须重跑
  `selfhost_fixed_point` 与差分门。

- ✅ 已修（2026-10-20，含 NUL 的子串搜索语义）：原方向 (3)「含 NUL 的子串
  搜索改成按长度比较（memcmp + 显式长度）」是**错的**——先实测 C 宿主才发现
  它的搜索/变换类方法用的是 **C 字符串语义**（截断于第一个 NUL）：
  `"AB\0CD".Trim().Length == 2`、`.EndsWith("CD") == false`、
  `.LastIndexOf("C") == -1`、`.LastIndexOf("") == 2`、`.ToString().Length == 2`；
  而 `Length` / 索引 / `Substring` / 拼接 / `==` 才是精确语义
  （`Length == 5`、`Substring(2,2).Length == 2`、`(h+"X").Length == 6`）。
  自举原来按精确长度做搜索，**反而**与 C 宿主分歧（`EndsWith` 判 true、
  `LastIndexOf` 判 3）。已改为与 C 宿主一致：新增运行时 `zstr_clen`
  （= `strlen`，带 magic 校验），`Trim` / `ToUpper` / `ToLower` / `Replace` /
  `Split` / `ToString` / `EndsWith` / `LastIndexOf` / `StartsWith` 全部改用
  C 可见长度；用例 `tests/conformance/string_nul_search.zan` 钉住。

### [ ] A395 闭包不捕获外层方法的接收者 `this`——实例字段在闭包里一律读成 0

- 现象（2026-10-20 探针实测，探针在 `_scratch/sd/p_this.zan`）：
  ```zan
  class Counter { int n; int[] buf;
      public Func<int> GetN() { return () => n; } }         // C 宿主 n=3，自举 n=0
  ```
  - C 宿主：`n=3` / `len=3` / 嵌套闭包 `p=8`；
  - 自举：`n=0` / `len=0`（嵌套闭包因捕获的是**字段**同样读 0）。
- 根因：自举 IR 后端在闭包体里把 `curHasThis` 置 false、`curClass` 置空，
  于是闭包里的裸标识符既不是局部变量、也够不到实例字段 → 直接返回常量 0。
  C 宿主（`checker.c` 的 `expr_captures_this`）与 ngen 后端
  （`ngen_delegate.zan` 的 `DelegateCaptureNameNg("this", …)`）都捕获 `this`，
  只有自举 IR 后端漏了。
- 方向：env 多一个槽存接收者；闭包体入口还原成 `%this` 并登记名为 `this` 的局部
  （嵌套闭包继续捕获它）；闭包体恢复 `curHasThis`/`curClass`。
- ✅ 已修（2026-10-20）：`CapturesThis` / `LambdaNeedsThis` / `ThisReceiver` /
  `ReceiverAvailable`（`irgen_expr.zan`）。判据照抄 C 宿主：不是局部变量且是本类
  的实例字段/实例方法才需要 `this`，且**要递归进嵌套闭包**（否则外层不捕获、
  内层引用未定义的 `%this` → 非法 IR）。用例
  `tests/conformance/closure_captures_this.zan` 钉住。
- 边界：接收者按**借用**语义处理（闭包体入口 retain、出口 release，净值为 0），
  与自举既有的"捕获局部靠外层帧持有引用"一致；闭包在方法返回后才被调用时，
  接收者可能已经没了（C 宿主靠闭包持有强引用保活）。

### [ ] A396 委托调用的实参在分支臂里求值——clang 的 X86 指令选择器崩溃

- 现象（2026-10-20 差分门实测）：`value_enumerable` / `linq_query` 两个用例
  编译到 IR 没问题，但 clang 在
  `Running pass 'X86 DAG->DAG Instruction Selection' on function '@__lambda_3'`
  阶段 0xC0000005 崩溃（LLVM 19.1.5 与 mozbuild LLVM 20.1.8 都复现，IR 通过验证器）。
- 根因：`EmitDelegateCall` 的实参在"堆闭包 / 裸函数指针"两条**分支臂里**求值，
  于是「实参本身是委托调用」时，内层调用的分支+phi 被塞进外层分支的臂里——
  这个 IR 形状（phi 的前驱块里还有 phi）触发 clang 的 ISel 崩溃。
- 方向：实参先求值成临时值，再分支；两条路复用同一批临时值。
- ✅ 已修（2026-10-20）：实参提到分支之前求值。顺带修掉"实参被求值两次"。
  用例 `tests/conformance/closure_nested_call.zan` 钉住；两个 GAP 随之闭账。
- 排查法：clang 在 ISel 崩时，把出问题的函数单独抠出来编译，再逐块删指令二分。

### [ ] A397 闭包的 fn 槽是 `i8*`：直接当函数指针调是非法 IR

- 现象：闭包记录是 `{i8* fn, i8* env}`，`EmitDelegateCall` 取出的 `fn` 是
  `i8*`，却直接 `call i64 (i8*, i64) %fn(...)`——被调值类型与函数指针类型不符，
  **非法 IR**。clang 对 `.ll` 输入默认 `-disable-llvm-verifier`，非法 IR 被静默
  送进后端。
- 方向：调用前 `bitcast i8* %fn to <函数类型>*`。
- ✅ 已修（2026-10-20）：闭包路径与裸函数指针路径都先 bitcast（虚方法槽那条路径
  本来就是对的，两处要一起看）。

### [ ] A399 自举：`T[]`（泛型数组）的字段槽两端类型不一致——存端打包成 i64、读端按 i64*

- 现象（2026-10-20 探针实测，探针在 `_scratch/sd/p_enum3.zan`）：
  ```zan
  class Box<T> { T[] buf; public Box(List<T> xs) { buf = new T[xs.Count]; } }
  ```
  - 症状：`buf.Length` 读成 0（闭包里取到的数组指针丢了）。
- 根因：`MapType` 先判"是否被擦除"再判"是否数组"，`T[]` 的元素类型是类型参数
  → 整个数组被降成 i64 槽；而 `IsErasedType` 又把数组算成擦除类型，于是
  **存端**按 i64 打包、**读端**按 i64\* 读，两端对不上。
- 方向：数组先于擦除判定（`MapType`），且 `IsErasedType` 排除数组。
- ✅ 已修（2026-10-20）：`MapType` 的数组判断提到擦除判断之前，
  `IsErasedType` 对 `t.ival >= 1`（数组）返回 false。探针 p_enum3 现在与
  C 宿主逐字一致（`p=8`）。

### [ ] A400 自举：`foreach` 不支持可枚举协议（GetEnumerator/MoveNext/Current）

- 现象：自举的 `foreach` 只认 `List<T>` / `T[]` / `string`，其余集合一律
  **报诊断拒绝编译**；LINQ 的迭代器（`ValueWhereIterator` 等）因此整个用不了，
  差分门里 `value_enumerable` / `linq_query` 长期是已知缺口。
- 方向：按 C 宿主的协议降级——`coll.GetEnumerator()` → 枚举器，
  `enumerator.MoveNext()` → bool，`enumerator.Current` → 元素。
  自举的泛型是**完全擦除**的，所以 `Foo<T>.GetEnumerator()` 不需要特化，
  直接走既有的实例调用路径即可。
- ✅ 已修（2026-10-20）：`irgen_stmt.zan` 新增 `GenForeachEnumerable`，
  `GenForeach` 在非 List/数组/字符串时先试它、失败才报原诊断。
  `Current` 支持**属性**（`get_Current`）与**方法**（`Current()`）两种形态
  ——与 C 宿主 irgen 的口径一致（字段形态是 C 宿主的缺口，见 A398）。
  用例 `tests/conformance/enumerable_protocol.zan` 钉住。

### [ ] A401 自举：认不出的调用**静默返回 0**——违反 fail-closed

- 现象（2026-10-20 探针实测，探针在 `_scratch/sd/p_unknown.zan`）：
  ```zan
  Console.WriteLine(Task.Delay(80));
  ```
  - C 宿主：编译期报错（`unresolved call 'Task.Delay'`）。
  - 自举：IR 里是 `printf(@.fmt.nl, i64 0)`——**一个字诊断都没有**，程序
    拿着错的值继续跑。
  - 根因：`Task` 是 C 宿主的**内建**（`src/compiler/builtin_api.c`），不在
    标准库的 Zan 源里，自举的 binder 不认识它 → `GenCall` 落到兜底分支
    `return new ExprVal("0", MkType("int"))`。
- 影响面：不止"接收者类型不认识"这一条——字符串/字典/StringBuilder 的**未知
  方法**也静默降级（字符串返回**接收者本身**，字典返回 0）；被调用者既不是
  成员访问也不是名字（`filters[f](item)`）也静默返回 0。
- 方向：这些位置全部改成**报诊断**；诊断发在**降级点**（irgen），因为只有
  降级点知道"自举到底会不会降级这个调用"（检查器对"接收者类型未知"一律放过）。
- ✅ 已修（2026-10-20）：`irgen_expr.zan` 的 `GenCall` / `EmitStrMethod` /
  `EmitDictMethod` / `EmitSbMethod` 全部 fail-closed；接收者名字解析不出来时
  报 C 宿主同款措辞（`unresolved call 'X.Y': 'X' is not a known variable, type,
  or namespace`）。两个 reject 用例钉住边界：
  `tests/selfhost/reject/unsupported_builtin_call.zan`、
  `tests/selfhost/reject/unresolved_call.zan`。

### [ ] A402 自举：不认识 C 宿主的**编译器内建**——标准库自己就在用，全是错的

- 现象：把 A401 改成 fail-closed 之后，标准库自己编不过了——报的是
  `call to 'String.CompareOrdinal' is not supported`、`call to 'b.ToStr' is not
  supported`、`string method 'ToBytes' is not supported`。这三类调用**自举一直
  在错编**（静默返回 0 / 返回接收者），只是没人跑到。
- 根因：它们是 **C 宿主后端里的内建**，不是标准库方法：
  `String.CompareOrdinal(a,b)` → `strcmp`（`irgen_call.c`）；
  `string.ToBytes()` / `byte[].ToStr(off,len)` → 零拷贝桥接
  （`irgen_expr.c` 的 `emit_bytes_call`）。标准库只是调用方（`Convert.zan`、
  `StringExtensions.zan`、`Text/Encoding.zan` …）。
- 方向：自举按 C 宿主的口径补齐这三个内建。窗口要**钳制**（off 夹到
  `[0,len]`，长度夹到 `[0,len-off]`）；自举的数组是**每元素一个 8 字节槽**，
  字节要逐个打包/拆开（不能整块 memcpy）。
- ✅ 已修（2026-10-20）：`irgen_expr.zan` 新增 `StrToBytes` /
  `ByteArrWindowToStr`，`String.CompareOrdinal` 落到 `strcmp`；用例
  `tests/conformance/bytes_bridge.zan` 钉住（自举与 C 宿主逐字一致）。

### [ ] A403 自举：调**委托类型的表达式**（`filters[f](item)`）静默返回 0

- 现象：LINQ 包 `packages/Zan.Linq/src/System/Linq/Stream.zan` 里的
  `this.filters[f](item)`（谓词存放在列表里，调用列表元素）自举**编不过**——
  被调用者是**索引表达式**，既不是成员访问也不是名字。改 fail-closed 之前，
  它静默返回 0 → 谓词永远返回"假" → 过滤结果全错。
- 方向：被调用者既不是成员访问也不是名字时，**只有委托类型的表达式**能降级
  （复用委托调用路径 `EmitDelegateCallVal`），其余报诊断。
- ✅ 已修（2026-10-20）：`EmitDelegateCall` 拆成"求值 + 调用"两半
  （`EmitDelegateCallVal`），`GenCall` 的兜底分支先求值再判是不是委托类型。
  差分门里 `value_enumerable` / `linq_query` 两个用例从"编不过"变回通过。

### [ ] A404 自举：认不出的**标识符**与**成员**仍静默返回 0
- 现象：`GenIdent` 末尾 `return new ExprVal("0", MkType("int"))`、
  `GenMember` 末尾同样。也就是 `int x = undefinedName;` 在自举里是 `x = 0`，
  `obj.NoSuchField` 也是 0。
- 方向：与 A401 同口径——报诊断。但要小心：自举的**类型推断**偏弱
  （注释里写过"自举常常留一个标识符的值类型没解析出来"），照搬"解析不出来就
  报错"会把合法程序也拒掉，所以要先摸清自举在哪些地方真的拿不到类型。
- ⚠ 尚未开工：A401 只覆盖了**调用**路径，标识符/成员访问两条路还开着。
- ✅ 已修（2026-10-20）：`GenIdent` / `GenMember` 的兜底改成诊断。
  探针实测：**常见情形检查器早就拦住了**（`undefined identifier 'X'`、
  `type 'X' has no member 'Y'`，与 C 宿主同款拒绝），所以这条兜底是
  "检查器与后端不一致"时的**背靠背安全网**，不是日常路径——固定点门
  （自举编自己的 62 个文件）与差分门（27 个用例 + 标准库）证明它平时不触发。
  真正被它挖出来的是 A405。

### [ ] A405 自举：不支持**静态属性**（`Cls.Prop`）——标准库 `Encoding.UTF8` 就是它

- 现象（2026-10-20 差分门实测）：把 A404 改成 fail-closed 之后，标准库
  `stdlib/System/Text/Encoding.zan:137` 报 `unresolved identifier 'Encoding'`：
  ```zan
  public static Encoding ASCII { get { return Encoding.UTF8; } }
  ```
  `Encoding.UTF8` 是**静态属性**，自举的 `GenMember` 只认静态**字段**、枚举成员、
  静态方法组——静态属性落到兜底 → 之前**静默返回 0**（`Encoding.UTF8` 拿到 0，
  编码整个是错的），现在报诊断。
- 方向：静态属性就是**静态 getter 方法**（解析器把属性拆成 `get_Prop`），
  按 C 宿主的口径降级成 `Cls.get_Prop()` 的静态调用。
- ✅ 已修（2026-10-20）：`GenMember` 的静态分支加一条"找静态 `get_<name>`"
  （`FindMethodUp` 会走基类链）；用例 `tests/conformance/static_property.zan`
  钉住（自举与 C 宿主逐字一致）。

### [ ] A406 自举：内建表的能力没测过——先量出来，再按矩阵补齐

- 现象（2026-10-20）：C 宿主把一批 API 声明成**内建**
  （`src/compiler/builtin_api.c` 的 `builtin_types[]` / `members_*[]`），
  自举后端要么自己降级、要么报诊断拒绝——但"哪些能降级"**从来没量过**，
  全靠撞。A401 之后更明显：不支持的调用会报诊断，可诊断只会告诉你
  "不支持"，不告诉你**一共还差多少**。
- 方向：写一个按内建表**自动生成探针**的测量脚本（参数类型从签名取，
  只编译不运行），把结果落成能力矩阵文档；然后按矩阵分组补齐。
- ✅ 已修（2026-10-20）：
  - 测量脚本 `scripts/selfhost_builtin_sweep.ps1`，矩阵文档
    `docs/selfhost-builtins.md`。
  - 按矩阵补齐了一批：`Math` 全家（14 个）、`string.Equals`、
    `List<T>.ToArray`、`Dictionary.Clear`、`StringBuilder.Clear`；
    用例 `tests/conformance/builtin_surface.zan` 钉住（逐字一致）。
  - 剩下的缺口记在文档里：`Task.*`（要协程帧，见 A385）与 `Console` 的
    颜色/标题属性。

### [ ] A407 自举：double 的**文本格式**与宿主不一致（`%g` vs 最短往返）

- 现象（2026-10-20 探针实测）：`Convert.ToString(Math.Acos(0.0))`
  - C 宿主：`1.5707963267948966`
  - 自举：`1.5708`
  根因：自举用 `printf("%g")`（六位有效数字），宿主用运行时的
  `zan_rt_dbl_str`（**最短往返**格式，`src/runtime/rt_timer.c`）。
  凡是打印 double 的程序，自举与宿主的输出都不一样。
- 方向：自举的 double→文本改走运行时的 `zan_rt_dbl_str`（跟宿主同一份
  格式代码），`Console.WriteLine(double)` 与 `ToCStr(double)` 两处都要改。
- ✅ 已修（2026-10-20）：`irgen_expr.zan` 新增 `DblToStr`（调
  `@zan_rt_dbl_str` 再包成托管串），`ToCStr` 与 `Console.WriteLine` 的
  double 分支都改走它；`builtin_surface` 用例里 `acos=1.5707963267948966`
  与宿主逐字一致。

### [ ] A413 自举：`Console.*` 只有一条 blanket catch——`Clear` 打印 "0"、`ReadLine` 返回垃圾

- 现象（2026-10-20 差分门探针实测，探针在 `_scratch/sd/p_console.zan` 等）：
  - C 宿主 `Console.Clear()` 输出 `ESC[2J ESC[H`、`ResetColor()` 输出 `ESC[0m`；
    自举两者都打印 `0`。
  - C 宿主 `Console.WriteLine(true)` → `true`、`WriteLine('x')` → `x`；
    自举 → `1` / `120`。
  - C 宿主 `Console.ReadLine()` 返回读到的行（含 `\r\n`）；自举返回垃圾，
    `line.Length` 直接访问冲突崩（exit -1073741819）。
- 根因：自举 `irgen_expr.zan` 里 `Console.*` 只有**一条兜底分支**——"取第一个
  实参，把它打印出来"。于是每个成员都被当成"打印第一个参数"：`Clear()` 打印
  `0`，`ReadLine()` 打印 `0` 并返回 0。
- 方向：照 C 宿主 `irgen_call.c` 的口径**逐个成员**降级（按实参静态类型分流
  `%s` / 最短往返 / `true`-`false` / char 的 UTF-8 / `%llu` / `%lld`；
  `Clear`、`ResetColor` 发 ANSI 序列；`ReadLine` 用 `fgets` 并回写精确长度；
  `Read`、`ReadKey` 读一个字符）。
- ✅ 已修（2026-10-20）：`irgen_expr.zan` 新增 `GenConsoleCall`（逐成员分流）、
  `CharToStr`（char → UTF-8 文本），删掉 blanket catch；`irgen.zan` 加两个 ANSI
  常量。用例 `tests/conformance/console_builtin.zan`（各类型渲染）与
  `tests/conformance/console_ansi.zan`（ANSI 字节）钉住，都进了差分门。

### [ ] A415 自举：内建的**属性**（Console 颜色/标题）不降级

- 现象（2026-10-20 能力矩阵实测）：`Console.ForegroundColor` /
  `Console.BackgroundColor` / `Console.Title` 在内建表里是属性（`'P'`），
  自举**不降级**——能力矩阵把它们归到"内建属性"一栏。
- 根因：自举的 `GenMember` / `StoreTo` 只认**调用**与**字段/属性 setter**，
  没有内建属性这条路径。
- 方向：照宿主的口径补——读＝读模块级颜色槽（缺省前景 7 / 背景 0）；
  写＝存 `color & 15` 并发 `ESC[<code>m`（`(idx<8?30:90)+4*b0+2*b1+b2`，
  背景 +10）；`Console.Title` **只可写**（`ESC]0;<title> BEL`），宿主没有
  getter，读它在宿主那边就是编译错误。
- ✅ 已修（2026-10-20）：`irgen_expr.zan` 新增 `ConsoleColorGet` /
  `ConsoleColorSet` / `ConsoleTitleSet`，`GenMember`（读）与 `StoreTo`（写）
  两处都挂了；用例 `tests/conformance/console_color.zan` 钉住（字节逐字一致）。
### [ ] A416 自举的检查器不认识 `Task` / `Task<T>`——任务句柄的定型规则没镜像

- 现象（2026-10-20 对账）：C 宿主 `checker.c` 按**实参形态**给
  `Task.Run` / `Task.Spawn` 定型——lambda/委托/无结果调用 → 无结果 `Task`，
  带类型的调用 → `Task<T>`；把无结果句柄当 `Task<T>` 用在编译期被拒
  （`tests/diag/task_run_action_result_type.zan`，诊断串
  `carries no result; declare 'Task' (not 'Task<T>')`）。自举的
  `src/selfhost/checker.zan` 既不认识 `Task` 这个类型（任何 Task 形状的
  声明都被报成 `unknown type 'Task'`），也没有这条定型规则——宿主与自举
  在「任务句柄定型」上分叉。
- 方向：镜像宿主的三件事——① `Task` / `Task<T>` 是内建类型名（宿主的
  `TYPE_TASK`）；② `Task.Run` / `Task.Spawn` 的结果类型按实参形态定型
  （宿主 `task_run_result_type`）；③ 无结果句柄流进 `Task<T>` 的槽位时报
  编译期错误，措辞与宿主逐字一致。
- ✅ 已修（2026-10-20）：`src/selfhost/checker.zan` 加 `MkTaskType` /
  `IsTaskTy` / `TaskElemTy` / `TaskCallType` / `CheckTaskNoResult`；
  `CheckType` 认 `Task`（至多一个类型实参），`InferType` 按宿主口径给
  `Task.Run`/`Task.Spawn` 定型，`CheckTaskNoResult` 挂在变量初始化、赋值、
  返回值与实参四处。诊断与宿主**逐字一致**（同一份源，两边输出同一条：
  `cannot convert 'Task' to 'Task<int>' in initializer: a task started from a
  lambda/Action carries no result; ...`）。`irgen.zan` 的 `MapType` 显式把
  `Task` 映成 `i8*`（自举的任务句柄就是协程帧指针）。差分门收录
  `task_run_action_handle`（宿主能跑、自举还没有 Task API 的降级），按
  **已知缺口**记账而不是假装通过。

### [ ] A428 自举的重载分派**按名字取**，不按实参类型——多个同名重载会选错方法

- 现象（2026-10-20 语料扫描实测，`tests/conformance/`）：
  - `operator_overload.zan`：`v %= 3` 没走重载，类指针被喂给 `srem`
    （`%t48 defined with type 'ptr' but expected 'i64'`）；`w + 2.5` 取了
    **int** 重载，把 `2.5` 当 i64 实参传（`floating point constant invalid
    for type`）。**已修**。
  - `overload_same_arity_return.zan`：`await OnMessage(message)` 的 await
    临时量取成了**另一个重载**的返回类型（类指针塞进 `printf` 的 i64 形参
    → 非法 IR）。**已修**。
  - `overload_resolution.zan`：`Derived.Widen(1)` 取了派生类的两参重载，
    报 "missing required argument for parameter 'b' of 'Widen'"（宿主选的是
    基类的一参重载）。**已修**。
  - `overload_inherited_arity.zan`：`Strip.Add(b)`（b 是 Bag）取成了派生类
    的 `Add(string)` → 无限递归 → **栈溢出**（0xC00000FD）。**已修**。
  - `cs_b12_conv_operator.zan`：转换运算符重载落到"按名字取第一个"→ 非法
    IR。**已修**（2026-10-20，见 A432）。索引器本体（单个 `this[...]`）与
    **多重**索引器重载也都已修（见 A431 / A432）。
  - `generic_param_shape_overload.zan`：`Item<T>.val` 读回的是**擦除槽的裸
    指针**（打印出地址而不是 "one"）——这是 A421 的泛型字段布局，不是重载
    分派。**未修**（见 A421）。
- 根因：自举有**三条**"按名字取成员"的路：
  1. 运算符分派 `TryOpOverload`（`binder.FindMethod`）；
  2. 普通调用 `ResolveMethod`（单候选时连**实参个数**都不看，派生类遮蔽
     基类重载时也不往基类找）；
  3. `await` 的被调方法 `AwaitCallTy`（`FindMethodUp`）——await 临时量的
     **类型**取自它，于是类型和真正调用的那个重载对不上。
- ✅ 已修（2026-10-20，提交 `93529a7c` + 本轮）：
  - 运算符路径：先在类链里找**声明类**，再按**实参类型**选重载（精确匹配
    4、int→long 3、int→double 2、已知不匹配 -1），发 `MethodSym(声明类, m)`
    （带 mangling 的符号，原来固定发 `Owner_op_add` → 多重重载会撞名）。
  - `%=` 补进复合赋值的重载分支；int 字面量的**后缀**参与选型（`7L` →
    long，宿主同口径）。
  - 普通调用：候选集改成**整条类链**（派生类的重载只遮蔽**签名相同**的
    基类重载——`Strip.Add(string)` 不遮蔽 `Bag.Add(Bag)`），先按**实参个数**
    过滤，再按实参类型打分；实参个数一个都不合适时**沿基类链**继续找。
  - 调用的符号改用**声明类**（`DeclOwnerOf`）：继承来的重载定义在基类名下，
    调用却指向派生类 → 符号不存在。
  - `await` 的被调方法改成同样的"类链候选 + 个数过滤 + 按实参类型打分"
    （参数类型从**外层方法的形参**取——归一化 await 时方法体还没生成，
    局部变量还不存在）。
  - 顺带修：struct 的 `==`/`!=` 改为**按值**比较（原来是指针同一性）。
  - 用例进差分门：`operator_overload` / `struct_operators` /
    `operator_relational` / `overload_resolution` /
    `overload_inherited_arity` / `overload_same_arity_return`。
- 剩余（未修）：泛型实参形状那一族（属 A421）。
### [ ] A431 自举的字段初始化器与构造器语义和宿主对不上——顺序错、基类构造器多调一次

- 现象（2026-10-20 实测；探针 `_scratch/sd/p_fldord.zan`、`p_fldord2.zan`、
  `p_basector.zan`）：
  - 宿主 `new Derived()`（Derived 与 Base 都有构造器）打印
    "base field → base ctor → derived field → derived ctor"；自举打印
    "derived field → base field → base ctor → derived ctor"。
  - 宿主 `new D0()`（D0 **没有**构造器，基类 B1 有）只跑基类与派生类的字段
    初始化器，**不调用** B1 的构造器；自举多调了一次 "B1 ctor"。
  - 宿主 `new D3()`（基类只有 `B2(int)`，没有无参构造器）仍然跑基类的字段
    初始化器；自举把基类字段丢成 0。
- 根因：自举把字段初始化器塞进"隐式构造器"里，顺序是**派生类优先**，而且
  派生类没有构造器时**照样调基类构造器**。宿主根本没有隐式构造器这个函数：
  找不到构造器时字段初始化器在**分配点**发（`irgen_expr.c` 的
  `emit_implicit_field_initializers`），基类链的顺序是"基类在前"。
- ✅ 已修（2026-10-20，本轮）：
  - 字段初始化器改成助手函数 `@__zan_finits_<Class>`：发**整条链**（基类在前），
    在"分配点找不到构造器"与"派生构造器没调到基类构造器"两种情形调用；
  - 构造器顺序改成宿主口径：基类构造器 → 基类链字段初始化器（**仅当**基类
    构造器没被调用）→ 本类字段初始化器 → 构造器体；`: this(...)` 的构造器
    不发任何字段初始化器（全交给被链接的那个构造器）；
  - 基类构造器按**参数个数**选（参数个数为 0 的那个），不再"取第一个构造器"；
  - 构造器的实参个数改成**严格**（与宿主后端同口径：省略的尾部参数用默认值
    补齐），并按宿主的两套措辞报错（检查器一套、后端一套）。
  - 用例进差分门：`field_decl_initializers` / `constructor_chaining` /
    `cs_b09_indexer`（索引器本体）。
- 剩余（未修）：
  - `objinit_ctor_field_overwrite.zan`：自举**崩**（0xC0000005）。根因**不是**
    对象初始化器，而是宿主那条 "`Binding<T>` 槽位赋值" 的原语降解（见 A432）——
    把 `title = seed`（string）塞进 `Binding<string>` 字段时，宿主会合成一个
    Binding 对象，自举直接把字符串指针写进去，随后 `title.Get()` 把它当
    Binding 解引用 → 崩。与本轮的改动无关：改动前后该用例生成的 IR 完全
    一致（无 `@__zan_finits_*`）。
  - **多重**索引器重载已修（见 A432）。

### [ ] A432 自举缺两样：多重索引器按实参类型选、用户自定义转换运算符

- 现象（2026-10-20 实测）：
  - `indexer_overload.zan`：一个类上两个索引器（`this[int]` 与
    `this[string]`）时，自举按**名字**取第一个 → `b["k"]` 走到了 int 那个
    重载。**已修**。
  - `cs_b12_conv_operator.zan`：`implicit`/`explicit operator T(S)` 自举
    完全没有这个概念——`double d = f;` 把 struct 指针直接塞进 double 槽位。
    **已修**。
- 根因：
  - 索引器：自举用 `FindMethodUp`（按名字取第一个），没有"按实参类型选"的
    路径（运算符重载那条路 §44 修好了，索引器没走那条路）。
  - 转换运算符：自举的转换点（变量初始化 / 赋值 / 实参 / return / 强制转换）
    各自做"内建转换"，没有"用户定义转换"这一层。
- ✅ 已修（2026-10-20，本轮）：
  - 索引器：候选集来自整条类链（派生索引器只遮蔽**签名相同**的基类索引器），
    按下标实参类型打分（写入口按下标与值两个实参打分）。
  - 转换运算符：一次查找（源类型优先、再目标类型；静态方法、恰好一个参数、
    返回目标类型、参数是源类型）+ 五个转换点（变量初始化 / 赋值 / 实参 /
    return / 强制转换；只有强制转换允许 `op_explicit`）。
  - 转换之后的值类型要跟着走：return 的 ARC 记账原来用**源**类型 → 转换后
    值已是目标类型，记账要改成转换后的类型。
  - 用例进差分门：`indexer_overload` / `cs_b12_conv_operator`。
- 关联：`objinit_ctor_field_overwrite.zan` 的崩溃不是对象初始化器的问题，而是
  宿主那条 "`Binding<T>` 槽位赋值" 原语（赋给 `Binding<T>` 槽位时合成一个
  Binding 对象；赋字段左值时合成 live 绑定）自举没有——见 A433。

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

### [x] A437 尺寸档还有几处不齐：switch 的档位不单调，textarea 只调字号

- 现象：四档统一后仍有例外：
  - `switch` 的 tiny/small/默认都是 24px（`switch.small` 的
    `height: var(--height-tiny)`），只有 large 是 40——"small" 与
    "tiny" 同高，档位不单调；
  - `textarea` 的档位只调字号，高度随内容（多行盒按高度走档不合理，
    但也没说清）；
  - `avatar` 没有 tiny 档（它有显式直径字段，规则对未设置直径的默认
    头像才生效）；
  - `badge` 的默认高度 24（`--height-tiny`），tiny 档 16
    （`--height-micro-tiny`），但代码兜底值过去写的是 18（已随本轮
    改成 24）。
- **已修**（2026-10-20）：`switch.small` 改走 `--height-small`（28），
  阶梯变 24/28/32/40 单调，轨道尺寸不变（tiny 24x14 / small 30x16 /
  默认 40x24 / large 56x32）。`textarea` / `avatar` 两个例外写进
  `docs/GUI_COMPONENT_AUTHORING.md` 的尺寸档一节——是刻意的，不是漏做：
  textarea 档位只调字号、高度随内容（多行盒按固定高度分档会截断输入）；
  avatar 尺寸由显式直径字段决定，档位只作用于没设直径的默认头像，
  且没有比 `--font-size-tiny` 更小的字号令牌可用。

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
### [ ] A438 裸名 `Window` 会绑到错的类——`Gui.Backend.Window` 与 `System.Data.Window` 同名

- 现象：给一个已经能编过的文件加一行 `using Game.Cards;`（引包）之后，
  文件里的 `Window.GetTickMs()` 立刻编不过：
  `unresolved call 'Window.GetTickMs': 'Window' is not a known variable, type, or namespace (is the class imported and registered in the stdlib map?)`。
  单写 `using System; using Gui;` 的探针文件则把 `Window` 绑到
  `System.Data.System_Data_Window`（Zan.DataFrame 里的另一个 Window），
  报 "type 'System.Data.System_Data_Window' has no member 'GetTickMs'"。
- 根因（未定位到具体代码，先记现象）：裸名解析没有"歧义"概念。
  `Window` 在 `Gui.Backend`（Zan.Gui）、`System.Data`（Zan.DataFrame）、
  `System.Automation`（Zan.Desktop）三处都有同名 public 类；而
  `using Gui;` 这类导入会把子命名空间的类型也算进候选，于是候选集随
  import 集变化，谁赢取决于解析顺序。之前 `Window.GetTickMs()` 能编过
  是**兜底命中**（检查器在内建成员表里找到了同名成员），不是真绑到了
  `Gui.Backend.Window`——irgen 需要类符号时就会露馅。
- 影响：凡是"同一文件既用 `Window.GetTickMs()` 又 import 别的包命名空间"
  的程序都会中招。游戏项目普遍用 `Window.GetTickMs()`，所以不是边角。
- 方向（择一）：
  1. 裸名多候选时按"成员命中"择优：调用 `X.M` 时先筛出声明了 M 的候选类，
     唯一命中就用它；仍然多命中才报歧义。
  2. 显式歧义诊断：裸名命中多个类就报"X 有多个候选（A.B.X / C.D.X）"，
     要求显式 `using` 或全限定名——比静默绑错强。
  3. 收窄 `using X;` 的导入面：只导入 X 直属的类型，不含子命名空间
     （`using Gui;` 不再自动带出 `Gui.Backend`），歧义面自然变小。
- 现状绕行（已落在 ddz 项目里）：用到 `Window` 的文件显式写
  `using Gui.Backend;`（仓库里的 trap-cat 模板本来就这么写）。
- 探针（最小复现，内容直接抄进本条，随时可重建）：
  ```zan
  using System;
  using Gui;
  public class ProbeWindow {
      static int Tick() { return Window.GetTickMs(); }
      static void Main() { Console.WriteLine("tick=" + Convert.ToString(ProbeWindow.Tick())); }
  }
  ```
  编译命令 `zanc <file>.zan --auto-stdlib --package-project . -o out.exe`：
  不加 `using Gui.Backend;` 绑到 `System.Data.Window` 报错；加上即编过。
- 关联：无（命名解析族，与 A430~A437 不同族）。

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

### [x] A447 泛型类的虚方法体拿不到类型参数——`is T` 经基类虚调用恒为 false

- 现象（2026-10-21 落地 `Zan.Messaging` 时撞到）：`class Ch<T> : Base`，
  `Base.Accepts(object)` 是虚方法，`Ch<T>` 覆写它并写 `return m is T;`：
  - `Ch<Order> ch; ch.Accepts(new Order(1))`（**直接调用**）→ true ✅
  - `Base b = ch; b.Accepts(new Order(1))`（**经基类引用虚调用**）→ **false** ❌
  - 换成 `Ch<T>` 里的**非虚**方法（`Direct(object m) { return m is T; }`）→ 直接调用 true ✅
  - 虚方法体里改成 `return this.AcceptsTyped(m);`（转交非虚方法）→ 仍 false ❌
  - 同一现象在 `build/zanc.exe`（2026-10-21 01:13）与 `build/zanc5.exe`
    （2026-10-20 23:00）上都在，与 HEAD 的「类级单态化」提交无关。
- 根因（2026-10-21 已定位并修复）：**整类只有一张共享 vtable**。IR 实锤——
  `@__zan_vtable_Ch = linkonce_odr constant [1 x ptr] [ptr @Ch_Accepts]`，槽位
  指向**擦除版**方法体；特化版 `@"Ch_Accepts$Order"`（体内 `is T` 判定正确）
  已经发射出来，却没被挂到任何 vtable 上。`new Ch<Order>()` 写进对象头的也是
  这张擦除表 → 经基类引用的虚调用拿到擦除体 → `is T` 被折成常量 false。
- 修法（`src/compiler/irgen_arc.c` + `src/compiler/irgen_expr.c`）：**给泛型类
  的每个实例化单独发一张 vtable**（`__zan_vtable_Ch$<token>`），槽位指向该实例
  化的特化版方法体；`new` 表达式按实例化取表。改动面小、与既有的
  `mangle_inst_suffix` 命名一致、虚调用点一行没动。
  - 保守项：**特化版与擦除版的 LLVM 签名不一致时退回擦除版**（值类型类型实参会
    改签名，按擦除签名发起的虚调用会误读返回值）。
  - vtable 表**创建即填**（`get_vtable_global_inst` 内部填槽）：`emit_vtables`
    在部分路径上早于方法体发射，晚创建会拿到全 null 的表。
- 证据：判定器 `tests/conformance/generic_virtual_dispatch.zan`（4 条：覆写体
  认自己的类型 / 圆认圆 / 方认方 / 两个实例互不干扰）。修复前 4 条全红，修复后
  全绿（`ALL GENERIC VIRTUAL DISPATCH TESTS PASSED`）；arcguard arc-clean、
  `--check-leaks` 无泄漏。
- 影响：**静默错值**——类型判定返回 false 而不是报错。凡是靠运行期类型分发的
  库（消息总线、插件注册表、序列化分派）都会踩，且症状是"消息没人收 /
  永远匹配不上"，极难定位。
- 旁证：**方法组转委托也会抹掉 `is T`**——`FuncT<object,bool> f = chan.Accepts;`
  （`chan` 是 `Chan<T>`）→ 恒 false；同一方法用 lambda 包一层
  （`(object m) => { return chan.Accepts(m); }`）→ true。
- 绕行（已落在 `Zan.Messaging`）：**类型判定在派生类构造器里定型成委托**
  （`this.acceptsField = (object m) => { return m is T; };`），基类只调用
  委托、不碰类型信息；总线只持有基类引用。修复后这个绕行不再是必需的，但保留
  也无害。
- 探针（最小复现，内容直接抄进本条，随时可重建）：
  ```zan
  using System;
  using System.Collections.Generic;
  class Order { public int id; public Order(int id) { this.id = id; } }
  class Base {
      public virtual bool Accepts(object m) { return false; }
  }
  class Ch<T> : Base {
      public override bool Accepts(object m) { return m is T; }   // 虚方法体里用 T
      public bool Direct(object m) { return m is T; }              // 非虚方法
  }
  public class Probe {
      public static void Main() {
          Ch<Order> ch = new Ch<Order>();
          Base b = ch;
          Console.WriteLine("direct: " + ch.Direct(new Order(1)).ToString());  // true
          Console.WriteLine("virtual: " + b.Accepts(new Order(1)).ToString()); // false
      }
  }
  ```
  方法组那一支（同一文件加两行即可复现）：`FuncT<object, bool> f = ch.Accepts;`
  → 调用恒 false；改成 `FuncT<object, bool> f = (object m) => { return ch.Accepts(m); };`
  → 恒 true。
- 关联：A448、A449。

### [x] A448 泛型方法按推断调用时不单态化——方法体里的类型判定恒假

> **2026-10-21 更正**：原标题写的是"在包单元里"，**不对**。真正的触发条件是
> **调用目标是实例方法**（与跨单元无关）：单编译单元里同样复现。跨单元只是
> 最初撞到它的场景。

- 现象：`Zan.Messaging` 第一版把登记写成 `MessageBus.Register<T>(MessageChannel<T>
  channel)`（泛型方法在**包单元**里），调用方在主单元：
  - `bus.Register(orders)`（**推断** T）→ 消息投不出去（`Publish` 返回 0，
    订阅者一次没被叫醒），**不报错**；
  - `bus.Register<Order>(orders)`（**显式** T）→ 正常投递。
- 隔离出的边界（2026-10-21 复测更正）：
  - **实例**泛型方法按推断调用 → 错（同单元、包单元都错）；
  - **静态**泛型方法按推断调用 → 对（同单元、包单元都对）；
  - 显式类型实参 → 永远对；
  - 跨编译单元（stdlib 的 `RandomExt.Pick<T>(List<T>)`）推断调用**结果正确**
    ——但那个方法体里没有类型判定，擦除不影响结果。
- 根因（2026-10-21 已定位并修复）：**形参与实参的映射差一位**。
  `src/compiler/irgen_expr.c` 的 `infer_method_tp_bindings` 写的是
  `int arg_base = recv_expr ? 1 : 0;`——只看"有没有接收者表达式"，于是把
  **接收者当成了第 0 个实参**去和形参类型合一：
  - 形参类型是 `Chan<T>`（类泛型），接收者类型是接收者类（`type_arg_count == 0`）
    → `unify_method_tp` 的逐位合一一个类型实参都绑不上 → `bind[0]` 保持 NULL；
  - `get_or_create_method_spec` 撞上 `if (!bind[i] || !type_is_concrete(bind[i]))
    return -1;` → 返回 -1 → 调用**退化成擦除版方法体** → 体内 `is T` 被折成
    常量 false。
  - 发射端的口径本来是对的：`try_method_spec` 用
    `recv_params = (recv_expr && m_static) ? 1 : 0`（**只有静态方法经实例表达式
    调用时接收者才占第 0 个实参**），`emit_method_spec_call` 同口径。**只有
    推断侧写错了**。
  - 静态方法按推断调用之所以对：`recv_expr` 是 NULL → `arg_base == 0` → 映射正确。
- 修法（`src/compiler/irgen_expr.c` 一行 + 注释）：`arg_base` 改成与发射端
  同口径 `(recv_expr && m_static) ? 1 : 0`。
- 证据：判定器 `tests/conformance/generic_method_inference.zan`（实例推断 /
  实例显式 / 静态推断 / 静态显式 / 两次推断互不干扰）。修复前 2 条红
  （"instance inferred type argument was lost"、"second inference was polluted"），
  修复后全绿（`ALL GENERIC METHOD INFERENCE TESTS PASSED`）；arcguard arc-clean、
  `--check-leaks` 无泄漏。回归：generic*/inherit*/lambda*/closure*/cast*/
  object*/delegate*/func*/messaging* 共 55 条用例 **0 失败**。
- 影响：静默错值（判定恒假 → 消息没人收）。凡是"实例泛型方法 + 按推断调用 +
  方法体里用类型参数"的组合都会踩。
- 遗留（**未修**，另立条目跟踪）：类型参数在 binder 里是**按名字、先到先得**注册
  在**根作用域**的（`binder.c` 的 `register_type_param_list`），类级 `T` 与方法级
  `T` 会互相顶掉；"同名 T 先注册谁"取决于**文件解析顺序**，同一份源码放主文件
  还是放包里，绑定结果可能不同。修它要动绑定期所有类型解析，影响面远大于本条，
  单独一条。
- 探针（最小复现，内容直接抄进本条，随时可重建）：
  ```zan
  using System;
  using System.Collections.Generic;
  class Chan<T> { }
  class Order { public int id; public Order(int id) { this.id = id; } }
  class Api {
      // 实例泛型方法：推断出来的 T 必须真的进到方法体
      public FuncT<object, bool> Make<T>(Chan<T> c) {
          return (object m) => { return m is T; };
      }
      // 静态泛型方法：推断本来就是对的（对照）
      public static FuncT<object, bool> MakeStatic<T>(Chan<T> c) {
          return (object m) => { return m is T; };
      }
  }
  public class Probe {
      public static void Main() {
          Api api = new Api();
          Console.WriteLine("inferred: " + api.Make(new Chan<Order>())(new Order(1)).ToString());        // false（修复前）
          Console.WriteLine("explicit: " + api.Make<Order>(new Chan<Order>())(new Order(1)).ToString());  // true
          Console.WriteLine("static:   " + Api.MakeStatic(new Chan<Order>())(new Order(1)).ToString());  // true
      }
  }
  ```
- 关联：A447、A418、A453（类型参数作用域分层，遗留项）。

### [x] A449 值类型 ↔ `object`：显式装箱崩进程，lambda 返回类型不兼容不报错

- 现象（2026-10-21 落地 `Zan.Messaging` 时撞到，`build/zanc.exe` 01:13 实测）：
  - **显式装箱直接崩进程**：`int v = 42; object boxed = (object)v;` → 程序
    **当场 access violation**（退出码 `0xC0000005` / -1073741819），没有任何
    诊断、没有消息；连"崩之前打印一行"都做不到。
  - **lambda 返回类型与委托返回类型不兼容不报错**：
    `FuncT<Order, object> f = (Order o) => o.id;` → **编译通过**；运行期
    `f(new Order(7))` 返回**非 null 的垃圾指针**（int 值被当成对象指针，
    `r == null` 为 false）；进程退出时 ARC 去回收这个假对象 → 再次
    access violation。块形式 `(Order o) => { return o.id; }` 同样编译通过、
    同样给垃圾（两种写法结果一致）。
- 根因（2026-10-21 已定位并修复）：两处，都是"没有装箱设施"却放行了。
  1. **显式装箱**：checker 的 `checker_cast_is_valid` 对 `dst->kind == TYPE_OBJECT`
     **无条件放行**；irgen 侧 `emit_expr_cast_expr` 遇到"整数 → 指针"就走
     `LLVMBuildIntToPtr`——`42` 变成指针 `0x2A` 存进 `object`，之后任何一次
     对象操作（ARC release 读头 / `is` 读 site 字）都在 `0x2A-16` 处读 →
     **access violation**，全程无诊断。
  2. **lambda 返回类型**：checker 的 `AST_LAMBDA` 分支**直接 `return type_error`**，
     而 `type_error` 在 `checker_type_assignable` 里被当作"什么都行"
     （`if (target->kind == TYPE_ERROR || value->kind == TYPE_ERROR) return true;`）
     → 赋值检查静默通过；irgen 侧 lambda 的 LLVM 返回类型直接取委托的返回类型
     （`object` → `i8*`），函数体里的 `o.id`（i32）在 return 处被 IntToPtr 转成
     指针 → 运行期返回**非 null 的垃圾指针**，进程退出时 ARC 回收这个假对象
     再崩一次。
     - 附带发现：checker **根本没给 lambda 的形参建类型**（`AST_LAMBDA` 分支里
       那个空的 `for` 循环是残留），也**不检查块形 lambda 的语句**
       （`zan_checker_check_expr` 没有 `AST_BLOCK` 分支）——所以"在 checker 层
       比对 lambda 体类型"这条路走不通，必须在 irgen 层做（那里委托返回类型
       与体类型都精确已知）。
- 修法（走方向 2：**编译期诊断**，不做真装箱）：
  - `src/compiler/checker.c`：`checker_cast_is_valid` 里把
    `dst->kind == TYPE_OBJECT` 收窄为"**来源必须是引用类型**"（可空类型按基类型
    判）；值类型 → `object` 报 `cannot convert 'int' to 'object': value types
    cannot be boxed (Zan has no boxing facility)`。
  - `src/compiler/irgen_expr.c` / `irgen_expr_core.c`：新增
    `check_no_boxing_conversion()`——目标类型是引用类型、来源是值类型（或反过来）
    就报错；类型参数（泛型未实例化）放过，用户自定义 `op_implicit` 放过。
  - `src/compiler/irgen_expr.c` 的 `emit_lambda_typed`：表达式体按
    "委托返回类型 vs 体类型"比对。
  - `src/compiler/irgen_stmt.c` 的 `AST_RETURN_STMT`：块形 lambda 的 `return`
    走这条路（只在 `g->lambda_depth > 0` 时查——普通方法由 checker 负责，不重复报）。
  - `src/compiler/irgen_expr.c` 的 `emit_expr_cast_expr`：**兜住泛型特化**——
    `object Box<T>(T x) { return (object)x; }` 的来源是类型参数，checker 阶段
    看不出来，放行；特化成 `Box<int>` 之后 `int` → `object` 由 irgen 拦下。
  - 保留的口子：`object → 具体类` 的下转照旧放行（运行期检查）；`(T)(object)null`
    这类"先转 object 再转 T"的写法不受影响（`null` 的类型就是 object）。
- 证据（三个诊断用例，全部"修复前编译通过、修复后报错"）：
  - `tests/diag/box_value_to_object.zan` → 显式装箱；
  - `tests/diag/lambda_return_object.zan` → 表达式形 lambda（块形同样报错）；
  - `tests/diag/box_generic_value_to_object.zan` → 泛型特化里的装箱。
  - 诊断文案：`cannot convert 'int' to 'object': value types cannot be boxed
    (Zan has no boxing facility)` / `cannot convert 'int' to 'object' in lambda
    return: value types cannot be boxed into a reference type`。
- 影响：任何"值类型进 `object`"的写法要么崩进程，要么静默拿到野指针。这是
  `Zan.Messaging` 不能做"统一请求-响应 + 擦除结果"的直接原因——结果类型
  必须由泛型端点一路保留（`QueryEndpoint<T, TResult>`）。
- 未做（**真装箱**，另立条目跟踪）：值类型 → `object` 时分配一个带类型描述符的
  堆对象。可行路径已探明（`rt_alloc(16 + payload, site_id, name)` + 为该值类型
  注册 site + 载荷写在头部之后；`is T` 的运行期检查读 site 字 → 查
  `__zan_site_names_tbl`，天然可用；ARC 回收走 `__zan_site_dtor_tbl`），但要动
  运行期内存布局、ARC 析构表、反射/`ToString`/序列化对装箱值的处理，是独立战线。
- 探针（最小复现，内容直接抄进本条，随时可重建）：
  ```zan
  using System;
  using System.Collections.Generic;
  class Order { public int id; public Order(int id) { this.id = id; } }
  public class Probe {
      public static void Main() {
          FuncT<Order, object> f = (Order o) => o.id;     // 现在编译期报错
          object r = f(new Order(7));
          Console.WriteLine("r is null: " + (r == null).ToString());
          int v = 42;
          object boxed = (object)v;                      // 现在编译期报错
          Console.WriteLine("after box");
      }
  }
  ```
- 关联：A434、A441~A446（同一族：`object` 没有值语义与类型描述符）、A454。

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
