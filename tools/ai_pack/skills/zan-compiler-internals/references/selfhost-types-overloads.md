# 自举闭环专题（二）：值类型、重载、继承、泛型与单态化（§40–§75）

返回总索引：[selfhost-closure.md](selfhost-closure.md)

## 40. 没有差分门 ≠ 没有检查：后端路径也得**先查类型再落码**

- 坑（2026-10-20 实测）：自举的 ngen（原生后端）分支在 `main.zan` 里**早于
  检查器**就 return 了——于是一个用了未定义类型的程序，ngen 照样 rc=0 写出
  66KB 的目标码。IR 路径（irgen）是查的，宿主也是查的，只有 ngen 不查。
- 教训：**"这条路径没有差分门"不等于"这条路径没有检查"**。差分门缺的是
  *参照实现*，而类型检查不需要参照实现——它只需要和 IR 路径共用同一个
  Checker。宿主就是"先查类型再落后端"的次序，自举的每条后端路径都该照抄。
- 门禁：没有参照实现的后端，用**自洽门**——"能降级的产出非空产物"+"不能
  降级的**不产出**任何东西并给出诊断"（fail closed）。第二条最关键：没有产物
  就没有"错答案"，比"产物对不对"更容易断言，也更能兜住 fail-closed 边界。
- 坑：断言产物"非空"要定个**下限**（如 64 字节）——空文件也算"有产物"。
- 坑：门禁脚本自己也会编 gen1，所以它同样受"别的会话重建 zanc.exe"的连坐
  影响（撞上被替换的二进制会报 Access violation）。门禁失败先看
  `build\zanc.exe` 的时间戳，别急着怀疑自己的改动。
## 41. 值类型在"没有聚合值"的后端里 = 指针 + **边界拷贝**

- 坑（2026-10-20 实测）：自举 IR 后端没有聚合值，`struct` 实例被表示成堆对象
  （`zalloc` + 字段偏移），于是**赋值 / 传参 / 入容器都只拷指针**——而 C 宿主
  把 struct 按值拷贝。同一个程序：宿主 `list.Add(p)` 后改 `p.x` 读出 `1`，
  自举读出 `99`；`P3 r = q; r.x = 42;` 宿主读 `q.x` 得 `3`，自举得 `42`。
- 教训：后端的**表示**可以不同（堆对象 vs LLVM 聚合值），**可观察语义**不能。
  表示换了，拷贝点就得**逐个补齐**：宿主在哪里按值拷，自举就在哪里显式拷内容。
  "以后改成聚合值就好了"不能作为现在语义不对的理由。
- 做法（`src/selfhost/irgen/irgen_struct.zan`）：拷贝 = 分配新实例 + **逐字段**拷，
  字段的分裂照抄宿主：
  - struct 字段 → **递归深拷贝**（嵌套实例也要分配）；
  - class 字段 → **共享**（拷贝方 retain 一次，源方那一份不动）。
- 拷贝点要**全**：赋值（`StoreTo` 与变量声明的初始化式）、传参（`CallArg`）、
  入容器（`List.Add/Insert/set`、`Dictionary.set`、数组下标、集合初始化、
  查询表达式的 select）、字段写入（`StoreField`）。漏一个就是一个别名。
- 坑：struct 字段是**指针**时，**创建实例**就得把嵌套实例分配出来——否则
  `o.in2.v = 5` 是空指针解引用，自举此前直接崩（不是算错，是崩）。
- 坑：拷贝要**无分支**。嵌套字段可能为 null，判空再拷就得把拷贝代码复制两
  份（IR 里 phi 的前驱块里还有 phi → clang 的 ISel 崩，见 §22 的排查法）。
  办法：用 `select` 选源指针——为 null 时读**目标**（刚分配、已清零）那份，
  等价于"拷一份零"。
- 坑：**方法接收者不在拷贝点里**。宿主里 `v.Bump()` 改的就是调用方的 `v`
  （不是 C# 语义）——先在宿主上把这条测准再决定要不要拷。
- 坑：泛型容器的元素类型是**擦除**的，拿不到具体类型 → "这是不是 struct"
  判不出来，泛型容器里的 struct 仍是别名（记为边界，别假装修好了）。
- 坑：自举的**数组没有析构器**（元素从不释放）→ 拷进数组的副本也永远不释放。
  这与"数组里的 class 元素也不释放"同源，别算成新引入的泄漏。
- 门禁：用例 `tests/conformance/struct_value_copy.zan`（8 个形态）与差分门
  一起钉住。

## 42. 运行期类型判定：没有类声明的类型也要有**类标识**

- 坑（实测）：自举的对象头带类标识槽（`@__tid_X` 全局的地址），于是
  `o is Foo` 判得出；但 `o is List<int>` **恒为假**——容器也是堆对象，却
  在 `zalloc` 时把类标识写成 `null`（`zlist_new` / `zdict_new` / `zsb_new`
  的第三个实参）。
- 教训：**"这个类型没有源声明"不等于"这个值没有类身份"**。内建容器
  （List / Dict / StringBuilder）同样要在对象头里写自己的 tid，类型测试
  才认得出它们。
- 做法：给容器各发一个 `@__tid_*` 全局（没有源声明，所以不能走
  `EmitTid`），容器运行时的 `zalloc` 把它的地址写进类标识槽，类型测试在
  遇到容器名时比较这个标识。**用户类占了同名就让位**（同名 `@__tid_*`
  发两遍就是非法 IR）。
- 坑：往 `zalloc` 传类标识时，`bitcast` 的结果**必须带类型前缀**
  （`i8* bitcast (i8** @__tid_List to i8*)`）——call 的实参是
  `<type> <value>` 对，漏了前缀 clang 报 "expected type"，而且报错位置
  落在全局名上，看着像"全局写错了"。
- 坑：**先量宿主的口径**再动手。原方向以为"泛型容器的 tid 要按元素类型
  分别发射"——实测宿主**不区分元素类型**（`List<int>` 的实例
  `is List<string>` 也为真，`Dictionary` 与 `Dict` 是同一个类型），所以
  一类容器一个 tid 就够。按错误的设想做，工作量翻倍还和宿主对不上。
- 坑：类型测试的口径要**连空值一起量**：宿主里空的容器判为**假**（不是
  "静态类型相同即真"）——所以容器必须走运行期判定，不能走静态类型的捷径。
- 门禁：用例 `runtime_type_test.zan`（11 项判定）与差分门一起钉住。

## 43. 帧头要跟**运行时的调度器头**对齐：对齐的是槽位编号，不是结构体

- 坑（实测）：自举的协程帧是**扁平 i64 数组**，头部自定义编号
  （0=state 1=done 2=awaiter …），而发行版 mt 运行时把帧首当 `zan_co_fhdr`
  读写（`rt_io.c` 的 `co_hdr()`：slot0 = `sched` 的 CO_* 位，slot1 = pending
  step）——运行时去改自举的 state/done，状态机当场错乱，程序跑飞。
- 教训：**跨后端共享的内存布局，编号必须跟对方走**。运行时只碰两个槽，
  但那两槽必须**让给**运行时，哪怕自举用不上。
- 做法：帧仍是扁平 i64 数组（不改成宿主那种混合宽度的结构体），但头部编号
  改成宿主的 `ASYNC_FRAME_*` 顺序：0=sched 1=sched_step 2=state 3=done
  4=awaiter 5=awaiter_step 6=result 7=cleanup 8=hcount 9=own_resume，
  自举从 slot2 起；`own_resume` 原本在 slot5，调用点读它的地方同步改成 9。
- 坑：**重编号必须单遍**。逐槽循环替换会级联（把 5 改成 9 之后，后面原本
  就是 9 的又变成 10）。要一次 `MatchEvaluator` 映射全部槽号。
- 坑：帧的**释放**也必须走运行时：直接 `free(frame)` 绕过运行时的 CO_DEAD
  标记，调度器还会去跑已释放的帧。改成调 `__zan_co_frame_free`（宿主语义：
  先标 CO_DEAD 再 free），自举保留 weak 回退（发行版的是强符号，压过回退）。
