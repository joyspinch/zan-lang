# 内建类型成员表与 await / 协程调度契约

## 五、内建类型成员表与代码生成的一致性契约

43. **内建成员表签名必须与实际发射的返回类型一致**：
   - 动作：`src/compiler/builtin_api.c` 的成员表（`members_*`）里声明的返回类型，必须与 `irgen_call.c` / `irgen_builtins.c` 中该成员实际发射的 LLVM 返回类型逐个对齐；改实现语义必须同改表里的签名（或反之）。
   - 理由：类型检查只认成员表，代码生成只认发射代码，两边脱节时**类型检查先拒绝合法用法**，而运行期其实是对的——症状是"明明实现了却编不过"，极难定位。实例：`Dict.Remove` 表里写 `void Remove(K key)`，实现却返回 0/1 标志（`irgen_call.c` 命中置 1、落空置 0），于是 C# 惯用法 `bool rm = d.Remove(k)` 被判 `cannot convert 'void' to 'bool'`，`case_arc_lookup_owned_arg` 连带它的泄漏断言（同一次运行）一起红。
   - 边界：内建集合方法（`Dict` / `List`）的"命中/落空"一律用 0/1 标志返回，对齐 C# `Dictionary.Remove` 的 bool 语义；不要把槽位下标当返回值暴露出去。
44. **内建表就是公开面，改它等于改 API**：
   - 动作：内建成员表同时是 IDE 补全与 LSP 的数据源（`zanc --emit-symbols` 写出的类型/成员索引），改签名后必须同步 `docs/STDLIB.md` 对应节，并留一条 conformance 用例钉住语义。
   - 理由：表里的签名直接进 IDE 补全与类型检查；声明与实现不一致时，IDE 补出来的用法与编译器的判定会互相打架，用户看到的是"补全说能写、编译器说不能写"。
45. **批量改可见性（`private` / `public`）属于跨边界公共面改动**：
   - 动作：把成员改成 `private` 或补 `public` 之前，先查它的跨包调用方（`tools/agent/impact.ps1` / `map.ps1 -Callers`），并定向跑受影响的用例。
   - 理由：可见性一改，**外部调用点立刻编译不过**，而这类破坏不会自己暴露——实例：`Socket.ResolveIpv4` 被规范成 `private` 后，`case_async_dns` / `case_async_mt` 两个用例从那天起一直红，直到有人手工重跑才发现（档位日志停在改动前一天）。
   - 边界：默认可见性是 public（无修饰符即 public），显式 `private` 才是收窄；规范化的目的是让可见性**显式可读**，不是顺手收紧 API。
46. **内建静态调用的返回类型必须按实参形态定型，并与 irgen 的分支一一对应**：
   - 动作：同一个内建静态成员（`Task.Run` / `Task.Spawn` 这类）传不同形态的实参时，若 irgen 会走**不同的帧/所有权分支**，检查器就必须给不同的返回类型（无类型实参的"无结果类型" vs 带类型实参的 `Task<T>`）；检查器不得对内建调用的返回类型一律宽松放行（`type_error` 对一切可赋值），也不得把"结果类型"一律写成 `long`。
   - 理由：宽松放行会让"把无结果句柄当 `Task<T>` 用"一路通到 IR 与运行期，直到帧被释放两次才 abort；类型侧收口后，误用在编译期就被拒绝，运行期那条 double free 路径根本不可达。实例：`Task.Run(lambda)` 产出的是无结果 action 帧，把它声明成 `Task<int>` 去读 `.Result` 曾经是运行期 double free（已闭账，见 TASKS.md A380 的残留项）。
   - 边界：改检查器的定型前，先读 irgen 里同一调用的分支（哪个实参形态走 keep_result、哪个走 action 帧），两边必须对得上；类型说 A、代码生成做 B 是最难查的一类 bug。
47. **内建成员表里的类型名不是可见符号，跨命名空间引用仍要显式 `using`**：
   - 动作：`src/compiler/builtin_api.c` 的类型表（`BT("Aes", "Aes", ...)`）只服务于
     **成员签名的类型定型**（`Aes.Encrypt` 的返回类型是 `Vector128`），不参与名字
     解析；引用某个类必须让它的命名空间真正进入作用域（文件头的 `using`）。
   - 理由：表里有 "Aes"（`System.Runtime.Intrinsics.X86.Aes` 硬件原语），但
     Wechat SDK 里的 `Aes.DecryptBlockEcb`（`System.Security.Cryptography.Aes`，
     在 Zan.Security 包里）报的是 "'Aes' is not a known variable, type, or
     namespace"——**未知名**而不是"未知名成员"，极易误判成"编译器漏注册内建
     类型"；实际只差一行 `using System.Security.Cryptography;`，同包的另一个
     文件早就写了这一行。
   - 边界：同名类分处不同命名空间时，先确认 import 的是哪一个；内建表的签名
     按硬件原语那份算，名字解析归 `using`，两套机制不要互相推断。
