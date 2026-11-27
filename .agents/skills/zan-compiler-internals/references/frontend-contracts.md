# 前端解析、绑定与语义检查核心契约（Parser / Binder / Checker）

## 一、前端解析、绑定与语义检查（Parser / Binder / Checker）

1. **扩展方法接收者兼容性双向同步**：
   - 动作：扩展方法的接收者兼容性判定必须同时作用于候选筛选与重载评分。
   - 理由：非泛型接口扩展在具体实现类与派生接口变量上应均可调用；精确类型匹配优于接口兼容，接口兼容优于裸类型参数。
   - 边界：涉及泛型接口实参匹配时，必须保留完整的泛型类型实例化匹配路径，禁止单纯通过 implements 符号遍历放行。
2. **语法树节点类型语义隔离**：
   - 动作：类型位置的节点为 `AST_TYPE_REF`，不可与表达式标识符 `AST_IDENTIFIER` 混淆。
   - 理由：混淆会导致符号绑定阶段将类型引用误判为局部变量或成员，引发语义解析穿透。
   - 边界：语法解析器（`parser.c`）中前瞻判定（`looks_like_var_decl`）需严格区分类型指示符与表达式操作符。
   - primitive keyword 后的点属于表达式：`(int.MaxValue - h) / 4` 不能仅凭
     `int` token 进入 cast；先排除 `keyword.member`，同时保留 `(int)-8` 等真实
     cast。判定器 `primitive_constants`。
3. **调用形状与静态性强约束**：
   - 动作：发射调用指令时，接收者槽位必须由目标方法的静态性确定（实例方法绑定 `this`，静态方法无接收者槽）。
   - 理由：防止属性或方法静态性判定失配导致 IRGen 阶段解引用空指针。
4. **控制流可空守卫深度遍历**：
   - 动作：可空性静态分析（Nullability Check）必须穿透 `lock` 等复合控制流语句，同步扫描内部守卫与返回路径。
   - 理由：避免对受锁保护的合法空值判空分支产生假阳性诊断。
5. **抽象方法的三条检查器规则**：
   - 动作：抽象类不可实例化（`new` 报 "cannot instantiate abstract class 'X'"）；
     具体类必须实现继承来的抽象方法（报 "non-abstract class 'X' does not
     implement abstract method 'Y' inherited from 'Z'"）；抽象方法不得同时是
     static / async；含抽象方法的类必须声明为 abstract。
   - 理由：抽象方法没有方法体，调用点在编译期不拦截的话，运行期只能撞空
     指针——而且是"某个分支才崩"的那种。检查器把错误提前到编译期，是抽象
     方法唯一不依赖运行期兜底的保证。
   - 边界：虚表槽位记账必须把 `MOD_ABSTRACT` 与 `MOD_VIRTUAL` 同等对待
     （`irgen.c` 的 `class_has_virtual_methods` / `count_virtual_methods` /
     `get_virtual_method_index`、`irgen_arc.c` 的 `collect_vslot_decls`、
     `irgen_generics.c` 的 `emit_dispatch_call`）——漏一处，派生类的 override
     会落到别的槽位上，虚分发静默走错方法。
6. **限定名必须按命名空间解析，不得退化成"取最后一段猜"**：
   - 动作：`Ns.Type.Member(...)` 这类限定名路径，只能解析到该命名空间（或其
     子命名空间）里的类型；路径首段是**类**时（`Widget.Tooltip.Flush`）后面
     才是嵌套成员。解析不到就报 "unresolved call"，不许退化成"拿最后一段去
     作用域里找同名类"。
   - 理由：退化之后，限定名写错（`System.Text.Csv.Parse`，Csv 实际住在
     `System.Csv`）会静默绑到 `using` 引进的同名类上——不报错、返回类型也
     对不上（`CsvTable` 当 `List<List<string>>` 用），运行期直接踩内存
     0xC0000005。症状落在业务代码里，根因却在编译器，定位成本极高。
   - 边界：判据是"路径前缀与类型所在命名空间必须互为祖先"（按命名空间段边界
     比），外加"路径首段是类 → 嵌套成员"这条例外——少了它
     `Widget.Tooltip.Flush` 这类嵌套访问会被误杀。回归用例：
     `tests/diag/qualified_name_wrong_namespace.zan`（必须编译失败）+
     `tests/conformance/qualified_name_namespace.zan`（合法限定名仍能解析）。