- 坑：帧的大小与清零范围跟着头部走（8 槽 → 10 槽，72 → 88 字节；action
  帧的委托槽 8 → 10）。
- 坑：链发行版运行时要用 **MinGW 目标**：`clang --target=x86_64-w64-windows-gnu`
  编译 IR + `ld.lld -m i386pep` + 捆绑的 `mingw/lib`；用 MSVC 链会报
  `__mingw_fprintf` 之类未解析（发行版的 `.o` 是 MinGW 构建）。
- 残留：宿主的 `done` 是 i32、在偏移 20（`state`/`done` 两个 i32 挤在一个
  8 字节里），自举的扁平数组把它放在偏移 24——所以运行时的 join 路径
  （`rt_timer.c` 的 `JOIN_OFF_DONE`）**不适用于自举帧**；自举自己降级
  `Task.WhenAll`/Join，不走那条路。
- 门禁：`selfhost_shipped_runtime` 把自举产物链接到当前构建的多 worker
  运行时，与 C 宿主对拍。运行时的强 `zan_co_ready` 和 `zan_co_sched_init`
  接管帧调度及定时器 ready hook。
- 同步 `Task.Delay` 由拥有就绪队列的调度器推进。gen1 的 weak 初始化把
  C 定时器堆的 ready hook 接到本地 FIFO。同步等待与普通调度共用推进循环，
  每轮检查自己的单调截止时间。强初始化接管时，同步等待交给运行时的
  `zan_co_sync_sleep`。用 `async_sync_delay_timer` 验证等待中的定时器进展，
  用单 worker 的 `async_nested_yield` 验证嵌套推进后外层协程仍能让出。

## 44. 重载分派要按**实参类型**选，不能按名字取

- 坑（实测）：自举的运算符分派用 `binder.FindMethod(cls, name)`——**按名字
  取第一个**。三个后果：
  1. 同名重载取错：`v + 2.5` 取了 int 重载，把 `2.5` 当 i64 实参传
     （`floating point constant invalid for type`）；
  2. 调用发的是**未 mangle 的符号** `Owner_op_add`，而定义侧带后缀
     （`MethodSym`）→ 链接不上；
  3. 运算符在**基类**声明时，调用指向操作数的静态类而不是声明类 → 符号
     根本不存在。
- 教训：重载分派的依据是**实参类型**（宿主口径），名字只是候选集；任何
  "按名字取成员"的地方都要再问一句"取的是哪一个重载"。
- 做法：先在类链里找**声明类**（类 → 基类，第一个声明该 `op_*` 的类），
  再在声明类里收齐全部同名候选，按实参类型打分：精确匹配 4 分、
  int→long 3 分、int→double 2 分、已知不匹配 -1 分；最后发
  `MethodSym(声明类, m)`。
- 坑：**调用侧的符号必须与定义侧一致**。定义侧 `GenMethod` 用
  `MethodSym(cls.sval, m)`（声明类 + mangling），调用侧自己拼
  `Owner_op_add` 就对不上（多重重载还会撞名）。
- 坑：字面量的**类型带后缀**（`7L` 是 long，不是 int）。自举里字面量的值
  类型一律 int，选型要用 AST 节点的 `suffix`（与宿主 `checker.c` 的
  `AST_INT_LITERAL` / `lit_suffix` 同口径）。
- 坑：复合赋值要**逐个**对齐运算符家族。`%=` 漏了 → 类指针被喂给 `srem`
  （`'%t48' defined with type 'ptr' but expected 'i64'`）。宿主把
  `+ - * / %` 映射到 `op_add/op_sub/op_mul/op_div/op_mod`（那张表在
  `irgen_expr_core.c` 的 `infer_expr_type`），自举的 `OpName` 要与之一致。
- 教训：**struct 的相等按值**。自举的 struct 是堆对象，指针同一性会把两个
  相等的值判为不等；没有 `op_eq` 时逐字段比（`EmitStructEq`，列表元素那条
  路径本来就在用）。
- 门禁：`operator_overload` / `struct_operators` / `operator_relational`
  进差分门（逐字一致）。

## 45. 重载候选要来自**整条类链**；派生类只遮蔽**签名相同**的那个

- 坑（实测）：自举的普通调用只拿**本类**的重载当候选，派生类声明了同名
  重载就再也看不见基类的：
  - `Derived.Widen(1)` 取了派生类的两参重载 → "missing required argument
    for parameter 'b'"（宿主选的是基类的一参重载）；
  - `Strip.Add(b)`（b 是 Bag）取成了 `Strip.Add(string)` → **无限递归 →
    栈溢出**（0xC00000FD）——调用进了自己。
- 教训：C# 的重载解析覆盖**整个继承链**；派生成员只遮蔽**签名相同**的基类
  成员。`Strip.Add(string)` 不遮蔽 `Bag.Add(Bag)`。
- 做法：候选集 = 本类 + 整条基类链（同名**同签名**的基类重载按"遮蔽"剔除）；
  再按**实参个数**过滤（`AcceptsArity`：默认参数不算必填，`params` 尾巴
  数组吃剩下的实参）；最后按实参类型打分。
- 坑：**调用的符号要指向声明类**。继承来的重载定义在**基类**名下，调用却
  按派生类拼符号 → 符号不存在（旧代码用 `MethodOwner(cls, name)`，那是
  **按名字**找声明类，重载一多就找错；要用"按签名"找：`DeclOwnerOf`）。
- 坑：`await` 的被调方法**也要**按实参类型解析。`AwaitCallTy` 原来用
  `FindMethodUp`（按名字）→ await 临时量的**类型**取成了另一个重载的返回
  类型（类指针塞进 `printf` 的 i64 形参 → 非法 IR）。
- 坑：归一化 await 时**方法体还没生成**，局部变量还不存在——实参的类型要
  从**外层方法的形参**取（`AwaitArgType`）。
- 门禁：`overload_resolution` / `overload_inherited_arity` /
  `overload_same_arity_return` 进差分门（逐字一致）。

## 46. 字段初始化器与构造器：**分配点**才是它们的家，顺序按基类链

- 坑（实测）：自举把字段初始化器塞进"隐式构造器"里，于是两处都错：
  1. **顺序**：自举是"派生类字段 → 基类字段 → 基类构造器 → 派生类构造器"，
     宿主是"**基类字段 → 基类构造器 → 派生类字段 → 派生类构造器**"
     （基类字段初始化器在基类构造器**内部**跑，因为基类构造器先被调用）；
  2. **基类构造器被多调了一次**：宿主在 `new D()`（D 没有自己的构造器）时
     **不调用**基类构造器——只在**分配点**发整条链的字段初始化器。
- 宿主口径（`irgen_emit.c` 构造器发射 + `irgen_expr.c` 的
  `emit_implicit_field_initializers`）：
  - 构造器 C：先调基类构造器（`: base(args)` 按实参选，没有就选**参数个数
    为 0** 的那个），再发 C 自己的字段初始化器，最后构造器体；
  - `: this(...)` 的构造器**什么都不发**，全交给被链接的那个构造器；
  - **基类构造器没被调用**时（C 没有可用的基类构造器），基类链的字段初始化器
    在 C 的构造器里发，**仍然在 C 自己的字段初始化器之前**；
  - `new C(...)` **找不到构造器**时：字段初始化器在**分配点**发（整条链，
    基类在前），基类构造器**不调用**。
- 做法（自举）：宿主没有"隐式构造器"这个函数，字段初始化器直接在分配点发；
  自举的 `this` 硬绑在字面量 `%this` 上，分配点没有 `%this`，于是用一个
  助手函数 `@__zan_finits_<Class>` 代替（它内部发整条链，基类在前）。
  分配点找不到构造器 → 调这个助手；派生构造器没调到基类构造器 → 调**基类**
  的助手。
- 坑：**基类构造器要按参数个数选**（"参数个数为 0 的那个"），不是"取第一个
  构造器"——基类声明了 `B(int)` 和 `B()` 时，取第一个会取到 `B(int)`，
  于是基类构造器**根本没被调用**（基类的字段初始化器也跟着丢了）。