48. **改动成员可见性、或把实现搬去别处之前，先按"族"核对公开面**：
   - 动作：可见性规范化（批量补显式修饰符）收窄成员时，判定基准是
     **同族成员**（同一类里其它同角色成员）＋**用例的静态直调**；凡是用例以
     `Cls.Member(...)` 形式直接调用、且该成员是自足工具（不依赖外部状态）的，
     就是公开面的一部分，应保持 public。**把实现从 A 处搬到 B 处（抽包）是
     同一条纪律**：搬迁前把旧类上的成员按名字列一遍，逐个判定它在**新位置**
     是否还可达；搬走的实现要在新位置补上等价公开面，旧位置保留转发入口。
   - 理由：规范化曾把 38 个这类成员收成 private，**22 条 conformance 用例直接
     编译失败**——红灯藏在编译错误里，看起来像"用例坏了"，其实是公开面被误收。
     铁证：`WechatReply.Text` 被收窄而同族 `Image`/`NewsItem`/`News`/`Build`
     全是 public；`MySqlConnection.readBinValue` 被收窄而
     `MySqlSyncConnection.readBinValue` 本来就是 public（无修饰符=public）。
     搬迁方向的同类事故：编码器实现抽到独立包时，旧类上 `static int
     SeqByteLen(int lead)` 这类**无修饰符**成员（无修饰符=public）被一起删掉，
     而它们是外部项目的既成公开面——仓库自己的 GUI 文档还在教用户这么调；
     抽包后老代码报 `has no member` + `unresolved call` + `code generation
     failed`，绕行（改用新包的 using）还会与旧命名空间撞名。
   - 边界：默认可见性是 public（无修饰符即 public），显式 `private` 才是收窄——
     规范化的目的是让可见性**显式可读**，不是顺手收紧 API；收窄前先问"这条成员
     有没有对外服务的义务"，而不是"它原来有没有 public 关键字"。搬迁后的兼容面
     只转发**纯函数**（无表、无共享状态），规格表与算法留在新包、用**全限定名**
     可达——全限定名不撞名，同时导入两个命名空间时裸名才会歧义。
     第二轮同类事故（cf7e2571e，Zan.Net 47 文件规范化）：`HttpClient.BuildRequestHead`/
     `BuildDownloadRequest`/`HasHeaderName` 与 `TlsStream.engine` 被收窄，4 条以上
     conformance 编译失败。自足的报文构建器恢复 public；**可变字段**不恢复成 public 字段，
     改给只读属性（`TlsStream.Engine { get }`）、用例跟着改名——外部要的是"看握手结果"，
     不是"换掉引擎"。

49. **只查不声明 = 静默跳过**：
   - 动作：编译器要调用**外部符号**（libc / Win32 API）时，必须用
     `LLVMAddFunction(g->mod, "名字", 类型)` **声明**进模块；
     `LLVMGetNamedFunction` 只是**查**（查不到返回 NULL），拿它当"要不要
     发射调用"的条件就等于"符号不在模块里就整段跳过"。
     正确写法是「先查后声明」：`Get` 命中就复用，没命中就 `Add`。
   - 理由：符号本来就不在当前模块里（链接期才由系统库/libc 解析），
     所以 `Get` **必然**返回 NULL。原实现在"顶层非异步上下文"的
     `await Task.Delay(ms)` 分支里只 `Get` 不 `Add`，于是整段等待被
     **静默跳过**——`await Task.Delay(500)` 编译成一条 `ret`，方法立刻
     返回。实测请求 500ms、实际耗时 0.0002ms，而**编译无警告、运行无
     异常**。同类正确写法在 `irgen.c` 的 `zan_io_pump_timeout` 里已有
     （`Add` 之后再调用），两处不一致正是缺陷的来源。
   - 边界：这条的代价是"漏发一个调用"而不是"发错一个调用"，所以症状
     是**时间/时序不对**（等没等、睡没睡），不是崩溃——排查时先怀疑
     "这段代码是不是压根没生成"（`--emit-ir` 看 IR 里有没有那个调用），
     而不是先怀疑参数算错。**新增外部符号时优先复用已有的运行时
     包装**（`zan_*`）而不是裸 libc 调用，否则每个交叉工具链的
     `zanrt_*.o` 都要跟着重建。