7. **重载分解只有一处规则：checker 与 irgen 必须共用**：
   - 动作：重载候选的打分与取舍规则抽到**共享**位置
     （`src/compiler/binder.c` 的 `zan_null_arg_score`），checker
     （`checker_find_method_typed`）与 irgen（`irgen_expr_core.c` 的
     `concrete_arg_score`）都调它；null 字面量只能绑到引用类型/可空类型
     形参，值类型形参判为不兼容。
   - 理由：两侧各自分解一次、规则还不一致时，**静默走错重载**：
     `Arg.Text(null)`（Text 有 int/long/double/bool/string 五个重载）以前会
     走到 `Text(int)`——checker 侧按 null 的静态类型（object）打分、irgen 侧
     对 null 推断不出类型一律给 0 分，于是"第一个重载"（值类型）赢了，null
     被当成 0 传下去。调用点看不出任何异常，断言却永远对不上。
   - 边界：null 的静态类型是 `object`（`checker.c` 的 `AST_NULL_LITERAL`
     分支），所以判据是"**形参能不能接住 null**"，不是实参的静态类型；
     值类型形参接 null 必须在编译期报错（`tests/diag/null_to_value_type.zan`），
     不能等到运行期变成 0。`object` / `T?` 形参接得住 null。
     实例：`Arg.Text(null)` 与 `Overloads.Of(null)` 两条曾同时走错
     （`tests/conformance/null_overload_resolution.zan` 钉住正解）。
   - 边界（**接口形参**）：语义检查与代码生成**各选一遍**，而代码生成侧曾把
     接口形参一律当 0 分（`irgen_expr_core.c` 的 `method_args_score`：
     `if (pt->kind == TYPE_INTERFACE) continue;`，理由是"打分看不出实现列表"）——
     于是**只靠接口类型区分**的一组重载全部同分，**第一个永远赢**：
     `TransactionScope.Create(fsql)`（`IZanSql` 实参）被发到
     `Create(IUnitOfWork)` 上。字段里装的是错类型（类型混淆），等后面调用保存点
     方法才炸，报的还是"事务已结束"，完全指不到病根。
     现在的口径：null 字面量走 `zan_null_arg_score`；实参也是接口 → 同一个接口
     +4（精确匹配）、派生自它 +1（可赋值）；实参是类 → 实现了该接口 +1，
     没实现**不加分也不判死**（实现关系可能经由泛型实例化等路径看不出来，误杀
     代价大——"不误杀实现类"正是那行 continue 的本意，只是它连"实参就是接口"
     这种能精确比对的情况也一并放过了）。
     回归用例：`tests/conformance/overload_interface_param.zan`。
   - 边界（**lambda 实参**）：lambda **没有自身类型**（Zan 是目标类型驱动的），
     所以实参是 lambda 时，判别依据只能是**形参声明的委托类型**：形参不是委托
     → 该候选判为不兼容；形参委托的形参个数与 lambda 显式形参个数不符 → 判为
     不兼容；类型一致 → 候选加分。
     两侧都要按这个口径：checker（`checker_find_method_typed`）与 irgen
     （`irgen_expr_core.c` 的 `method_args_score`）各取形参的委托类型来判。
     **判不出就放过是最坏的选择**：所有候选同分 → 决议退化成"取成员表里第一个
     同名方法"，而 partial 类跨文件合并时**调用点自己声明的重载排在前面**，
     于是 lambda 被绑到不相干的重载上。
     症状示例：`LspSession.StartAsync(App, ...)` 绑到了 `App` 重载而不是
     `Action<Action>` 重载 → **无限自我递归** → 异常处理临时栈耗尽
     （top ≥ 262144）→ 进程 `exit(1)`。整个 IDE 一开工程就崩，而调用点
     在源码上完全看不出异常。
     边界：`TYPE_TYPE_PARAM` / 类型形参这类"判不出委托类型"的形参**放过**
     （不能因判不出就误杀）；`void` 委托接"有返回值的表达式体 lambda"
     **不判死**（丢弃返回值是合法的），只有块体里显式 `return <value>;`
     才判死。
     回归用例：`tests/conformance/overload_lambda_target.zan`（跨文件 partial，
     干扰重载在调用点声明）。
   - 边界（**实例方法组与继承链调用**）：IR 的 `arg_method_group` 必须与
     checker 的 `expr_method_group` 同时接受 identifier、`AST_THIS_EXPR` 与嵌套
     `AST_MEMBER_ACCESS` 接收者：`state.Click` 从局部静态类型取 class/struct，
     `this.Click` 从当前类型取，`this.actions.Replay` / `holder.actions.Replay`
     从接收者表达式的静态类型取；沿用字段类型实参替换，再按目标委托签名评分。把它当未知普通值会令继承候选同分，派生
     `OnClick(EventValue)->void` 抢走基类 `OnClick(Action)->Parent`，自然链
     `new Child().OnClick(state.Click).Height(36)` 在末端报 E3068；仅修局部
     receiver 仍会漏掉真实 `return new Button("Action").OnClick(this.Click).Height(36)`。
     嵌套字段接收者漏识别时，IR 将方法组当未知值给 0 分；候选虽已遍历完整
     基类链，仍因派生先遍历、平分保首而错选事件重载。红例：natural 订阅 0 /
     eventCalls 1，显式 BindEvent 与基类 receiver 均为 1 / 0；根修同步两侧
     方法组识别，保持候选收集与排名，不靠基类优先或未知值加分掩盖。
     保留遮蔽：局部存在但类型未知时不得回退 binder 同名类型；末端成员命中真实
     字段/属性时仍是普通值（含嵌套委托字段遮蔽继承同名方法），不得误识别成方法组。
     所有权谓词 `expr_yields_delegate_value` 同样须先查字段/属性遮蔽，再查方法：
     借用委托字段不能判成 fresh 方法组闭包，否则调用后清理多 release，业务断言
     全过仍在退出时报 already-freed；真正方法组仍为 fresh owned，正常清理与异常
     实参登记共用该判定，必须保留严格 ARC 遮蔽对照。返回类型推断
     与实际调用必须使用同一重载选择，不能靠拆链、强转或委托临时变量掩盖缺陷。
     回归用例：`tests/conformance/inherited_overload_chain.zan`，覆盖局部/this自然链、
     直接return、partial父类自身双重载与子override、接收者身份、standalone、
     派生值重载、静态方法组、局部/this委托字段；
     `inherited_onclick_field_method_group.zan` 覆盖嵌套字段方法组、闭合泛型字段
     接收者、直接 return 与委托字段遮蔽。最小复现绿后还须实际验证原消费者。
   - 手法（确认编译器实际选了哪个重载）：在**每个**候选重载里各放一句
     `throw new Exception("PICKED-<签名>")`，跑一次看抛的是哪一句——比读评分
     逻辑快得多。最小复现要按真实调用形状写：**Zan 不接受顶层函数**（顶层只
     允许类型声明，报 "expected type declaration"），方法得放进类里；包内成员
     与主编译单元的解析路径可能不同，同形代码换到主编译单元就正常。