- 坑：**构造器的实参个数要严格**。宿主有两套规则、两套措辞：
  - 后端（`find_ctor`）：参数个数必须**正好**等于实参个数（省略的尾部参数
    用默认值补齐后再匹配一次）；`params` 数组**匹配不上** →
    "no constructor of 'C' accepts 3 arguments"；
  - 检查器（`checker.c` 的 `ctor_arity`）：实参个数落在 [最少必需, 最多] 内
    就行，`params` 数组上界是 -1（任意个数）→ 检查器放行、后端拒绝。
  - 检查器拒绝时的措辞是另一条："no constructor of 'C' takes 1 argument(s);
    a class that declares constructors has no implicit parameterless one"。
  - 类**一个构造器都没声明**时，检查器的措辞是 "no constructor of 'C'
    accepts N arguments"（复数恒定）。
- 坑：**字段初始化器不能编码成 `Assignment` 节点**。自举的 `GenAssign` 靠
  `op == TK.Eq()` 区分纯赋值与复合赋值；合成节点忘了设 `op` → 走复合赋值分支
  → 生成 `add i64 %t6, %t7` 这种非法 IR。字段初始化器要**单独发射**
  （`EmitFieldInits`），不要塞进语句列表。
- 坑：跨类 `bitcast` 的类型**必须带 `%` 前缀**（`bitcast %Derived* %this to
  %Base*`）——漏了前缀 clang 报 "expected type"，且报错行看着像类型名写错了。
- 门禁：`field_decl_initializers` / `constructor_chaining` 进差分门（逐字一致）。

## 47. 索引器：走 parser 合成的 `op_index` / `op_index_set`

- 坑（实测）：自举的 `GenIndex` 只认数组和内建容器，用户类写 `obj[i]` 直接
  报错；`obj[i] = v` 更是落进"字段赋值"分支，**静默写错地方**。
- 做法：parser 已经把 `this[int i] { get {...} set {...} }` 合成成两个方法
  `op_index` / `op_index_set`，自举照着调即可（读取调 `op_index`，赋值调
  `op_index_set`）。没有索引器的类型**必须 fail-closed** 报错，不能默默按
  字段/下标处理。
- 坑：判"是不是用户类"不能只看 `ClassOf(obj.ty)`——`Vec[]` 的
  `ty.sval` 是元素类型名（"Vec"）、`ty.ival` 是 1，会被误判成类。加
  `obj.ty.ival == 0` 守卫，数组才走数组路径（踩了之后 `struct_value_copy`
  用例从 MATCH 变成 BAD-IR）。
- 坑：**多个索引器重载要按实参类型选**（`this[int]` 与 `this[string]`）。
  按名字取第一个（`FindMethodUp`）会走到错的那个重载。做法与 §45 相同：
  候选集来自整条类链（派生索引器只遮蔽**签名相同**的基类索引器），按下标
  实参类型打分（`OpArgScore`），写入口按下标与值两个实参打分。
- 门禁：`cs_b09_indexer` / `indexer_overload` 进差分门（逐字一致）。

## 48. 用户自定义转换运算符：一处查找，五个转换点

- 坑（实测）：自举完全没有 `implicit`/`explicit operator T(S)` 的概念——
  `double d = f;` 把 struct 指针**直接**塞进 double 槽位，程序读到垃圾。
- 宿主口径（`irgen_expr.c` 的 `find_user_conversion` / `find_conversion_method`）：
  转换运算符是**一个静态方法**，名字 `op_implicit` / `op_explicit`，**恰好一个
  参数**，**返回目标类型**、**参数是源类型**；先在**源类型**里找，再在**目标
  类型**里找（所以 `implicit operator double(Feet)` 与
  `implicit operator Fahrenheit(double)` 两种写法都成立）。
- 做法：一次查找（`FindUserConv`）+ 五个调用点：
  | 转换点 | 宿主位置 | 允许的运算符 |
  |---|---|---|
  | 变量声明的初始化式 | `irgen_stmt.c` | `op_implicit` |
  | 赋值 | `irgen_expr.c` | `op_implicit` |
  | 实参 | `irgen_expr.c` | `op_implicit` |
  | return | `irgen_stmt.c` | `op_implicit` |
  | 强制转换 `(T)x` | `irgen_expr.c` | `op_implicit` + `op_explicit` |
- 坑：**转换之后的类型要跟着值走**。return 路径的 ARC 记账原来用**源**类型
  （`v.ty`），转换后值已经是目标类型 → `bitcast %Feet* <double>` 这种非法
  IR。记账要用**转换后**的类型。
- 坑：转换运算符的**参数是 struct 时，实参要按值拷贝**（C# 语义：运算符收到
  的是副本）。自举的 struct 是堆对象，这个拷贝由 `CallArg` 的
  `NeedsStructCopy` 路径自动完成——**不要**在转换点自己再拷一次。
- 坑：自举源码本身受宿主检查器约束（`ClassOf` 可能返回 null → 直接取
  `.sval` 会被宿主拒绝编译）。写自举代码时按宿主的空值规则来。
- 门禁：`cs_b12_conv_operator` 进差分门（逐字一致）。

## 49. 字段遮蔽：名字查找最派生优先，两个槽位都留

- 坑（实测）：自举的 `FieldIndex`/`FieldType` 扫的是**扁平化后的字段表**
  （`CollectFields`，基类在前），名字命中**第一个**就返回 → 派生类遮蔽基类
  同名字段时，读到的是**基类**的槽位。宿主 `get_field_index` 扫的是成员表
  （基类成员在前、派生成员在后），**取最后一个命中** → 派生优先。
- 做法：拆成 `BaseFieldCount`（基类链的字段数）+ `FieldPos`（本类字段在扁平
  表里的位置，本类字段先找，找不到再递归基类）+ `FieldIndex`/`FieldType`。
  派生字段遮蔽基类字段时**两个槽位都保留**（扁平表里各占一个），只是**名字
  解析**指向派生那个。
- 坑：扁平表是"基类在前"，所以派生字段的位置 = 基类链字段数 + 本类内的序号；
  写错顺序会让派生字段和基类字段**写进同一个槽**（互相覆盖）。
- 门禁：`field_hiding` 进差分门（逐字一致）。

## 50. op_call 协议：裸调用的接收者是类类型局部变量

- 坑（实测）：`E(args)`（E 是类类型的局部变量）自举的 IR 路径直接报
  "call expression is not supported"；ngen 路径早就有这个协议，IR 路径漏了
  → 同一份源码两个后端行为不一致（差分门抓到 `cs_b_opcall_params` 生成
  `call i64 @Prog_sc(i64 1.0, i64 2.5)` 这种非法 IR）。
- 宿主口径：`E(args)` 走**被调用对象协议**——E 的类声明了静态 `op_call` 就调
  `Cls.op_call(E, args...)`（E 作为**显式第一个实参**），声明了实例
  `op_call` 就调 `E.op_call(args...)`。
- 做法：**改写成普通调用**再走既有路径（重载选择、params 打包、实参转换全都
  复用），不要自己拼实参。
- 门禁：`cs_b_opcall_params` 进差分门（逐字一致）。

## 51. 隐式构造器转换：标量实参 → 类类型形参

- 坑（实测）：宿主允许 `TakeBool(false)`（形参是类 `Box`，类有 `Box(bool)`
  构造器）；自举的 IR 路径直接把 bool 塞进 `%Box*` 槽位 → 运行时
  0xC0000005 访问冲突，**一个输出都没有**（stdout 缓冲区随崩溃一起丢）。
  ngen 路径有这条协议，IR 路径漏了。
- 宿主口径（`irgen_expr.c` 的 `is_implicit_ctor_source` / `implicit_ctor_for_arg`
  / `wrap_implicit_ctor_arg`）：源类型是**标量/枚举**（bool/sbyte/byte/short/
  ushort/int/uint/long/ulong/nint/char/enum），目标类型是**类**，且该类有
  **匹配的一参构造器** → 实参改写成 `new Target(arg)`；没有匹配构造器 →
  后端报 "cannot convert 'X' to 'Y' in argument: no matching constructor"
  （检查器放行、后端拒绝，所以措辞是后端这一条）。