---

---

## 六、await 语义、协程调度与工具链时效

50. **同步上下文里的 `await` 是阻塞式根驱动，不是让出、也不是丢弃**：
   - 动作：判断同步方法里 `await` 的行为时，先分清**操作数是不是协程帧**：操作数是
     async 调用 → 代码生成走**根驱动**分支，自己把子协程推进到完成再取结果（同步方法里
     `int r = await AsyncCall(21)` 拿到 `r=42`）；操作数是需要帧的原语（`Task.Delay` /
     `Task.Yield`）→ 没有帧，顶层分支只做同步休眠或直接返回 0（`Task.Delay` 那条曾因
     「只查不声明 = 静默跳过」整段被跳过，已修复；`Gate.Park` 有专属诊断）；**裸调用**
     async 方法（不写 `await`）→ 只创建帧并入队，协程体不在调用点执行（宿主平台上
     照样会跑完，见本节「宿主平台的协程调度是多 worker 池」）。
   - 理由：把「同步方法里的 await 一律拒绝」写成语义检查规则，会把全仓上百个合法文件一次
     判死（实测 150 个受检文件里判死 146 个：tests 109 / examples 20 / templates 7 /
     src 6 / packages 4），而它与仓库既有约定直接冲突——自举固定点 `tests/selfhost/prog1.zan`
     的 `static void Main()` 里就有 `await P.AsTwice(21)`，`src/selfhost/frontend/checker.zan` 的
     CheckAwaits 明写同步方法里的 await 是合法的。**判据只能是「操作数是不是帧」，不是
     「方法是不是 async」**。
   - 边界：这条只讲语义，不讲「可不可以写」——同步方法里写 `await` 完全合法，代价是
     **占住调用线程直到子协程跑完**。要并发不阻塞就把方法声明成 `async`，那时 `await`
     才是挂起/恢复。判断同步方法里的 `await` 是否合理，看的是「我愿不愿意在这里阻塞」。
   - 判定器：`tests/conformance/async_sync_delay.zan`（同步等待真的等了、结果取对）、
     `tests/conformance/async_void_call.zan`（裸调 async void 的语义）。

51. **宿主平台的协程调度是多 worker 池，不是 M:1 协作式单线程**：
   - 动作：写「协程怎么被推进」的注释或文档前，先确认**目标平台**：宿主平台（Windows /
     Linux / macOS，x86_64 / aarch64）链的是多 worker 池那份运行时（`zanrt_io_mt.o`，
     `ZAN_CO_DRIVER` 变体），多个 worker 线程各自跑协程；M:1 协作式就绪队列那份实现
     （`rt_co.c`）**只在交叉目标**用，普通宿主程序的链接集里根本没有它。裸调用 async
     方法的协程照样会跑完，**不需要**主循环泵调度器。
   - 理由：「协程是 M:1，await 挂起后由调度器在同一个线程上恢复」「裸线程里 await 既
     等不到也占着线程不放」这两句一旦写进包注释，就会误导后来人按它改形状或给 GUI 主
     循环额外加泵。改法（裸调 async void）实测是对的，**理由是错的**。
   - 边界：这条讲的是**宿主平台**。交叉目标（esp32 / wasm32 等）走另一套：就绪队列没有
     worker 线程，必须由程序自己泵——两边的结论相反，别把一边的话抄到另一边。
   - 判定器：`tests/conformance/async_runtime_workers.zan`（worker 数可配）、
     `tests/conformance/async_mt_pool.zan`；二进制指纹：生成的 exe 里有 `co_worker` /
     `co_worker_thunk` / `g_co_workers` 符号。