8. **裸名的导入作用域按文件隔离，歧义必须显式化**：
   - 动作：声明（class/struct/interface/enum）可见的 `using` 集合只能是
     **它自己那个文件**的导入——`nsresolve.c` 的 `zan_nsresolve_stamp` 要给每个
     文件**快照**一份自己的 using 列表（`snapshot_usings`），不许把多个文件的
     using 合并成一个列表再把指针存进每个声明。裸名类型解析顺序固定为
     当前命名空间 → 祖先命名空间 → 本文件 `using` → 成员命中消歧 →
     歧义诊断；同名类型多于一个时，**按被访问的成员筛候选**（唯一命中就用它），
     仍然多命中就报 `ambiguous type 'X'; qualify it with its namespace`。
   - 理由：驱动层（`main.c`）把所有编译输入——用户文件 + 各包源码——的 using
     合并进同一个 compilation unit 的 `usings` 列表，而声明里存的是**列表指针**：
     合并后每个文件的声明都看得见全工程的 using，于是"别的包文件 import 了什么"
     决定了本文件的裸名绑到哪个类。`Window` 在 `Gui.Backend` /
     `System.Data` / `System.Automation` 三处同名，Zan.DataFrame 新增
     `System.Data.Window` 之后，仓库外 5 个项目（breakout / goldminer /
     gomoku / weiqi / xiangqi）集体编不过——症状是"加一行 using 就编不过"。
   - 边界：成员命中消歧只看**声明**了该成员的候选（`decl_has_member`），命中
     零个就不猜（交给后续诊断），命中多个才报歧义；显式 `using Ns;` 命中的候选
     永远优先于成员命中。回归用例：`tests/conformance/nsusing/`（同名类分处两个
     命名空间，裸名按本文件 using 解析）+ `tests/diag/ns_using_scope_leak.zan`
     （本文件没导入 → 必须报歧义）。