- 做法：改写成 `new Target(arg)` 后交给既有的 `new` 管线（分配、构造器、
  字段初始化器、ARC 记账全都对）。自举 IR 路径的落点：`GenArgExpr`（所有
  实参的公共入口）。
- 门禁：`implicit_ctor_argument` 进差分门（逐字一致）。

## 52. `new List<T>(src)`：拷贝构造 vs 集合初始化式

- 坑（实测）：自举的 `new List<T>(...)` 只认 `ival == 2` 的**集合初始化式**
  （`new List<T>{...}`），带括号的实参被**默默丢掉** → `new List<int>(src)`
  得到空表，随后 `cp[0] = 100` 越界 → 0xC0000005。
- 宿主口径（`irgen_expr.c` 的 `list_copy`）：**一个**实参且它是 `List<...>`
  → **拷贝构造**（逐元素复制，新表拥有自己的引用）；其它实参 → 集合初始化式
  （`new List<int>(5)` 得到 1 个元素 5 的表，`new List<int>(1,2,3)` 得到 3 个
  元素的表）。
- 做法：自举的运行时有 `zlist_new`/`zlist_add`，补一个 `zlist_copy(dst, src)`
  （逐元素 `zlist_add`，元素归新表所有时照常 retain——和宿主拷贝路径的
  retain 语义一致）。
- 门禁：`list_copy_ctor` 进差分门（逐字一致）。

## 53. 整数宽度：值在 64 位槽上，**进定宽位置时收窄**

- 坑（实测）：自举的 `int`/`uint` 和 `long` 一样映射到 i64 槽，`int z = unchecked(x + 1)`
  得到 2147483648，宿主给的是 -2147483648。
- 宿主口径（实测，探针 `p_intw` / `p_intw2`）：
  - 整数的**运算**在 64 位槽上做：`int x = 2147483647; x + 1` 直接打印是
    **2147483648**（不是 -2147483648）；
  - 但值**进入定宽位置**（变量初始化 / 赋值 / 字段写 / 实参 / return）时，
    **按声明类型收窄**：同一个表达式存进 `int` 就变成 -2147483648，存进
    `long` 仍是 2147483648；
  - `Console.WriteLine(2147483647 + 1)` 打印 **2147483648**——`Console` 是
    内建路径，不走实参转换，不收窄；用户方法的 `int` 形参则收窄。
  - 无符号类型零扩展、有符号类型符号扩展。
- 做法：新增 `IntBits` / `IntUnsigned` / `NarrowToDeclared`，在五个落点调用：
  局部初始化（`GenVarDecl`）、赋值（`StoreTo`）、字段写（`StoreField`）、
  实参（`CallArg`）、return。**内建路径（Console/字符串/容器方法）不收窄**。
- 坑：收窄只对**整数载体**做：指针、double、装箱的可空值、擦除的类型参数、
  数组槽都不动（擦除槽里可能躺着指针，截成 32 位会毁掉它）。
- 门禁：`cs_b14_checked` / `cs_b15_checked_unsigned` 进差分门（逐字一致）。

## 54. `>>>` 逻辑右移：先加宽再逻辑移位

- 坑（实测）：自举的 IR 路径没有 `>>>`（token 有、ngen 路径有），落到
  `ArithOp` 的空分支 → 整个表达式变成常量 0。
- 宿主口径：`>>>` 一律**逻辑右移**（高位补零），与操作数的有号性无关；宿主
  的做法是**先把左操作数加宽到 64 位**（有符号符号扩展、无符号零扩展）再
  `lshr`。所以 `int neg = -16; neg >>> 2` = 4611686018427387900（不是 C# 的
  1073741820），`uint u = 0x80000000u; u >>> 4` = 134217728。
- 做法：自举的槽本来就是 64 位且有符号值天然符号扩展、无符号值天然零扩展，
  所以 `>>>` 就是 `lshr i64`；`>>>=` 走复合赋值的同一条路。
- 顺带：**移位计数要按左操作数的宽度取掩码**（32 位类型 → 31，64 位 → 63），
  否则 LLVM 里越界的移位计数是 poison（宿主的 `mask_shift_count` 同口径）。
- 门禁：`ushr_operator` 进差分门（逐字一致）。

## 55. 静态构造器：`static C() { ... }`

- 坑（实测）：自举把静态构造器当**实例**构造器发射（带 `%this` 形参），
  它于是永远不被调用 → 类里的静态字段停在初值（`Registry.doubled` 是 0
  而不是 6）。
- 初始化契约：需求身份是**声明所有者 + 闭合类实例**。Main、适用的库导出、
  实际执行的反射创建/调用及已有 attribute/HTML 回调是执行根；可达方法、
  静态字段读写（含 ref/out）与非数组 `new` 激活对应身份。`typeof`、`sizeof`、
  布局、声明、元数据保留和为诊断发射的死方法体都不能独立激活初始化。
- 先从执行根闭包需求；只扫描已激活初始化器的执行边以发现传递需求，再在
  Main 前由单一 `__zan_static_init` 按依赖顺序执行。每个身份内部先按声明顺序
  执行静态字段初始化器，再执行自己的无接收者 cctor；循环沿用 DFS 跳过回边，
  不引入首次访问懒初始化。
- 自举将需求记录在方法体的编译器标记里，`ResolveStaticInits` 解析执行边、
  可达分配和实际 vtable 槽目标，闭包后移除标记及未激活的初始化函数。不能把
  全局表/反射表的保留引用直接当执行根，也不能让初始化器自己成为根。
- 静态构造器与实例构造器的重载集分离；`FindCtor` / `FindCtorArity` /
  `CtorDeclCount` / `ResolveCtorEx` 和 `GenNew` 的 arity 回退都跳过 cctor。
  仅声明 cctor 的类仍有隐式零参实例构造；声明非零参实例构造后则没有。
- 验收入口：`static_constructor`、`static_init_order`、
  `static_init_dependency_order`、`selfhost_static_init_demand`、
  `publish_static_init_demand` 的宿主/自举差分，以及 canonical 源列表的 fixed point。
  自举反射仍沿用现有 tidNames 候选分派，库导出仍受现有非泛型 public 根支持范围
  限制；不能据此宣称具备宿主的精确反射或完整导出边界。

## 56. 表达式里的简单名：局部 > 字段 > 类型

- 坑（实测）：类 `Emitter` 有字段 `Collapse`，另有类 `Collapse`：
  `Collapse.Add("x")` 被当成**静态调用** `Collapse.Add` → 字段里的列表没收
  到元素（`Count()` 是 0 而不是 2）。
- 宿主口径：表达式里的简单名按**最内层声明**绑定——局部变量、然后是外层类
  的字段、最后才是类型名。字段与类型同名时，**字段赢**。
- 做法：`ShadowsTypeName(name)`（当前类有同名的实例字段或静态字段）→ 静态
  接收者的两条入口都要让路：`GenMember` 的类型接收者分支、`GenCall` 的
  `Cls.Method(...)` 分支。

## 57. null 字符串：软失败读零页

- 坑（实测）：自举的 `zstr_len` 直接读 `-8` 处的头 → 空指针崩溃；字符串
  下标更是直接 GEP+load → 0xC0000005，**整个程序一个输出都没有**（stdout
  缓冲区随崩溃一起丢，看起来像"用例没输出"）。
- 宿主口径：null 字符串是**运行期软错误**——带源位置报告一句，然后**换到
  scratch 页继续执行**：长度探针答 0，下标读出 NUL（打印出来是空行）。
- 做法：`zstr_len` 先判 null → 答 0；字符串下标把基址与下标都 select 成
  零页（`@.zeropage`）与 0 → 读出 NUL。
- 坑：自举的 IR 路径**没有运行期报告通道**（报告只在编译期），所以这条
  用例的 stderr 与宿主不同（stdout 一致）。
- 门禁：`static_constructor` / `static_object_fields` /
  `field_shadows_type` / `null_string_index` 进差分门（逐字一致）。



## 58. NativeMemory.Find：命中位置从**基址**量，不是从窗口起点