52. **改了源码 ≠ 消费者用到的二进制变了**：
   - 动作：面向仓库外消费者（外部项目、已发布 SDK）的编译器改动，必须**显式重建消费者
     实际使用的那份二进制**再验证；汇报「我改了编译器」之前先核对那个二进制的
     **时间戳**。断言「某行为存在/不存在」时，用**两个不同时间戳的二进制**跑同一个
     未改动文件对照，才能把「源码差异」和「二进制差异」分开。
   - 理由：一条只在工作区存在过、从未进 git 的「await 只能在 async 方法里」诊断，随
     **过期**的编译器二进制漏到仓库外的全部项目（实测 19 个 GUI 项目整包编译失败）——
     看起来像项目代码坏了，实际是二进制没重建；重建后同一批项目立刻通过。
   - 边界：这条与 `references/selfhost-closure.md` 第 40 条「没有差分门 ≠ 没有检查」末尾那句
     「门禁失败先看 `build\zanc.exe` 的时间戳」是同一条坑的两个面——那条讲差分门，这条讲外部消费者。
     **「源码改了」和「用到的二进制变了」是两件事**。
53. **LLVMAddGlobal/LLVMAddFunction 的第一参是模块，不是上下文**：
    - 动作：发射全局与函数一律 `LLVMAddGlobal(g->mod, ...)` /
      `LLVMAddFunction(g->mod, ...)`；LLVM C API 形参全是 void*，把
      `LLVMContextRef` 误当模块传入**编译不报错**，首次解引用才段崩
      （0xC0000005）。
    - 理由：纯类型混淆，无任何编译期诊断；段崩点离出错点远（崩在
      LLVMSetInitializer/LLVMSetGlobalConstant 等后续调用），肉眼 review
      很难看出 c 与 mod 之差。坑出处：char 大小写映射发射（A525 闭环的
      charcase 表）首版误传 context，一编译就崩。
    - 边界：发射路径的段崩用 fprintf 进度标记二分（表构造前/后、函数创建
      前后各一个），两轮内必中崩点；比事件日志解析或盲二分快。
54. **EH 临时栈的协议三件事（写泄漏修复前必读）**：
    - 槽是**地址**不是值：mit_eh_tmp_push_slot 推的是带 kind 标签的
      槽地址，展开器 load-and-null 后按 kind 释放**当前**引用——字段重
      赋值、循环内重复 push 同一槽都天然安全（第二次读到 null 是 no-op）。
      坑出处：A528 结构体 ctor 字段泄漏的修复就是靠把字段槽推上栈收口。
    - disarm 只改栈顶指针，**必须**在局部变量 EH 出栈之后：先 disarm 把
      尚未出栈的局部槽位闷在栈里，随后的 pop 按计数递减，下溢冲掉调用方
      帧。显式 return 路径的顺序（catch 异常→局部→disarm）是既定口径。
    - **裸武装**（无 handler 的纯深度快照）+ 既有 disarm/unwind 就能兜住
      新的泄漏类别，不必加运行时函数或新槽类型：入口存 old_top，正常返回
      走 disarm 免释放回退，抛出走 unwind 按槽 kind 释放。A528（结构体
      ctor 字段）零运行时改动闭环即此模式。

    - **实例调用保住已加载的接收者值**：在 null/scratch 替换及实参求值前
      retain 借用值；owned local 也不能豁免，`receiver.M(Clear(ref receiver))`
      会先释放槽里的旧值。owned 表达式沿用其 +1；正常在调用边界释放，EH
      用既有临时栈收口。class/interface 的借用与 owned 接收者统一走
      `emit_eh_tmp_push_obj`：tagged delegate 必须编码为 DLG，一次登记对应一次
      pop，实参先退再退接收者；原始 tag 不能当 EH kind 解码。
    - **自举同口径**：`EmitInstanceCallAs` 在 guard/参数前通过
      `HoldCallReceiver` 转移已有 temp owner 或 retain 借用值，复用 local-owner
      链兜 EH，返回时 `DropCallReceiver` 清槽并释放；不能只 AddTemp 拖到分号。
      delegate→interface cast 复用 `EraseDelegateReference`，转移源临时所有权。
      判定器 `arc_callback_receiver_lifetime` 用 weak witness 同时验证调用内存活、
      ref 清空、嵌套参数异常、tagged capture 释放、返回 this 同表达式退役；
      读回旧标量不能证明对象仍存活。独立 failed-as 析构时机不随此修复扩展。