9. **调用不存在的成员方法必须在类型检查阶段点名报出**：
   - 动作：checker 在**调用上下文**里也要判定接收者上有没有这个方法，没有就当场报
     `type 'Ns.Name' has no method 'X'`；取值位置仍报 `has no member`——两句话不要混，
     "方法名写错了"和"字段名写错了"必须分得开。判定口径要与发码口径一致，三类都得
     算进"这个类型上有什么"：扩展方法（首参带 `this` 的方法）、接口方法（含接口
     继承链）、**编译器原生实现的成员**（`int.Parse` / `string.AsSpan` / `Gate.Park` /
     `Socket.RecvOv` 这类——实现完全在编译器里，不在任何成员表里，必须单独按
     「类型名 + 成员名」查原生成员表）。诊断里的类型名要面向用户：同名类型被命名
     空间解析改写成内部 mangle 名（`Gui.Widget.QrEncoder` → `Gui_Widget_QrEncoder`）
     时，显示名用 `命名空间 + "." + 原始简单名`（原始名在声明的元信息里，
     **仅同名冲突时有值**），不能把内部符号名漏给用户。
   - 理由：调用上下文整段跳过成员存在性检查时，错误一路滑到发码阶段，只剩一句
     `code generation failed`——既看不到方法名，也看不出是哪个类型，用户只能自己猜。
     内部符号名同理：诊断里冒出 `Gui_Widget_QrEncoder`，用户会以为自己的类被改名了。
   - 边界：只对"成员集完全已知"的接收者报错。编译期内建类（`Convert` / `Math` /
     `Console` / `File` / `Path` / `Directory` / `Environment` / `NativeMemory` /
     `Task` / `String` …）的成员一半在标准库声明、一半由编译器原生实现，按名字枚举
     必漏 → 判不了就不报；`object` / `decimal` / 类型参数 / 可空 / 枚举 / 数组 /
     无符号的内建泛型容器同样判不了。判不了一律放行到发码阶段，绝不猜。
     漏掉一条原生成员的后果不是"少报一个错"，而是**整个标准库编不过**——
     原生成员表必须与发码侧的分派点逐条对齐。
     **开放泛型模板（类/结构体带类型形参）里的调用不能整类放行**：只有"这个调用
     自己的类型实参里带类型形参"才放行，判据是两条**同时成立**——① 所在类/结构体
     是开放泛型模板；② 该调用的类型实参里带类型形参。
     - 为什么 ① 不够：只按 ① 放行等于把**整个泛型类的成员存在性检查关掉**，模板里
       调一个谁都没有的方法（`repo.NoSuchMethod()`）照样编得过，一路跑到运行期才
       炸。成员集定死的调用该报就报。
     - 为什么 ② 不够：模板体之外（普通类里的泛型方法）成员集判不出来，只说明"这个
       成员压根不存在"——那是真错。缺了 ①，`class P { void Use<T>(Helper h)
       { h.Missing<T>(); } }` 会被放过，错误一路滑到发码阶段只剩一句
       `code generation failed`，比 checker 直接点名差得多。
     - 放行的正当理由：模板体是"擦除模板"，真身由泛型基类特化
       （`zan_parser_specialize_generic_bases`）在派生类里补出，模板体只留一个
       运行期转发桩。ORM 泛型基类就是这个形状——
       `class BaseDao<T> { IDbConnection db;
       List<T> All() { return this.db.Select<T>().ToList(); } }`——`IDbConnection`
       上并没有 `Select`（编译期 ORM 代码生成器只改写"类型实参是具体实体类"的调用，
       模板里的 T 不是具体实体，改写不了），发码侧对这种调用是放行并发运行期桩的
       （`irgen_call.c` 的 `call_receiver_is_open_generic`），checker 必须同口径，
       否则 `BaseDao<T>` / `BaseRepository<T>` 整包编不过。
     回归用例：
     `tests/diag/call_unknown_method.zan`（必须点名报出方法名）+
     `tests/diag/call_unknown_method_name.zan`（同名类型显示原始名）+
     `tests/conformance/native_member_call.zan`（原生成员必须能正常调用）+
     `tests/diag/generic_class_unknown_method.zan`（泛型类里调不存在的方法当场
     报错，接口/类/自身三类接收者各一次）+
     `tests/conformance/generic_open_call_template.zan`（泛型基类里的 ORM 调用
     仍要编得过）。
10. **checker 与 irgen 对"这个类型上有什么"必须共用同一份判定**：
   - 动作：凡是"某类型是否实现了某接口""某扩展方法能否作用于某接收者"这类判定，
     必须落在**一个共享位置**（binder 层），语义检查与代码生成都调它，不许各写一份
     遍历。
   - 理由：两份遍历只要有一处漏（少一层接口继承、少一类接收者），checker 就会把
     合法调用判成"没有这个方法"，而 irgen 又能把同一个调用发出去——诊断与实际行为
     互相矛盾：要么合法代码被误报卡住，要么错误被悄悄放过去。
   - 边界：共享判定要覆盖扩展方法的三类接收者——类型参数（适用任意类型）、接口
     接收者（看实现关系）、其余按 kind + 符号相等；扩展方法的登记时机放在绑定阶段
     （那时方法自己的类型参还在作用域内，首参 `this` 的类型能解析成正确形状），
     登记口径与发码口径逐字对齐（只看首参是不是 `this`，不额外要求 static）。
11. **类型的"显示名"与"身份名"必须分开**：
   - 动作：诊断里显示的类型名必须由**一个渲染器**按类型形状现算（可空 `T?`、
     数组 `T[]`、泛型 `List<T>`）；`zan_type_t.name` 只当身份用（散列、mangle、
     类型相等判定），**一个字都不许改**。所有诊断共用这一份渲染器
     （`src/compiler/binder.c` 的 `zan_type_display`）。
   - 理由：`zan_type_t.name` 对可空类型存的是**元素类型**的名字（`bool?` 的 name
     就是 `bool`），`?` 没有任何字段承载 → 所有诊断把 `bool?` 说成 `bool`，用户
     看不出自己写的是可空类型。同一个字段被"身份"和"显示"两种语义共用，是这类
     失真的通用根因——发现一个字段被两种语义共用，就该拆开，而不是给它打补丁。
   - 边界：渲染只影响面向用户的显示：tuple 身份（`tuple_sig_type`）、typeof 反射
     元数据（`render_type_full`）、调试符号（`di_type_name`）、运行时反射字段名
     （`refl_field_type_name`）、"这是哪个类型"的原始名轴（`checker_type_simple_name`）
     都不许被形状污染。共享渲染入口的结果要用**线程局部轮转池**承载（语言服务有
     诊断线程与工作线程两线程），不能让多个诊断的临时缓冲互相踩。
12. **改公共诊断措辞要改共用入口，不要逐个诊断点手改；宿主与自举必须同一提交改**：
   - 动作：一个措辞被几十个诊断点共用时（`src/compiler/checker.c` 的 `type_name()`
     有 45 个调用点），改措辞要**换掉共用入口的实现**（签名不变 → 调用点零改动）；
     同时**宿主与自举（`src/selfhost/`）必须同一提交改**。
   - 理由：逐个手改必然漏——漏一处，宿主与自举就分叉，对拍门的三字段用例（宿主与
     自举必须同措辞）会红，而分叉的措辞让用户看到"两个编译器说法不一样"。实测 45 个
     调用点共用一个 `type_name()`，改它的实现比改 45 处措辞更省也更安全。
   - 边界：新增措辞要配一条 `tests/diag/` 用例钉住；自举侧对应措辞在
     `tests/selfhost/reject_cases.txt` 里钉住（两字段钉自举侧，三字段再钉宿主侧）。
13. **隔离构建目录必须让 `--auto-stdlib` 找得到 stdlib**：
   - 动作：Windows 下 `--auto-stdlib` 只找 `zanc.exe` 同级的 `stdlib/`，或 exe 目录
     **上一级**的 `stdlib/`（`src/compiler/main.c`）。用隔离 build 目录
     （`_scratch/build-*`）做定向验证时，build 目录要**在源码根下一层**
     （`repo/build` → 命中 `repo/stdlib`）；放到更深处（`repo/_scratch/build-x`）就
     自己补一个指向源码根 `stdlib/` 的目录联接（junction）。
   - 理由：找不到 stdlib 时 `--auto-stdlib` 只打一条 warning，但**凡是用到 `using`
     的用例全部改写**：报出来是"undefined type 'FuncT'"这类完全不相干的错误，整批
     测试集体变红，看起来像改坏了编译器，其实是没 stdlib。
   - 边界：先看输出里有没有 `no stdlib directory found next to the compiler`；有就
     先补 stdlib，再看别的失败。
14. **新增语言原语前先查 ast.h 的 AST 枚举与 lexer 的 TK_ 关键字**：
   - 动作：想加 `sizeof(T)`/`typeof(T)`/`checked(...)` 这类表达式原语时，动手前先
     grep `ast.h` 的 `AST_*` 枚举、`parser.c` 的 `case TK_*` 分支与 checker/irgen
     的对应 case——`sizeof`/`typeof` 早已存在（`TK_SIZEOF` → `AST_SIZEOF_EXPR`
     → irgen `LLVMSizeOf`），缺的往往只是 checker 校验这一小块。
   - 理由：A382④ 做 Marshal.SizeOf 时重复造轮子：新加枚举成员导致重定义编译
     错、新加 parse 分支与既有 TK_SIZEOF 分支并存——全部回滚后真正要做的只有
     checker 的 E4002 校验一行路。
   - 边界：既原语的返回形态以 irgen 现状为准（sizeof 返回 i64），wrapper（如
     `Marshal.SizeOf<T>() { return sizeof(T); }`）用纯 Zan 泛型方法表达，不要
     给原语再叠 irgen 拦截。
15. **批量改写 C 源码的脚本必须把复合字面量的花括号计入嵌套深度**：
   - 动作：用脚本给 `zan_diag_emit(...)` 这类调用**按逗号切实参、在指定槽位插
     新实参**时，切分器必须同时跟踪 `()`、`[]`、`{}` 三种括号——`(zan_loc_t){0, 0, 0, 0}`
     复合字面量里的逗号不是实参分隔符；并且回读校验：插完后的目标槽必须是
     纯整数字面量（code 槽）、格式串槽必须以引号/标识符开头。
   - 理由：只跟踪圆括号方括号的切分器把代码 `2093` 插进了 `{0, 0, 0, 0}` 内部，
     fmt 串被挤进 int 槽——文案与代码整体错位；A382 全量编号 300+ 站点时
     checker.c 一处中招。
   - 边界：这类错位**必现编译错误**（字符串落进 int 形参），所以"改完必编译"
     就能全量兜住；但脚本层的回读校验能把发现点从"编译失败逐个排查"提前到
     "脚本当场报错"，批量改写一律两条都上。
16. **类型参数保留根 fallback，声明作用域必须遮蔽同名普通类型**：
   - 动作：保留 `register_type_param_list` 的根作用域 fallback 与按名字的类型
     参数等价判定；binder 绑定方法签名、checker 检查类/方法时用
     `zan_binder_push_type_params` 叠加显式声明的类型参数，结束恢复原作用域。
     方法类型参数遮蔽同名类参数，不能先按接收者实参替换掉方法自己的 `T`。
   - 理由（2026-11-12 实证）：`ffi_nint_handle` 的普通 `class T` 曾让 checker
     将只读集合构造器的 `List<T>` 形参重解成 `List<普通类T>`，字段却仍是
     `List<类型参数T>`，报同名类型不可转换。两者 kind 不同，不能靠放宽泛型
     不变性或按名字忽略 kind 来放行。
   - 边界：签名复用 binder 的声明绑定；显式类型参数作用域中解析的方法体类型
     也保留词法绑定（`rt_type` 非空且 `rt_scope == NULL`），交给 IR 现有的
     方法/类实例替换；普通缓存仍按作用域校验。根 fallback 不删除，闭合泛型
     不变性不放宽。判定器：`case_ffi_nint_handle`、`case_type_param_same_name`
     （含普通 T/U 碰撞、构造器/局部/返回值、方法参数遮蔽）及泛型不变性负例。