- 坑（实测）：自举的 `NativeMemory.Find(p, off, b, n)` 把窗口偏移算了两遍
  ——`rel = hit - p` 之后又 `+ off`，于是 `Find(p, 3, 122, 2)` 返回 6 而不是
  3，`ByteBuffer.IndexOfByte(4, 10)` 返回 11 而不是 7（"从偏移续找"整体
  错位）。
- 宿主口径：`Find` 的返回值是**相对基址 p 的绝对下标**（`at - p`），窗口
  偏移已经包含在 memchr 的起始地址里，**不能再加一次**。
- 做法：自举 `irgen.zan` 的 `Find` 内建只保留 `rel = at - base0`，
  `base0` 是 p（不带 off）。
- 连带：`ByteBuffer.IndexOfByte(from, b)` 是 `Find(data, from, b, len-from)`，
  所以同一个错位会让"续找/最后一个/越界起点"三类断言一起挂。
- 门禁：`bytebuffer_indexof` 进差分门（逐字一致）。
## 59. 擦除的类类型参数：null 比较比的是 0，不是 null 指针

- 坑（实测）：自举把泛型类的类型参数擦除成 **i64 槽**，于是
  `val == null`（`val: T`）发出来的是 `icmp eq i64 %t1, null` —— LLVM 直接
  报 "null must be a pointer type"，链接都到不了（用例
  `generic_type_param_null_check` BAD-IR）。
- 宿主口径：宿主按实参把 `Binding<T>`/`Container<T>` 单态化，槽位是指针，
  比的是 null；自举的槽是 i64（指针的 ptrtoint 值），**0 就是 null**。
- 做法：`GenBinary` 的 null 比较与 `EmitNonNullVal` 都按值的 LLVM 类型选
  零：`IsPtrLL(ty)` 为真才用 `null`，否则用 `0`。
- 连带：同一个错位也会出现在 `x is null`（那里早就按类型分叉了）。

## 60. 自举检查器不得比宿主更严：宿主没有的诊断要删掉

- 坑（实测）：自举检查器有一条 "not all code paths return a value in 'X'"
  ——**宿主根本没有这条诊断**（`src/compiler/checker.c` 里搜不到）。于是
  方法体以 `lock (this) { ... }` 收尾的正常程序（packages 里的连接池就是
  这么写的）被自举判成错误，整个 packages 用例编译不过。
- 口径：**宿主是规格**。自举检查器多出来的诊断 = 自举拒收宿主接受 programs
  = 行为不一致；这种"更严"不是安全网，是偏差。
- 做法：删掉那条诊断（连同只服务于它的 `DefinitelyReturns`/`HasBreak`/
  `IsTrueLit`）。自举 irgen 在方法尾部本来就会补默认返回（`ret i64 0` /
  `ret <ptr> null`），IR 不会缺返回块。

## 61. 裸名：类里的字段 / 属性（实例与静态）

- 坑（实测）：类里写 `Value = 100;`（`Value` 是带 `get/set` 访问器的属性）
  被自举检查器判成 "undefined identifier 'Value'" —— 属性在 parser 里被
  降解成 `get_Value`/`set_Value` 两个方法，检查器的名字表里没有它。
- 宿主口径：表达式里的裸名按**最内层声明**绑定——局部、然后是外层类的
  字段/属性、最后才是类型名（属性与字段同等）。
- 做法：检查器的 `KnownValue` 走 `HasMember`（字段 + 方法 + `get_`/`set_`
  访问器）；`InferType` 对裸名再查一次 getter 的返回类型。
- 连带（自举侧）：裸名读属性走 `get_X`，裸名写属性走 `set_X`；**静态属性
  的访问器没有接收者**，必须发静态调用（`Owner_set_X(v)`），不能按实例
  调用发（`Owner_set_X(%this, v)`）——发错就是"把 this 当参数传"的坏 IR。
## 62. 异步方法里的未初始化局部变量：结构体照样要零初始化存储

- 坑（实测）：自举把 `struct` 实例表示成**堆对象**（一个指针）。同步路径里
  `Vec3 v;` 会给局部发一次零初始化分配（zalloc + 嵌套字段），但**异步路径
  只处理有初始化器的声明**——没有初始化器就什么都不发，局部槽里留着帧槽
  的垃圾值。于是 `v.x = x` 往一个野指针上写，程序 0xC0000005，一个输出都
  没有（用例 `async_struct_return`）。
- 做法：把零初始化分配抽成 `AllocZeroStruct`，同步与异步两条路径共用；
  异步路径没有初始化器时也要发（局部被闭包捕获时先分配 cell，结构体实例
  放进 cell 里）。托管引用类型（类）没有初始化器时存 null。
- 连带：帧槽里的垃圾值不是"未定义但无害"——它会变成野指针，症状是整个
  程序无输出，看起来像"用例没跑"。

## 63. by-ref（`ref`/`out`）形参：传的是地址，引用归调用方

- 坑（实测）：自举 IR 后端只认 `ival == 1` 的 place 形式，而 parser 只给
  `ref x` 置了这个标记——`out x` 落到默认分支，被发成 `%Box* null` 当实参，
  而被调方的形参是 `%Box** %p0`：**类型不匹配**。clang 对 `.ll` 默认不跑
  verifier，这种错配会静默通过，运行期解引用空指针 → 0xC0000005，一个输出
  都没有（用例 `out_param_field`）。
- 宿主口径（`src/compiler/irgen_expr.c` 的 `emit_ref_arg`）：
  - `out T name`（**声明**形式）：新建一个零初始化的槽，登记为局部，传地址；
  - `out x` / `ref x`（**place** 形式）：传该 place 的地址（局部 alloca、
    字段地址、元素地址）；
  - 被调方的形参类型是 `map_type(pt)*`——**指向调用方槽位**的指针。
- 做法：用**节点形状**区分两种形式（`a != null` 是 place，`type != null` 是
  声明），不要靠 `ival`；调用点对 `AK.OutArg()` 统一按"传地址"处理。
- 关键纪律：**by-ref 形参的引用归调用方**。被调方读/写这个槽，但**不能在
  作用域退出时释放它**（宿主的 `local_owns_arc()` 对 `byref_slot` 返回 0）。
  自举原来把它当普通局部，退出时多 `zrelease` 一次 → 调用方的引用被多释放
  一次 → use-after-free。
- 连带：
  - 字段地址是 `getelementptr %Cls, %Cls* obj, i32 0, i32 i`，类型是
    `%Cls*`，而 place 要的是 `<字段类型>*`——必须补一次
    `bitcast %Cls* %p to i64*`，否则 clang 报 "invalid cast opcode"；
  - 虚表槽的函数指针类型（`MethodFnTypeThis`）也要给 by-ref 形参加 `*`，
    否则虚调用和定义对不上；
  - 自举的数组元素一律是 8 字节槽，`out arr[i]` 只能给"槽形状"的元素
    （byte/char 除外），否则要 fail-closed 而不是发出语义不同的地址。

## 64. `Binding<T>` 的槽位赋值糖：IR 后端要自己造 Binding 对象

- 坑（实测）：自举把 `input.data = u.name;`（`data` 是 `Binding<string>` 字段）
  当成普通赋值，把字符串指针 bitcast 成 `%Binding*` 存进槽位——后面
  `Binding_Get` 拿到的是野指针，程序一个输出都没有（5 个 binding 用例全
  DIFF）。
- 口径（宿主 `src/compiler/irgen_expr.c` 的 `emit_binding_value`）：赋给
  `Binding<T>` 槽的**非 Binding 表达式**要包成一个新建的 Binding 对象
  {target, getter, setter, live, constVal, version}：
  - 右值是**字段左值**（`obj.field`，含类内裸字段名 = `this.field`）→
    **活绑定**：target = 接收者（**非持有**的弱引用，否则 model<->view 成环
    泄漏）、getter/setter = 合成的访问器函数对、live = 1；
  - 其他表达式 → **常量绑定**：constVal = 值快照、live = 0；
  - `b = null` → 普通存储（清空）；右值本身是 Binding → 普通存储（共享）；
  - 接收者是"语句结束就死"的 owned 临时（`f().field`）→ 降级为常量绑定
    （活绑定的 target 是弱引用，临时死了 target 就悬空）。