55. **ARC 分配点编号从 1 起，site 0 是运行时的"未跟踪"哨兵**：
    - 动作：`reserve_arc_site` 的站点索引一律从 1 开始分配（0 号槽永远
      留空，去重循环也从 1 起扫）。
    - 理由：运行时把对象头里的 site==0 当作"无站点"——
      `refl_obj_type_fn` 的动态类型解析带 `site > 0` 守卫，rt_alloc 零
      初始化的头部天然是 0。站点若从 0 起数，第一个抢到 0 号的类型
      （往往是主文件里第一个带分配的类——主文件先于 stdlib 发射），
      其所有对象的动态反射 Set/Get 会被守卫拒绝而**静默失效**（读回
      恒 0，无任何诊断）。坑出处：ORM 探针式列名检测（对 dummy 实体
      写入哨兵值再回调 lambda 验证）在插桩构建下全部落兜底列，聚合
      SUM 变成 SUM(主键)；最小复现只差一个"类内 static 方法含 new"。
    - 判定器：conformance 反射用例在 `--check-leaks` 下的读写回读；
      探针 pOff 系（同构类去掉 static 方法即恢复）。
56. **覆写丢弃的引用字段旧值必须走 dyn 释放，浅释放会整串泄漏**：
    - 动作：任何"覆写引用字段/槽位"的发射路径（反射 SetFieldObject
      的 FK_REF 分支等），丢旧值必须调 `zan_rt_release_dyn`（按对象头
      的 dtor 分发到类型化释放体）；`zan_rt_release` 是浅释放——只减
      计数不 walk 字段。
    - 理由：旧值若是类实例，浅释放后它的 string/引用字段全部滞留——
      ARC 泄漏计数线性增长且 per-site 归因缺失（泄漏对象在别的 TU 里
      分配时站点表对不上）。坑出处：ORM Include 每轮覆写导航字段漏
      约 3 个对象（导航实例+其名字串），10 次循环漏 30 的探针实锤；
      同构探针把覆写改成局部变量作用域释放就干净， proving 问题只在
      覆写路径。
    - 边界：覆盖前旧值可能为 null（rt_release_dyn 首判空，安全）；
      字符串字段用 rt_str_release 的同理只适用于串本身。
57. **反射 Set/Has 类名字实参的所有权判定必须显式做**：
    - 动作：`TypeInfo.HasField/HasMethod` 等吃名字串的反射内建，发射
      实参后必须调 `emit_release_owned_call_temp`——拼接/调用产出的
      fresh owned 名串在消费完释放；局部变量/字段/字面量是借用，
      该函数内部识别并安全跳过。
    - 理由：这些内建直接 `emit_expr` 吃掉实参却从不释放，产 owned 的
      实参每次调用漏一个串；循环里 `HasField(a + b)` 每轮漏一个，
      泄漏量与迭代数精确线性（n 次调用 n 个对象），极易误判为
      "缓存泄漏"。坑出处：ORM Include 循环里 `HasField(fname + "Id")
      || HasField(fname + "id")`，10 轮漏 40。
    - 判定器：同构探针——把实参从字面量换成拼接，泄漏量应保持 0。
58. **泛型实例收集路径没有词法作用域（locals=NULL）**：
    - 动作：`collect_inst_expr`（收集泛型实例化清单）调用
      `method_ret_type_at` 等类型推断时传的 locals 是 NULL；被调链里
      凡是要把实参表达式交给 `infer_expr_type`/`local_find` 的推导
      （lambda 形参绑定、实参类型合一），必须先判空提前返回——
      `local_find` 对 NULL scope 直接解引用。显式 type_args 的绑定
      不需要作用域，保留。
    - 理由：`M<T>(json, x => ...)` 这类"显式类型实参 + lambda 实参"
      的调用走进收集路径就在编译期段错误；程序里只要有一个这种调用
      整个编译崩。坑出处：JSON 门面 `ParseScoped<T>(json, lambda)`
      编译即崩，json_scoped_scratch/reflect 系用例全红；修复只加了
      一个 `if (!locals) return;`。
    - 边界：collect 路径丢了 lambda 形参类型带来的 TP 绑定能力，
      但显式 `<T>` 绑定已够用（返回类型代换仍正确）；若未来要在
      collect 里做完整推断，需先给收集器建独立作用域。