- 做法（IR 后端）：
  - 访问器函数按委托 ABI 发射（env 在前）：`i64 @get(i8* env, i8* target)`
    与 `void @set(i8* env, i8* target, i64 v)`，字段字经 i64 槽搬运
    （指针 ptrtoint/inttoptr，double bitcast）；
  - 委托值是**带 tag 的闭包记录** `{fn, env}`（env 槽为空），不是裸函数
    指针——裸指针走委托调用的"无 env"分支，形参个数对不上；
  - 擦除的类型参数住在 i64 槽里，所以 get/set 的签名都是 i64；
  - 声明路径（`Binding<string> s = m.name;`）与字段初始化器路径要走同一
    套糖，只有赋值路径做是不够的。
- 连带：
  - **类型参数不能当类解析**：方法形参上的 `T` 只活在声明类的作用域里，
    不能被单元里同名的用户类劫持（宿主用 SYM_PARAM 修的）。自举里
    `WrapImplicitCtorArg` 会把 `num.Set(v)` 的 int 实参包成 `new T(v)`——
    用户恰好有个 `class T` 时就发出坏 IR。修法：擦除的类型参数不做隐式
    构造器转换。
  - LLVM 的函数类型在**值位置**不能加括号：`bitcast i64 (i8*, i8*)* @f to
    i8*` 合法，`bitcast (i64 (i8*, i8*)*) @f to i8*` 报 "expected type"
    （括号形式只在实参位置合法）。

## 65. 泛型重载：符号要带类型参数，显式类型实参要参与打分

- 坑（2026-10-20 实测）：自举 IR 后端的 `MethodSym`（`src/selfhost/irgen/irgen_stmt.zan`）
  只按"类名_方法名_形参类型标签"拼符号，**不带方法自己的类型参数**。于是
  `class Box { async string GetAsync(string k); async T GetAsync<T>(string k); }`
  的两个重载撞成同一个 `Box_GetAsync___string` —— LLVM 报 "invalid
  redefinition of function"，程序一个输出都没有（用例
  `generic_overload_type_arity`）。宿主是按实参单态化后拼
  `mangle_inst_suffix`（`src/compiler/irgen_emit.c`），自举擦除版要对齐的
  是"泛型与非泛型不能同名"。
- 做法：`MethodSym` 在重载符号里追加方法自身的类型参数标记（泛型重载变成
  `Box_GetAsync___T_string`），两个重载不再撞名。
- 坑（2026-10-20 实测）：**显式类型实参在 parser 里被 `StripTypeArgs` 丢掉了**，
  只留在 call 节点的 `d` 槽里（`__typeargs` 容器，kids 是 TypeRef）。重载解析
  此前完全不看它 → `b.Pick<User>(u)` 选中非泛型的 `Pick(string)`，返回值类型
  都不对（宿主选泛型那个）。
- 做法：`ScoreCand` 里按显式类型实参的个数与候选的类型参数个数匹配与否计分
  （匹配 +8、不匹配 -8），让 `M<int>(x)` 稳定落到泛型重载上。
- 边界：显式类型实参与重载打分只是"选对函数"，**不等于按实参单态化**——
  泛型重载的返回类型仍是擦除的 i64 槽，异步泛型方法（`async T GetAsync<T>`）
  的协程帧与 await 降级也还没做，别把"能选中"当成"能用"。

## 66. 宿主没有的诊断要删；调用要能推断类型；闭包要保留类上下文

- 坑（2026-10-21 实测）：自举检查器有两条宿主**根本没有**的诊断：
  - "cannot return a value from a void method" / "must return a value"
    —— 宿主 grep `src/compiler` 找不到这两条；`static void Main() { ...;
    return 0; }` 是宿主编译并运行的正常程序，自举却整份拒收（用例
    `member_index_store_valid`）。
  - "ambiguous call to overloaded 'X'" —— 宿主从不报歧义，它按自己的最佳
    匹配规则选一个。自举一报就把宿主接受的程序拒了（`Sha1.Hash` 等）。
- 口径（§60 的延续）：**宿主是规格**。自举多出来的诊断 = 自举拒收宿主接受
  的程序，一律删掉；重载打平时取第一个最佳候选，不报错。
- 连带（要让打分有意义）：**调用表达式的返回类型要能推出来**。此前
  `ArgTypeNode` 不认 `Call` → `Sha1.Hash(b.Data(), b.Len())` 的实参类型全是
  "未知" → 所有重载同分 → 判成歧义。补上 Call 的返回类型推断后，
  `Hash(byte[], int)` 才压过 `Hash(string, int)`。
- 坑（2026-10-21 实测）：**闭包里裸调的静态方法会丢掉类名**。自举在发射
  lambda 体时把 `curClass` 清空（"没捕获 this 就没有类上下文"），于是
  `CaptureSafe(exe, args)` 变成 `EmitStaticCall("", ...)` → 符号
  `_CaptureSafe`（无类前缀）、返回类型退化成 i64 → 链接期 "use of undefined
  value '@_CaptureSafe'"（用例 `log_structured`）。
  正确做法：**类上下文与接收者上下文是两回事**——`curClass` 始终保留，
  只有 `curHasThis` 随捕获走。静态字段/属性/方法在闭包里照样可解析。
- 坑（2026-10-21 实测）：**对象初始化器里的属性要走 setter**。
  `new A { Mixed = 50 }`（`Mixed` 是带访问器的属性）此前一律当字段存 →
  字段下标 -1 → "invalid getelementptr indices"；自动属性（`{ get; set; }`）
  在自举里是**后备字段**（没有访问器方法），仍走字段存。
- 补齐的内建（宿主有、自举没有）：`Dictionary<K,V>.TryGetValue(key, out v)`
  （命中才写 out 槽——宿主未命中时**不碰** out 槽）、`List<T>.LastIndexOf`、
  `NativeMemory.FindNotAnyOf` / `ScanNotAnyOf`（strcspn / strspn）。
- 门禁：每修一条，配一个 conformance 用例进 `tests/selfhost/diff_cases.txt`。

## 67. 泛型字段：存储形状按声明类型，值类型按接收者的具体实参

- 坑（2026-10-21 实测）：自举把泛型类的类型参数擦除成 i64 槽之后，**字段读回
  来的值类型也跟着丢了**：`Box<double>.values` 的声明类型是 `List<T>`，读出来
  仍是 `List<T>`（元素类型 T 依旧擦除）→ 元素被当成 i64 槽读 → 打印出位模式
  而不是 double（用例 `generic_field_value_type`）。
- 口径：宿主 `generic_field_slot` 取各实例化的**最大槽宽**（= 8 字节），所以
  两边的**存储布局**本来就一致；差的是**使用点的值类型**——宿主按接收者的具体
  类型实参替换，自举原先直接丢。
- 做法：`LoadField` 把两件事分开：声明类型 `fty` 决定**存储形状**（擦除时仍是
  i64），接收者静态类型 `recvTy` 决定**值类型**——`SubstRecvTy(fty, recvTy)`
  用接收者类型里的具体类型实参，替换字段声明里提到的类级类型参数；擦除字段
  用 `FromSlot(t, vty)` 还原。
- 连带坑（2026-10-21 实测）：`SubstTP` 之前**丢掉了数组的秩**——`T[]` 替换成
  实参后变成裸元素类型（`T[]` → `int` 而不是 `int[]`），字段存的数组指针和读出
  的元素类型对不上。数组类型节点用 `ival` 记秩、`sval` 记元素类型名，替换时只
  换元素、保留 `ival`/`sval2`。
- 边界：这套只解决**使用点**的值类型。泛型**类的方法体**是共享的（擦除），方法
  体里的 `this.item` 拿不到具体实参——那要类级单态化（宿主
  `irgen_generics.c` 的 `generic_insts` + `find_generic_fn`），自举还没有。
  别把"字段读对了"当成"泛型全对了"。
- 门禁：`tests/conformance/generic_field_value_type.zan`——把 `SubstRecvTy` 短路
  成直接返回声明类型，用例会立刻变红。

## 68. 泛型方法单态化：显式类型实参要一路带进克隆体；约束是模板体唯一的线索

- 坑（2026-10-21 实测）：自举有方法级单态化的骨架（`SpecQueue` / `CloneSubst`
  / `SpecTag`），但**实例调用**（`EmitInstanceCall`）根本没走这条路——只有扩展
  方法（`EmitExtensionCall`）和静态调用（`EmitStaticCall`）接了。于是
  `p.Describe<Conn>(c)` 选中的是**擦除**的方法体：体里的 `c` 类型是 `T`，成员
  查不到 → "call to 'c.Name' is not supported"（用例 `generic_instance_method`）。
- 做法：实例调用也走 `SpecQueue`——把方法自己的类型参数绑出来（**显式类型实参
  优先**，其次从实参类型推断），算 tag，克隆一份具体化的方法体，调用点指向克隆
  的符号。静态调用也改成显式类型实参优先。
- 坑（2026-10-21 实测）：`CloneSubst` 之前**只替换节点的 `type` 槽，不替换类型
  名**。类型节点（`AK.TypeRef`）的名字在 `sval` 里 → 克隆体里的显式类型实参还是
  `T`，嵌套调用（`Both<T>` 里的 `this.Describe<T>(c)`）于是又落回擦除形态。修法：
  `CloneSubst` 对 `AK.TypeRef` 节点改走 `SubstTP`（按 `sval` + `kids` 重建类型
  节点）。`SubstTP` 的数组分支还要保住 `op`（可空标记）与行列号。
- 坑（2026-10-21 实测）：**擦除形态的方法体里，类型参数上的成员访问不能报错**。
  宿主对类型参数做成员访问是**静默**的——`checker.c` 的成员检查只对 class /
  struct / interface 报 "has no member"，因为泛型体只是**模板**：真正的成员访问
  发生在单态化之后的体里。自举原来报 "is not supported by the self-hosted
  compiler yet" → 把宿主接受的程序整份拒了。修法：接收者类型是擦除类型参数时，
  模板体返回该擦除类型的零值（不报错）；单态化克隆体干真活。
- 坑（2026-10-21 实测）：`where T : IConn` 这样的**约束**原来被 parser 直接丢弃
  （`SkipWhereClauses`）。约束是模板体里唯一的"接收者能干什么"的线索：丢掉它，
  `T` 上的成员就只能等单态化，而**类**的方法体没有单态化。修法：约束存进类型
  参数节点（`b` 槽），IR 期按**当前作用域**查（方法自己的类型参数遮蔽类的——
  `curMethodNode`），接口约束走虚调用、类约束走直接调用 / 字段偏移。
- 口径补充：**宿主是规格**这条在"能力缺口"上也要用——宿主对类型参数上的成员
  访问不报错，自举就不能报；报了就等于比宿主更严，把宿主接受的程序拒了。
- 边界：这套解决的是**方法级**。泛型**类**的方法体仍然是共享的一份，类自己的
  类型参数在体里拿不到具体实参（`this.item` 里的 `item` 还是 `T`）——那要类级
  单态化，见 §69。自举的 **ngen 后端**已经有一版类级单态化（`ngen.zan` 的
  `SpecRegister` / `SpecRegisterClass` / `RegisterInstCls`），搬到 LLVM 后端就是
  照它设计的。

## 69. 类级单态化：类的类型参数要从**接收者的静态类型**里绑出来

- 坑（2026-10-21 实测）：泛型**类**的方法体是共享的一份**擦除体** → 体里
  `this.item` 的类型还是 `T`（不是 `string`），方法级单态化管不着（它只绑方法
  自己的类型参数）。宿主是连**类的类型参数**一起单态化（`irgen_generics.c` 的
  `generic_insts` + `find_generic_fn`）→ 每个实例化一份方法体。
- 做法（`irgen_expr.zan` 的 `EmitInstanceCall`）：接收者静态类型带了具体类型实参
  （`Pool<string>` 的 kids 数量 == 类的类型参数数量）时，把**类的类型参数**绑成
  这些实参，与方法自身的类型参数**合并成一次克隆**（不是两次——两次会克隆出两个
  体，符号还会撞）。克隆体的 `this` 静态类型 = 具体实例化（`Pool<string>`）→
  体里的字段读回才有真实值类型。
- 坑（2026-10-21 实测）：克隆体和普通体在同一个队列里，符号名是
  `Owner_Method__SP<tag>`。类级和方法级若参数列表相同 → 同一 tag → 抢同一符号
  → **先注册的体赢**，后面所有调用点都打到错的体上。修法：tag 加前缀区分类级
  （`"I"`）与方法级（`""`）。
- 坑（2026-10-21 实测）：`this` 的静态类型不是 `curClass`，而是"当前发射的方法的
  接收者类型"。引入 `CurThisTy()`（`curThisTy` 字段，默认 `MkType(curClass)`），
  克隆体发射期间由 drain 循环设成 `specThis[sp]`（**保存/恢复**，否则后续普通
  方法会继承克隆体的 `this` 类型）。
- 边界：类级克隆只对**本类自己声明**的方法做（`owner == clsName`）。继承来的方法
  （基类没有类型参数）不需要克隆；**虚方法不克隆**——vtable 指向未特化的定义，
  克隆体不会被虚调用到，克隆了是死代码。
- 门禁：`generic_param_shape_overload` / `generic_field_33` 由 DIFF 转 MATCH。

## 70. async 方法的函数类型**恒**返回协程帧指针（`async void` 也不例外）

- 坑（2026-10-21 实测）：`MethodFnTypeThis` 用**声明的**返回类型 → `async void`
  的函数类型是 `void (%Pool*)`，而**定义**返回的是协程帧 `i8*` → vtable 项与定义
  的类型不一致；而且 await 拿不到帧指针 → "this operand is not lowered as a
  coroutine frame"。
- 口径：async 方法的函数**恒**返回 `i8*`（帧指针），与声明的返回类型无关；
  `async int` 的结果在帧里，不在返回值里。
- 连带坑：`EmitVirtualCall` 里 async 判断必须**在** void 分支之前，否则虚调用
  `async void` 方法时帧指针被吞掉。
- 门禁：`async_interface` 用例（async void 经接口虚调用）。

## 71. 泛型类型作形参类型：parser 的"定形"前瞻要扫平衡尖括号

- 坑（2026-10-21 实测）：`using (ConcurrentDictionary<string, int> cd = ...)` 报
  "syntax error: unexpected ','"。`LooksLikeTypedParam` 的前瞻只认 `ident ident`，
  遇到 `Name<...> name` 就判成表达式 → 把 `Name<` 当比较表达式解析 → 在 `,` 上炸。
- 做法：前瞻里跳过**平衡的**尖括号组（`<` / `>` / `>>` 一起计数），再看后面是不是
  标识符。表达式 `a < b` 会跑出角括号组 → 找不到名字 → 仍读作表达式。
- 通用教训：凡是"扫两个 token 判断形状"的前瞻，遇到**带参数的语法**都要扩；否则
  新语法一加就误判（这类坑在自举里反复出现：泛型类型参数、类型实参、where 子句）。

## 72. try/finally：`return` / `break` / `continue` 必须跑 finally（否则死锁）

- 坑（2026-10-21 实测）：自举的 `GenTry` 只在 try 体**正常结束**时跑 finally
  （`if (!term) { ...; GenStmt(s.c); }`）。try 体里一个 `return` 让 `term` 变成
  true → **finally 被跳过**。`ConcurrentQueue.Count()` 正好是这个形状：
  `try { return this.inner.Count(); } finally { Mutex.Unlock(this.mtx); }`
  → 锁永远不释放 → 第二次 `Lock` 同一把 SRW 锁**死锁**（程序无输出、像是卡住）。
- 口径：C# / C 宿主都会在 return 上跑 finally。自举原来不跑 = 语义缺失，不是
  "能力缺口"。
- 做法（照 ngen 的 pending-finally 模型）：每个 try 入栈一条
  `{finally 体, EH 帧是否还在}`；`return` / `break` / `continue` 离开 try 区域时，
  由内向外把还在栈上的 EH 帧弹掉（`@__exc_sp` 必须回落，否则帧数组会越界），
  再依次跑 finally 体。finally 体自己再 return 时靠"正在执行"的标记防止自我
  递归。break/continue 只跑**循环内部**打开的 finally（记一个循环基线）。