59. **分配点登记必须先于一切按站点索引的表发射（反射 thunk 时序）**：
    - 动作：所有 `reserve_arc_site` 调用必须发生在 irgen_emit.c 尾部的
      `emit_site_live_tables` / `emit_site_dtor_table` /
      `emit_site_tyname_table` / `emit_site_meta_table` /
      `zan_irgen_emit_arc_desc_init` 之前。反射 ctor thunk
      （`refl_finalize_mtabs`）原本在名表冻结后才 reserve 新站点，反射
      `CreateInstance` 造出的对象按站点查类型名落空，一切 is/下转恒假。
      现以 `refl_reserve_thunk_sites` 前置登记（mtabs 带 site 字段），
      thunk 构建只取用预登记站点。
    - 理由：运行时 is/下转按"对象头 site 字 → 站点类型名表"判身份，
      且有两个模式：desc_hdr（desc 指针表）与 check_leaks（站点索引表）
      ——冻结后补登记的站点两个模式都查无此名；只调换 desc/finalize
      顺序会修好一个模式、弄混另一个（thunk 退回索引模式与正常分配的
      指针模式混搭），必须用"登记前置"而非"换序"。
    - 判定器：orm_reverse_relation / orm_typed_query /
      orm_metadata_concurrency；最小探针 =
      `typeof(T).CreateInstance()` 后 `(T)obj` 判型（修复前得 OTHER）。

60. **strtod 系解析的溢出判定只能走 endp 零消费约定，不能按值判 INF**：
    - 动作：`double.Parse` / `Convert.ToDouble` / `TryParse` 的内建展开
      （irgen_call_builtins.c）共用 `zan_rt_dbl_parse`（rt_timer.c）。
      判"输入溢出"（C# OverflowException / TryParse false）必须在运行时
      `errno == ERANGE` 且结果为 ±INF 时把 `*endp` 拨回起点，让既有的
      `emit_parse_bad_pred` 零消费判坏（`ep == s`）接住；不要在 IR 侧
      `fabs(r) == DBL_MAX*2` 按值判 INF——`"Infinity"` 特殊拼写（93ac441b3
      的 C# 可回读契约）合法返回 INF，按值判会把回读一并杀成 0
      （case_double_format 的 `inf > 1e308` / `ninf < -1e308` 变假由此）。
    - 理由：溢出与合法 INF 在值域上同形（都是 ±INF），值不可区分；
      唯一的区分点是"输入是拼写还是数字串"，只有运行时能看到 errno，
      IR 侧只能看到值。约定终点收敛在 parse 家族已有的 bad_pred，
      三处调用点（Parse 抛 / TryParse false / Convert 软诊断+0）零改动。
    - 判定器：case_double_format（Infinity 回读两侧比较）+
      case_convert_overflow（真溢出仍要判坏）+ case_builtin_parse_chain。

61. **泛型类里的 static Main 不是合法入口——入口选择必须拒收，否则开放编译静默产坏码**：
    - 动作：入口选择（irgen_emit.c 的 Main 扫描）跳过 `type_params.count > 0`
      的宿主类（__DesignMain 同守卫）；全程序无入口且仅见泛型 Main 时报
      E3102 清晰诊断，不再落到链接期 undefined main 的哑错。
    - 理由：入口体按"无宿主实例化"编译，体内对该类类型参数的一切显式引用
      （调用类型实参如 `db.SyncStructure<E>()`）绑回开放参数——扩展侧烟枪
      实证收到 `entity=T cols=0` 的空 Meta，Sync 产 `CREATE TABLE ()` 报
      `near ")"`；同形调用在实例方法（有宿主实例化）与经实例化链调用的
      static（p33）都正确，唯入口路径漏环境。C# 原版即禁止泛型类入口。
    - 判定器：`class Repo<E>{ static void Main(){ db.SyncStructure<E>(); } }`
      须报 E3102；实例方法/static 经实例化链两形照常。
    - 连带纪律：新增诊断码先查已占用编号（本次初版误用
      已占用的 3092，靠补文档行时纠出改号 3102）。
62. **反射字符串内容池的判重必须比 initializer 完整相等，不能只比 hash 或名字**：
   - 动作：反射元数据字符串入私有内容池时按内容 hash 定名、碰撞序号兜底；同名全局只在
     记录类型与常量属性一致且 initializer 完整相等（长度/头/含 terminator 的负载逐项相同，
     LLVM 常量唯一化即按完整 initializer 判等）时复用，否则递增碰撞序号换名重建。
     池项用 `LLVMGlobalUnnamedAddr`（与普通字面量同语义）+ 私有链接，内容地址用
     `LLVMConstGEP2` 直接构造常量 GEP，不引入 builder 指令。
   - 理由：只比 hash、名字或 C 串，hash 碰撞时会把不同内容别到同一地址，反射回读串错；
     initializer 完整相等是唯一与内容绑定的判据。
   - 边界：已证范围仅限本池命中路径；跨片去重是否完整、反射内容是否泄漏源码字面
     均未验证，不得声称完成；完整 IDE 重发布前不作为整产品验收依据。

63. **async throw 在 catch 前的 finally 清理下界必须取 landing pad 的词法深度**：
    - 动作：`emit_finallys_left_by_throw` 找到带 `catch_bb` 的目标 try 时，
      `target_fin` 必须取该 landing pad 的 `finally_depth`。即使目标 try 没有自己的
      finally（`fin_idx < 0`），也不能把下界归零；该深度只含目标 try 自己的
      finalizer，不含任何外层 finalizer。
    - 理由：匹配的 catch 必须先于外层 finally 处理异常。把无 finally 的 catch
      映射成下界 0 会提前展开外层 finally，令其消费原异常；内层已处理异常随后
      错误外逃，恢复时 pending pop 失去配对并下溢，最终触发容量守卫 abort。
      不要在 pending push/pop 加兜底掩盖这个上游清理边界错误。
    - 判定器：`async_finally_shared_paths` 必须覆盖异常穿越 finally、finally 内
      嵌套 try/catch 和跨 `await` 恢复，同时保留原异常与内层已处理异常的身份；
      联跑 `async_finally_paths`、`async_finally_shared_contexts`、
      `async_finally_pending_owners`、`catch_return_release` 及 focused selfhost diff。

64. **Task.Run/Spawn 的 async 实参是协程帧，不是 ARC 临时对象**：
    - 动作：`Task.Run`/`Task.Spawn` 走 async-call 分支时，`emit_detach_async_call`
      已把协程帧移交调度器/任务注册表（`Task.Spawn` 为自等待者，`Task.Run`
      keep-result 为注册表帧），之后不得再按 AST_CALL 的 owned-temp 规则
      `emit_release_owned_call_temp`。`Task<T>.Result` 则要标成 owned 值——
      `emit_task_member` 读走 RVAL 槽后把槽清零，所有权转移给消费方。
    - 理由：async 调用的发射值是帧指针。把它当 ARC 对象释放会在帧前缀
      （frame-16，即分配器 magic/class）上做引用计数减一，随后任务释放时
      `co_frame_recycle` magic 校验失败静默 abort；`Task.Result` 不标 owned
      则帧 RVAL 槽的 +1 永不释放，弱引用观察对象在 `result = null` 后仍存活。
      自举侧 TaskRun/Result 路径本就无 owned temp 释放并 AddTemp 转移，宿主
      修正后两版行为一致。
    - 判定器：`case_task_release_owner`（--check-leaks --arc-guard 下曾 rc=3
      静默 abort）、`case_task_run_async_bg`、`case_async_spawn_result_release`、
      `case_task_spawn_indirect`；运行时注册表单元
      `runtime_task_handle_registry`。

65. **泛型实例发现与规范化必须按接口实现关系匹配接口扩展方法接收者**：
    - 动作：`src/compiler/generics/generic_discover.c`（`disc_collect_call`）与
      `src/compiler/generics/generic_canon.c`（`canon_chk`）查找扩展方法返回类型
      以登记 `$__Canon` 泛型实例化时，不能只按 `p0->kind == rt->kind && p0->name == rt->name`
      精确比对接收者——当扩展方法接收者是 `TYPE_INTERFACE`（如 `this IDbExecutor db`）
      而调用点接收者是具体实现类 `TYPE_CLASS`（如 `SqliteConnection`）时，必须同步使用
      `zan_binder_class_implements_iface(rt->sym, p0->sym)`（与 `expr_overload.c` 的
      `ext_receiver_interface_match` 同口径）。
    - 理由：若 `conn.Delete<Entity>().WhereEq(...).ExecuteAffrows()` 链路中没有显式声明
      `DeleteQuery<Entity>` 局部类型，漏匹配会导致 `DeleteQuery<Entity>` 从未被登记到
      `$__Canon` 单态化表，发码退化为未特化的 `call void @abort(); unreachable` 桩，
      运行期静默 `RC=3` 崩溃。
    - 判定器：`tests/conformance/generic_ext_iface_return.zan`（具体类调接口泛型扩展
      链式返回且无显式局部声明）与 `case_arpg_save_repository`。