- 连带坑：finally 体在 try 体的**正常路径**和 **catch 路径**上各有一份拷贝
  （原代码就是这么发的），别以为 finally 只发一次。
- 坑（2026-10-21 实测）：finally 体在 try 区域里被**内联复制**到每个退出点上，
  所以 finally 体不能假设"只被发一次"；发两份以上时，局部变量槽位会重复注册，
  作用域要各自收干净。
- 异步 `finally` 跨 `await` 时，待定返回值与异常由协程帧持有。处理器首次武装时保存
  待定栈深度，恢复时沿用；退出活动 `finally` 时按它保存的深度释放失效记录，并保留
  仍有效的外层记录及当前接替的控制转移。任务结果槽在完成前也可暂存 I/O 数值，放弃
  未完成的帧时只按记录的析构函数释放待定载荷。回归覆盖 `async_finally_pending_owners`、
  `async_finally_scope_transfers`、`selfhost_decimal_wide` 和发布运行时的 `mt/async_timer.zan`。
- 内联 `finally` 时按声明点解析名字、循环目标和裸 `throw;`。保留出口侧 ARC 所用的局部槽和
  异常清理栈，词法重抛只选择异常槽。分派持有独立引用，让捕获并返回的内层处理器可以正常
  释放自己的异常。前向 `goto` 补发 `finally` 时还原出口侧异常处理器。
- `goto` 先按函数和最近可见的 `finally` 选择标签，再以 try、各 catch、finally、lock 的
  词法身份限制入口。克隆用独立标签 ID，前向引用保存源点绑定的 ID。回归
  `finally_lexical_context` 和 `goto_into_try` 覆盖异常重抛、同名标签和前后向跳转。

## 73. 泛型擦除必须**按作用域**判定：用户类名不能被同名类型参数击穿

- 坑（2026-10-21 实测）：自举的擦除判定用一份**全局**类型参数名表
  （`tparamNames`），于是 `class T`（用户类）和 `Binding<T>`（类型参数 T）
  同名时，`T` 处处被当成擦除类型 → 用户类被降成 i64 槽 → 程序把指针存进
  i64 槽，`store i64 %t66, i64* %v.victim.4` 直接是坏 IR（用例
  `binding_tp_shadowed_by_user_class`）。
- 口径（C# 语义）：**类型参数只在自己声明的作用域里遮蔽同名用户类型**。
  `Binding<T>` 的体里 `T` 是参数；`Program` 里 `T` 是用户类。
- 做法：擦除判定改成"作用域感知"——
  1. 名字是**当前类/当前方法**的类型参数 → 擦除；
  2. 否则程序里有同名**用户类型** → **不擦除**（那是用户类）；
  3. 否则 → 擦除（别的类的类型参数）。
  判定用的作用域不是恒等于"当前类"：**别的类的方法**的声明类型写在**声明类**
  的作用域里（`MapTypeDecl` / `IsErasedTypeDecl`），**类布局**、**析构函数**、
  **静态字段**的类型也写在**各自类**的作用域里。
- 连带坑：类布局（`%C = type { ... }`）和析构函数是在**类上下文之外**发射的，
  忘了切作用域 → 泛型字段被按用户类型布局（`%Binding = type { ..., %T*, ... }`）
  → 析构函数还会去 release 一个其实是 i64 槽的字段 → **访问违例**。
- 连带坑：方法级/类级单态化之外，**索引器与属性访问器**（`op_index` /
  `op_index_set` / `set_X`）也必须走类级单态化——它们的形参/返回值是类的类型
  参数，擦除形态会把 string 键按**数字**格式化（`snprintf("%d")`）后塞进字典，
  查不到就抛异常。
## 74. 泛型类的构造器：实参推断用**具体类型**，调用 ABI 仍是**擦除形**

- 坑（2026-10-21 实测）：`LabelOf<Opt>` 这类泛型类的构造器，形参是委托类型
  `delegate Label<Opt> (Opt)`。自举用**声明的形参类型**（`T` → 擦除的 i64 槽）去推断
  实参 → 传进去的 lambda 拿到的是**擦除的形参类型** → lambda 体里写什么都不生效
  （用例 `generic_nested_generic_field`）。
- 口径：擦除只影响**存储形状**，不影响**实参推断**。lambda 的形参类型必须来自
  接收者的**具体类型实参**。
- 做法：调用构造器时，形参的**推断类型**用 `SubstTP(pty, ctps, ty.kids)`（具体），
  但**实参的槽形状**仍用 `MapType(pty)`（擦除）——构造器体是**共享**的（不克隆），
  它按槽收参数，槽宽按声明类型；把 double 之类的具体值直接塞进 i64 槽会被截断。
- 连带坑：泛型类的**引用类型字段**在**存字段**的那一刻要 retain。字段**拥有**它
  持有的引用；克隆体里值类型已知，不 retain 的话字段存的是借用指针，
  调用方释放临时后字段悬空 → 下一次读是 use-after-free → 堆损坏。
## 75. 泛型类的静态成员按**实例化**隔离（宿主的 mangle_inst_suffix 语义）

- 坑（2026-10-21 实测）：自举给泛型类的静态字段只发一个全局
  （`@__zan_sf_Stat_s`），于是 `Stat<int>.s` 与 `Stat<double>.s` 是同一格，
  `Stat<int>.Bump()` 加的是所有实例化共享的计数（用例 `generic_statics`：
  自举 i=6/d=6/s=6/f=7，宿主 i=4/d=4/s=5/f=5）。静态字段的**初始化器**同理：
  `public static int seq = Seq.Next();` 必须**每个实例化各跑一次**，否则
  70 个实例化的求和差一个（用例 `generic_inst_count_70`：宿主 2485=1+…+70）。
- 宿主口径（`irgen_emit.c` / `irgen_arc.c`）：
  - 宿主给每个泛型类发**一个擦除变体 + 每个具体实例化一个变体**；
  - 静态字段的全局名按实例化加后缀（`mangle_inst_suffix`）→ 每实例化一份存储；
  - 静态字段的**存储形状不随实例化变**（宿主用声明类型 `map_type(fsym->type)`，
    只改名字不改类型）——别自作聪明把 `T` 换成具体类型，形状会和宿主不一致；
  - 静态字段初始化器与 cctor 只对**执行闭包激活的闭合实例**各跑一次，
    `typeof(Stat<double>)` 等仅保留元数据的实例不跑，擦除变体也不跑；
    发现实例化用于准备存储/布局，不能把发现集合直接当初始化需求集合；
  - 泛型类的静态**方法**也按实例化各有一份（`Stat<int>.Bump()` 走
    `Stat<int>` 的那份方法体）。
- 做法（自举侧）：
  1. parser 要**保留类型实参节点**（不能只留名字）：`Stat<List<int>>.s` 的
     实参名字拆不回来。`StripTypeArgs` 把 `__typeargs` 容器留在 MemberAccess
     的 `d` 槽（`sval2` 的名字串继续给 ngen 用）。
  2. 发射前先扫一遍 AST 收集**所有具体实例化**（宿主的 `discover_generic_insts`）：
     类型引用（`new Wrap<T0>()`、字段/局部类型）与泛型限定的成员访问
     （`Stat<int>.s`）都要收。静态存储要在发全局之前就知道有哪些实例化。
  3. 静态字段的全局名 = `@__zan_sf_<类>_<实例化标签>_<字段>`；实例化标签由
     实参的类型名拼成（`int`、`double`、`string__int`）。
  4. 静态方法按实例化走**类级克隆**（和实例方法同一套 `SpecTagC` + `I` 前缀），
     克隆体里的 `Stat<T>.s` 经类型实参替换后指向该实例化的全局。
  5. 克隆体里的**裸**静态字段（不带 `Stat<...>` 限定）要用"当前克隆的实例化"
     定位存储：克隆的接收者静态类型（`curThisTy`）就带着实参。
  6. cctor 同样按闭合类实参克隆；其中裸调用的静态 helper 要回落
     `CurInstFor` 取得当前闭合实参，静态字段仍保留声明形状的 ABI。
