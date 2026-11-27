# 代码规范——包/平台专属定式与语言事实逐坑

> SKILL.md「三、代码规范」只留通用规则主干；本文件收领域定式（Web/ORM/AI/
> 游戏/IDE/原生互操作）与逐包实机踩出的语言事实系列，每条含坑出处。

- **GUI 控件的样式解析与类型身份只有一条路**：
  ① **样式解析走基类入口**：控件解析自己的外观用 `Control.Box(app)` /
     `BoxState(app, state)`；借用别的类型的外观用
     `ResolveStyleAs(app, type, cls, state)`；**部件表面**（列表行、树行、
     滚动条轨道、弹层、分割线）用 `BareBox(app, type, state)` 取**裸基线**
     ——裸基线不带本控件的 Class、不带 id、不套行内覆盖：套上会串味
     （列表写成 `.primary` 就把每一行都染成主色）。控件实例方法里直调
     `Style.Of` / `Style.Full` 一律算违规（门禁：组件契约审计脚本的
     R4 规则）；静态 helper 没有 `this`，解析显式的（类型, 类）组合
     不算违规。
  ② **内联 style（`SetProp("style", ...)`）分两半**：视觉键（背景/颜色/
     圆角）在**解析时重放**进样式盒，控件上不留第二份副本——替换原文后
     旧值不留残影；布局键必须参与实际 Measure/Arrange，不能只改绘制外观。
     width/height/gap 的既有兼容投影按消费方维护；**padding 读取解析后的
     StyleBox 四边值，不转成显式 Pad()**，手工 Pad/Padding 保持显式优先级。
     原因：漏接布局曾使 `style="width:88px"` 无效；将 padding 简写首数
     写入 Pad 又会丢失四边和单位，并在替换样式后残留 padSet。回归同时检查
     实际槽位、替换样式和 DPI，不以样式字段正确代替布局正确。
  ③ **控件 kind 名单由编译器合成，不手写注册梯子**：编译图里继承 `Gui.Control`、
     public、非抽象、可无参构造的类按类名（= `Kind()` 默认值）进
     `__ZanControlCatalog`；别名/工厂/泛型闭合写 `[ControlKind(Name=, Factory=, Of=)]`
     （如 Radio 的 `RadioButton`、`ListView<string>`）。包清单 `catalog = "Ns ..."`
     声明的命名空间在程序按名构造且 `using` 了它时整体拉入。`ControlFactory.Register`
     只留给运行期动态组件。见 docs/GUI_CONTROL_CATALOG.md。
     新增或调整 kind 时验证自然构造、工厂、HTML、Html.Clone 及弱引用
     释放等价；复合控件通过 CloneContentTo 复制业务内容并重建生成部件。
     编译期 GenForm 限定名单已删除，不恢复重复清单。
- **Win32 资源所有权必须分开记账，程序默认图标取自身 exe**：
  ① **系统共享图标不能销毁**：`LoadIcon(0, IDI_APPLICATION)` 这类是系统资源，
     `DestroyIcon` 它等于销毁别人的东西；而 `LoadImage`（从 .ico 文件加载）、
     `ExtractIconEx` / `SHGetFileInfo`（取文件自带图标）得到的图标**由调用方
     负责 `DestroyIcon`**；
  ② 全局句柄（托盘图标、窗口图标）用"**谁创建谁销毁 + 显式 owned 标记**"
     记账，退出路径统一回收：凭感觉无条件销毁会误杀系统资源，漏销毁则每
     进程泄漏 GDI 对象；
  ③ **默认图标取程序自身的 exe**（`Environment.ProcessPath` + stdlib 的
     `FileInfoEx.Icon`），不要退回系统通用图标——zanc 生成的 exe 内嵌了
     默认图标资源，用户用 `--icon` 指定的图标也走同一条路，托盘/窗口因此
     显示的就是程序自己的图标；
  ④ 异步初始化的全局资源（如托盘图标在专用线程上装载）必须在**调用方
     返回前就绪**（在调用线程上装载），否则调用方读到的是半初始化状态；
  ⑤ 判定"图标是不是程序自己的"要**比像素**、不能比句柄：两次
     `ExtractIconEx` 拿到的是**两个不同的 HICON 对象**（同一张图），
     句柄不等不代表图标错；栅格化后逐字节比才是判据（`GetIconInfo` +
     `GetDIBits`，头要用 BITMAPV4 + BI_BITFIELDS 才拿得到 alpha 通道）。
- **Zan.Web (Zan.Mvc) 视图 CSP 纪律（`[security].csp` 开启后 `style-src 'self'` 拦内联）**：
  `style=""` 属性、`<style>` 块、`onclick=` 内联 handler、JS 里 innerHTML 拼进的
  style 属性全会被拦；CSSOM 赋值（`el.style.width=`）不受限。视图写法：th/td 列宽用
  `width="N"` 表现属性；组合样式进 admin.css「视图去内联工具类」或语义类；模板条件
  样式把 `{{#if}}` 搬进 class（`class="chatrow{{#if mine}} mine{{/if}}"`）；动态百分比
  宽高用 `data-w`/`data-h` 交 admin.js 统一 CSSOM 回填；页面脚本一律外链 `/static/js/`
  并 `defer`。（坑出处：(d) 框架+模板两次全量清扫，902+177 处内联样式；动态宽高
  与 tooltip 色点两种 CSSOM 形态实机验证。）
- **ORM 条件只有值自带原语，裸 SQL 片段是生成代码专属**。用户面条件一律
  `WhereEq/WhereGe/WhereLe/WhereIn/WhereLikeAny`（列名经实体元数据校验，
  值全部参数化绑定）与类型化 lambda（`ids.Contains(a.id)` 降级为参数化
  IN；空列表恒假，不会生成非法 `IN `）；列表筛选收集成 `List<OrmCond>`
  后 `WhereConds(conds)` 一口吞（多列 LIKE 用 `|` 分组 OR）。批量更新/
  删除走 `DbTable.Of(conn, "表").Query.WhereIn(...).ExecuteUpdateAsync(...)`
  （列名过 RequireIdent 白名单）。`W("片段")+InI(...)` 这类片段/参数分离
  写法已整体删除，值全部自带（值自带后错位不可能发生）；要给查询链接
  名字就用具名链：`SelectQuery<Post> sel = db.Select<Post>();`。
  坑出处：片段+参数靠位置配对，编译器查不出错位，历史上实烂三处
  （根治，值自带后错位不可能发生）。
- **Zan.Web 菜单是显式选择（GenRoute 约定），别靠描述"顺便"进侧栏**。进
  管理菜单只认显式标注：`[Custom(IsMenu = true)]`（类级或动作级，图标/权限
  同写在那一处）、`[Menu]`、`[AdminAction(IsMenu = true)]`；类级
  `[Description]` 只做文档与标题回退，**不会**让控制器进菜单。非根动作
  没有方法级显式标注一律不进——类级菜单配置只作用于根动作。方法层显式
  标注永远赢过类级默认（两层合并类先方法后）。坑出处：登录/健康检查这类
  有描述的公开端点曾被类描述隐式默认刷进侧栏，而 Monitor 非根动作上
  已标注的 `[Custom(IsMenu=true)]` 又被反向吞掉（GenRoute
  改约定后两端同时修复，web_menu_attrs 用例锁定）。
- **Zan 的父类构造默认不自动调用——基类字段初始化在子类实例上不生效**。
  子类要显式写 `: base(...)`；不写就等于没跑基类构造。症状很有迷惑性：
  "直接 new 基类一切正常，new 子类就 null reference"——因为前者跑了基类
  构造，后者没有。修法二选一：子类显式 `: base(...)`，或基类把那个对象
  改成**惰性获取**（首次用到时补建，基类访问器统一走它）。坑出处：把
  ChildWindow 的模型绑定逻辑抽成 `Gui.ModelBinder` 后，`binder = new
  ModelBinder()` 写在基类构造里，所有 ChildWindow 子类（设置窗、聊天窗、
  NavWindow）拿到的 binder 全是 null——`SetRoot`/`Wire`/`Handle` 一律
  null reference；改成基类惰性访问器后全绿。
- **把专用特性换成通用名值对时，构造默认值不会跟着走——默认值要迁到消费方**。
  `[Custom(...)]` 早期是带构造参数的专用特性（`icon = "mdi:antenna"` 这类默认），
  泛化成 `CustomAttribute(name, value)` 后按 key 存值，"某个 key 的默认值"就
  没有位置了：没写该 key 的调用方拿到空串。症状是"某几条记录少一个字段、
  显式写了的却正常"。修法：默认值落到**唯一消费方**（菜单图标 → `MenuBuilder`
  的 `defaultIcon` 兜底），别塞回特性。用 `git log -S '<默认值字面量>' -- <路径>`
  能定位它在哪次重构里丢的。坑出处：web_menu_attrs 期望 `mdi:antenna`，
  `5b2bf946`（纯文件规范化重构）把默认值吃掉后该用例一直红。
- **控件自带菜单要接宿主能力，走"宿主项 + 命令回吐"通道，别在宿主里另画一套菜单**。
  控件（如代码编辑器）自己画右键菜单时，宿主（IDE）级能力（格式化文档/重命名/
  LSP 代码动作）不能在宿主里再画一个菜单顶上去：宿主禁自绘，且两套菜单会同时
  开、互相打架。正确形状是——控件留一段**宿主段位**（如 `action >= 100`）供宿主
  注入菜单项，宿主项带**命令名**；控件点选后把命令名写进自己的命令槽，宿主每帧
  用 `TakeCommand()` 取走（取一次即清空）。控件不认识命令语义，宿主只落地命令，
  两边都不越界。判断口径：菜单归谁画，能力就以"项 + 命令"挂进谁的菜单。
  坑出处：给代码编辑区补右键菜单时，IDE 侧曾冒出"另画一套"的冲动，走控件既有
  `ctxCmd`/`TakeCommand()` 通道后一次做对。
- **门面/物理双层对象：物理实例绝不能回填管理器回引用，配置在发放时下沉拷贝**。
  连接池、事务包装这类"门面 + 物理"结构里，门面判定字段（如
  `client.pool != null`）往往就是命令路由开关——给池建的物理连接回绑
  `pool`，它自己的第一条命令（连验活 PING 都算）就会被门面语义重新路由进
  `pool.Acquire`，递归开连直接挂死，且没有任何编译期/诊断提示。正确做法：
  池级钩子（序列化器、事件、超时）在开连时**拷贝**到物理对象上，池与物理
  对象之间保持单向引用。边界：发放即快照，之后改池级钩子只影响新开对象，
  需在注释里写明。坑出处（Redis 池级序列化钩子实测）：回绑写法
  表现为"第一条命令挂死、OpenOne 被递归进入两次"，分阶段打印（enter/
  connected/wired/ping ok）一击定位——async 挂死先怀疑命令被二次路由，
  别急着怀疑编译器。
- **语言事实三则（CRUD 声明化实机踩出）**：① `protected`
  是**仅子类可见**——同包同命名空间的协作类也访问不到（`Crud` 访问
  `AdminController` 的 `Can/Note/Saved` 直接编译错）。包内协作面要么
  放开为默认公开（如 Note/Saved 这类通用应答/审计），要么像
  `Screen(canUpdate, canDelete)` 那样由子类调用方取好掩码再当参数传；
  ② 关键字不能当标识符——`where`（lambda 语法）与 `set`/`get`（属性
  访问器语法）都是保留字：`where` 做变量名报 `unexpected token` 一串，
  `DbValues set` 参数把整段方法解析炸成上百条级联错（各改名实修）；
  另 `StringBuilder.Append` 返回 void，`sb.Append(x).ToString` 链式
  取值编译不过，必须拆两条语句；
  ③ async 方法同一表达式里 await 前不得再求值带副作用的调用
  （`this.Ctx.path` 要先赋局部变量），编译期强制；
  ④ **不支持嵌套类**：类里面不能再声明类。实体类型一个文件一个类
  （同包约定：`Game.Kit.BlitSourceEntry`、`Game.Data.TableColumn` 都是
  独立文件）。想在同一处看全就放同一命名空间的不同文件，跨文件引用。
- **语言事实第四则（全站假死排查踩出）**：`virtual` 在派生类
  里是**新开槽位**（隐藏基类实现），不是覆盖——挂接框架钩子必须写
  `override`。AppController 曾把 `OnBeforeAsync/OnAfterAsync` 声明成
  `virtual`：路由蹦床经 `Controller` 静态类型的槽位派发，新槽位永远
  打不中，跑的是基类空实现——请求租约借了不还，池按需扩张到上限后
  Acquire 全部挂满超时，全站 503/无响应（客户端只见偶发 000）；而
  `base.` 这类静态引用仍能打中新槽位，半通半不通最迷惑。排查定式：
  池 acq/rel 计数失配 + 钩子首行日志打不出来 = 槽位没接上。
  起 zanc 对该形状直接告警（`--deny-warnings` 升级为编译
  失败），diag_virtual_hides_inherited_virtual 用例锁定，合法重载与
  真 override 不误报。
- **语言事实第五则（XML DOM 包首日踩出）**：① 包命名空间按
  using 拉取：`packages/*` 里的类型必须先 `using System.Xml;`（对应的
  包命名空间）才入编，漏 using 直接报 `undefined type 'XmlNode'` 且不
  指向缺 using——新包首次使用先补 using 再排查别的（与 stdlib 按需拉入
  同一门槛，包从 packages/ 发现根扫描）；② 可空返回值不得链式解引用：
  checker 强制（`'RootElement' can return null; accessing ... faults at
  runtime`），既有代码惯例是存局部变量判空，无 `?.` 使用先例、别臆测其
  形态；深层取值逐层存局部判空，同根多条断言用 `if (x != null) {}` 包住
  整段，测试探针尤其常用。
- **语言事实第六则（Zan.Toml 包首日踩出）**：① `JsonValue.At(i)`
  是**数组专属**——对象上调 `At(i)` 直取数组底层的 `items[i]`，对象该列表
  恒空，无边界检查直接段崩（编译期查不出）；对象遍历配对用
  `KeyAt(i)`/`ValAt(i)`，`Count` 两种形态通用；② 无序格式的往返相等
  **必须按键集合深度比，不能比 ToJson 原文**——写出器把行内对象键合法地
  挪进 `[表头]` 区块，同层键序变化是语义不变的（TOML 表无序），拿 JSON
  文本判等会误报失败：递归 `Eqv(a,b)`（对象逐 KeyAt 查 Has+ValAt 递归、
  数组按序递归、标量按类型比）才是正确闸门。
- **语言事实第七则（数值类型与 IEEE-754 位重解释现状——已淘汰早期手写绕行）**：
  ① 数值类型体系已完整支持 `sbyte`/`byte`/`short`/`ushort`/`int`/`uint`/`long`/`ulong`/`float`/`double`/`decimal`/`nint`，不再需要因缺 `float` 或有符号小整型做手写绕行；
  ② IEEE-754 位型互转直接调用标准库内建重解释原语 `NativeMemory.AsF32(int)`、`NativeMemory.AsF64(long)`、`NativeMemory.AsI32(float)`、`NativeMemory.AsI64(double)`（单条硬件 bitcast，零分支/零精度损失；注意 `AsF32`/`AsF64` 参数分别为 `int`/`long`），严禁再手写逐位乘 2 / 归一化循环拼 IEEE-754；需要硬件位计数/前导尾随零计数时直接用 `System.Numerics.BitOperations`（`PopCount`/`LeadingZeroCount`/`TrailingZeroCount`，参数为有符号 `int`/`long`）；
  ③ 判 NaN 仍用 `x != x`，判 ±∞ 用 `x > 1e308` / `x < -1e308`（或结合 `AsI64` 检查指数全 1 位型）。
- **标准库与基础包治理定式（热路径、并发队列与外部脚本转义）**：
  ① **异步等待队列用 `Queue<AsyncGate>`，禁 `List<AsyncGate>.RemoveAt(0)`**：数据库连接、网络流、TLS/WS/MQTT/Modbus/SSH 等 FIFO 等待者与缓冲淘汰队列一律使用 `System.Collections.Generic.Queue<T>`（注意计数方法为 `.Count()`），消灭 $O(N)$ 首元素搬移；
  ② **集合判重用 `HashSet<T>`，禁 `Dictionary<T, bool>`**：单次 `seen.Add(key)` 返回 bool 同时完成查重与插入，省掉双倍哈希与布尔值槽位；字典查找优先 `TryGetValue(k, out v)`，遍历值集合用 `.Values` 避免 `Keys` + 二次索引；若 `TryGetValue` 位于 `&&` 短路右侧，须先给 `out` 局部变量赋初值（如 `string s = null;`）以通过编译器确定赋值检查（E2098）；
  ③ **LRU 与日志环形队列杜绝 $O(N)$ 搬移**：LRU 缓存用 `LinkedList<T>` 新增的 $O(1)$ 节点 API（`AddLastNode`/`RemoveNode`/`MoveToLast`）或侵入式双向链表（`Sweep()` 遍历活跃链而非全槽位数组）；定长历史日志（如 `MemoryLogSink`）用定长数组环形缓冲替代 `List.RemoveAt(0)`；原生 `Mutex` 持有者须实现 `IDisposable` 并以 `try/finally` 保证 `Mutex.Unlock` 不泄漏；
  ④ **外部边界转义禁止不完整 `.Replace()`**：拼接 JS 字符串（如 `FirefoxAgent`）、AppleScript（`Window.Posix`/`TrayIcon`）或 JSON（`FingerprintProfile`/`JsonRpcBuilder`）必须走完整转义器（覆盖 `\`、`"`、控制字符 `< 0x20`、`\r`/`\n`/`\t` 及 JS 的 U+2028/U+2029），严禁 `Replace("'", "\'")` 这类自欺欺人的空操作。
- **语言事实第八则（Zan.Yaml 首日踩出）**：① 同类里
  **static 方法调用实例方法不报检查器错**，直到 LLVM verification 才炸
  （`Operand is null`，指向被调函数的 this 实参）——报错不提示
  "static 调实例"，纯帮助类小函数一开始就 `static` 化（`JsonValue`
  伴随 helper 如 `HexDigit`）；② 字符串转义集合有限：`\b`/`\f` 是
  编译错（invalid escape sequence），要控制字符用
  `Encoding.Utf8FromCodePoint(8)`（退格）/`(12)`（换页）落；③
  `while (true)` **合法**（stdlib TaskJoin/ByteBuffer/JsonTape 都在
  用）——别凭"语言不支持"把循环改写成标志位绕路（此前会话曾这样
  误绕，证伪撤案）；④ 可空返回链式取成员（`Get.Get`）检查器
  强制"先落地局部 + 判空"，测试代码给一个永非 null 的哨兵 helper
  （缺失返 `NewNull`）一次消音全部链式断言。
- **语言事实第九则（深审修复批连踩两次）**：转出型 `byte[]` 的
  `Length` 含尾随 NUL 槽——`ByteBuffer.ToBytes`、hex/字符串转字节这类 API 产出的
  数组比"逻辑数据"多 1 个收尾 0 字节（既有契约，bytebuffer_bounds 金样钉死）。
  拿 `.Length` 当数据长度喂底层（实例：RSA hex 密钥按字节数传参）会多传一个 0
  而被拒或错算；真实长度用写入/转换时记下的 n 传递，不反查数组。
- **语言事实第十则（异步调度与高并发进程/同步原语重构踩出）**：
  ① `Task.Run(Action/delegate)` 下发给 Worker Pool 时必须生成独立堆帧
  （TaskActionFrame）并绑定 `zan_co_ready`，不能在当前调用线程同步 invoke；
  ② 跨线程运行的委托闭包记录必须通过 `emit_closure_retain/release` 保活，
  完成后写入 DONE 原子状态并唤醒 awaiter，不能直接返回 0；
  ③ 进程启动严禁直接调用系统的 `popen/system` 拼接裸命令，必须提供直接调用
  `CreateProcessW` / `execve` 并进行参数转义的 argv[] 机制，以杜绝命令注入；
  ④ 同步原语 Mutex 应优先使用用户态轻量化 `SRWLOCK` / `futex`，避免进入内核态
  创建具名或匿名内核互斥体造成的调度与争用开销；
  ⑤ 异步解耦必须彻底独立于 UI/渲染帧：UI 线程专职负责事件分发与绘制，后台任务与 I/O
  完成后通过 `App.Post` + 线程级 `Wake` 唤醒 UI 消息泵排空回调，严禁后台计算依赖渲染循环推进；
  跨线程/协程结果传递与协作取消统一使用 `TaskCompletionSource<T>` 与 `CancellationToken` 原语。
- **语言事实第十一则（Coreforge 存档迁移连踩两次）**：
  `JsonValue` 对象上 `Put(key, value)` 对**已存在的键是追加重复键**（序列化
  产出两份同名键，读档端 `Get` 只见其一、`Count` 虚高），`Set(key, value)`
  才是**原位替换**——改写从旧档剥出来的字段（如给 buildings 列表元素剥
  旧字段、裁 researched 长度后写回）必须用 `Set`；数组元素没有原位写，要
  重建数组再 `Set` 整个键。踩坑实锤：探针证明 `Put` 后对象含两个同名
  `"list"` 键（Count 4→Set 后 3），存档迁移测试两次因 `Put` 写回被
  Require 拒收。

- **文本格式包的 BOM 防线（Yaml/Toml/Xml 跨平台审计踩出）**：
  `File.ReadAllText` 已在文件层剥 UTF-8 BOM，但 `Parse(src)` 收网络报文/
  内嵌资源时没有这层兜底——Windows 工具产出的文本常带 EF BB BF，不剥
  首键/首标签带隐形字节或报"expected <"。任何文本格式解析器的 Parse
  入口自己剥一次 BOM（三字节判定 + Substring(3)），conformance 加
  BOM+CRLF 向量锁行为；换行侧在切行层吃掉 CRLF/孤立 CR，写出恒 `\n`。
- **标记类消毒器：实体要解、协议要清、输出要转义**（坑出处：HTML 消毒器
  落地）：三处漏一处就能被绕过——
  ① **属性值里的实体必须解码**：只解文本不解属性，`href="javascript&colon;…"`
  会被当成"没有协议名的相对地址"放过去，浏览器却是当 `javascript:` 执行的；
  ② **判协议前按浏览器的规则清 URL**：去掉制表/换行（`java&#9;script:` 就是
  这么绕过去的）、去掉首尾空白与控制字符，再看协议名；
  ③ **输出端转义才是最后一道闸**：文本转义 `& < >`，属性值转义 `& < > "`；
  标签白名单之外，默认"连内容一起丢"（script/style/iframe 的代码留在正文
  里没意义），而不是只丢标签。
- **stdlib 的"静默空操作"比崩溃更难查**：删错了列表（对象的值列表 vs 数组的
  元素列表）时，调用方看不到任何报错——元素还在，一切"看起来正常"。
  同族的坑是**偏移量写死**：扫描从 `from` 开始，起点却写成常量 1，
  from>0 时读到错位的数字（`{"x":-7}` 被读成 0、`[1,-2]` 被读成 -1）。
  为什么：这两个坑都不抛异常，只会悄悄给出错的数，等到上层用错数据出
  问题时，早就查不到源头了。**做法**：凡是"按下标改容器"与"从 from 扫到
  to"的代码，逐行核对用的是不是同一份数据；测试里既要断言结果，也要
  断言"没动的地方真没动"。
- **写进文档的语法要逐条对账实现，边界条件单独测**：JSONPath 的"逗号并接"写
  进了类注释与 README，实现却漏了，是 conformance 第一次跑就暴露的；同一次
  里 JSON Pointer 的切分循环写成 `while (i <= n)`，探针直接挂死才暴露。
  **做法**：每条语法配一条用例（含"该不匹配时就是不匹配"的负例）；切分/扫描
  类代码按"空输入 / 只有一个元素 / 最后一个元素 / 越界"四种形态各写一次；
  循环条件里的 `<=` 与 `<` 要对着"最后一个元素的下标"手算一遍；探针要带
  超时跑，挂住就是死循环而不是"慢"。
- **声明驱动的 CRUD 定式（ListPage/Crud 落地）**：一屏一个
  `ScreenDef` 静态声明（列 Col/Tag/Flag、行内操作 Ops、筛选、工具栏、
  `Table/EditFields` 写白名单），渲染 `Screen` 整段产出 screenHtml
  （视图只剩壳），通用写动作走 `Crud.Field/Batch/Delete`（白名单+
  Gen.Safe 双保险+`?` 占位符，行级授权由调用方预检 allowed 集合，
  越权与不存在同答 404），`Crud.Conf` 同一声明投影 adminUI conf 契约。
  新屏别再手写表格 HTML 与格式化帮静态方法；业务语义（会话失效、
  关联清理）留在屏内动作，通用动作只对声明的主表负责。
- **堆外密码学实现的三坑（stdlib Pbkdf2 首日踩出）**：① 内层消息
  缓冲必须按 `max(saltLen+4, 32)` 分配——PBKDF2 的 U 链从第二轮起
  消息是 32 字节摘要，按"盐+块序号"尺寸分配会在盐短、c≥2 时
  span 越界；② 每轮算完 U 必须把 U 回写外层缓冲的消息位，只留下
  内层摘要 ih 会让下一轮错拿 ih（链静默变错）；③ 最小对拍集必须含
  c=1 与 c≥2、短盐与 >64B 长键、dkLen≤32 与 >32 分块——c=1 与
  单块向量对上述两类 bug 全都测不出来（RFC 7914 形状
  向量 + Hmac.Compute 对拍双保险锁进 tests/conformance/pbkdf2）。
- **动态加载系统 C API 时逐参数对照目标 SDK 头文件**：函数指针不得凭
  印象省略“编码类型”等中间参数；Crypt32 的 `CertAddEncodedCertificateToStore`
  曾少声明一参，编译和链接都通过，却在首次加入证书时进入系统 DLL 后
  崩溃。对负面策略做多次调用并在崩溃时取原生回溯；Windows SDK 结构
  成员还受头文件宏与版本控制，限时字段不可用时用缓存-only 策略失败
  关闭并明确互通限制，不能改成无界网络查询。
- **异常面 catch 裁决 taxonomy（全量裁决沉淀）**：Zan 代码里合法
  的 catch 只有五类，写时注释必须能对号入座——① 传播/包裹/重抛（边界
  换异常类型，或清理后原样重抛）；② 合法错误值契约（探针读可选资源，
  缺席即常态返回默认值；TryX/bool/null/Failure 契约，doc 写明"失败返回
  什么"）；③ 进程/后台循环边界记日志继续（请求→500、后台循环不因一轮
  失败而死）；④ don't-mask 清理（回滚/释放自身失败不得覆盖原始异常，
  空 catch 是常态但注释写明不变式）；⑤ 设计内护栏（每帧 guard、日志
  终端汇"无处可报"）。唯一禁令：**无注释裸吞**。why：审计曾把"61 处
  catch 仅 6 处重抛"当软着陆证据，全量逐处读上下文后 122 处真实站点
  全部合法——grep 口径不是吞点口径，探针/契约/边界天然占多数；裁决时
  还要把"静默丢根因"（catch 转 null/默认值且不留任何诊断，如池借出
  null 无日志）与真吞点区分开单独挂账。

- **取消 IO 的顺序是置位、唤醒、确认结束、关闭**：取消通知可能早于 reactor
  注册，须在取消状态下重复唤醒，保留描述符到 await 完成；停用并排空迟到
  回调后才允许 close/reuse。显式 stopped 状态禁止 Accept 自动重开。
  为什么：空闲 accept 停止竞态会挂住关闭流程，迟到通知会误伤复用句柄；
  POSIX 监听 socket 的 shutdown 不等于取消，仍分配的 fd 上 fatal errno
  不能按 IsOpen 重试，errno 应在 syscall 后立即保存到局部。Windows
  取消/复用及真实多进程关闭已验证；POSIX 此次仅静态复核，不冒称实测。
- **注销后台回调不等于生命周期排空**：进程退出前 await `Timer.StopAsync`，
  等已选回调和最后一个共享泵结束；同步 `Stop` 仍供回调自停。临时批次
  放同步 helper，返回时释放，不跨后台 Sleep；完成标志须在这些引用释放后发布。
  为什么：Start/Stop 最小探针五次泄漏临时列表，后台泵还撞上全局析构；
  排空修正后五次及周期/自停/重启回归均无泄漏。取消回调只捕获独立状态，
  不捕获持有该 Timer 的 owner，避免 owner→timer→callback→owner 环。
- **树形结构不要存父指针——ARC 收不掉引用环**：命令树 / AST / 配置树 / DOM 这类
  "父持有子、子又指回父"的结构，在 ARC 下成环，引用计数永远降不到 0，
  进程退出时被泄漏检查报成"仍可达"（症状是 `memory leak detected: N
  object(s) still reachable`，点名 `List<...>` 之类容器）。
  **做法**：要"从根到自己"的路径就**存路径字符串**（挂到父上时算好），
  或存下标/id；**要真回父节点就用 `weak` 字段**（`public weak Node parent;`，
  父被回收后读到 null，语义正好）。**"存祖先链"不是办法**——链里装的是
  父节点的强引用，父→子(children) + 子→链→父 照样成环。
  **验收**：这类用例要跑带 `--check-leaks` 的变体（普通跑是绿的，泄漏只
  在变体里看得见）。
  坑出处：CLI 命令树最初让 `CliCommand` 存 `parent` 指针，命令树整体
  成环 → conformance 报 22 个对象仍可达；改成存 `path` 字符串后归零。
  同一个坑在 HTML DOM 上又踩了一次：祖先链 `List<父节点>` 是强引用，
  98 个节点连带 497 个对象（每个节点两份 chain 列表）在退出时报泄漏，
  换成 `weak` 父指针后归零。

- **原生模块拆分先写所有权与内容版本契约**：借用像素须注明有效期、
  行 stride 的单位和 alpha 格式；接收方要长期持有就用自己的分配器 COPY，
  不跨独立 CRT 释放。GPU 缓存按内容 serial 更新，不按指针/句柄判断；
  临时 surface view 不缓存。坑出处：sprite 同 key 重烘焙保留句柄、瞬时
  view 复用同一地址，只有新 serial 或强制重传才能画出新内容；回归要
  同时覆盖 padded stride、驱逐重载和稳定句柄换像素。
- **IDE 运行与发布编译参数解耦（调试挂满诊断，发布绝不挂隔离区）**：
  IDE 本地调试/F5 运行统一走 `-g --arc-guard --check-leaks --strict-runtime`，
  把野指针/悬垂指针和生命周期错乱当场转成受控断言 / `0xDEAD0000DEAD0000` 崩溃，
  避免内存破坏（0xC0000005）串到远端分配器深处才爆；但部署/发布产物
  （`--publish` 或 `-g --no-arc-guard --no-check-leaks`）严禁启用 `--arc-guard`
  与 `--check-leaks`，因为死块隔离区只吞不吐会导致长期运行的服务或桌面端 RSS
  无界膨胀；工程级 `strictMemory` 开关仅作用于本地非发布运行，绝不穿透覆盖发布配置。

	- **AI 辅助工具链与包生态韧性（永远暴露、自动回退、缺包自愈）**：
	  ① MCP 工具注册必须去条件化：核心工具（`zan_api_search`, `zan_lookup_symbol`,
	  `zan_list_members`, `zan_example`）严禁因为项目目录缺少 `.zanmap` 离线文件
	  就拒绝注册向 AI 隐藏，必须始终暴露，调用时自动向 `knowledge/` 或 SDK
	  源码兜底；② 面向终端发布必须包含 `packages-catalog.json` 与 `pkg_*` 工具族
	  （`pkg_list_installed`, `pkg_search`, `pkg_install`），遇到 `namespace not found`
	  时由 AI 反向映射并自动安装，而非盲猜重写；③ 编译器内建 `zanc --api <symbol>`
	  命令行直达查询，确保即使无 MCP 环境，AI 也能通过单行命令拿到真实类型与方法签名。
	  坑出处：历史版本因未建 `.zanmap` 导致 MCP 工具静默全部隐藏，AI 退化为瞎猜接口；
	  且未装扩展包时 AI 无法感知或安装已有官方包，反复重造已有基础设施。
- **原生栅格化与 Blit 裁剪必须做 64 位整数防溢出**：
  在计算目标裁剪右界和下界时，`dx + dw` 和 `dy + dh` 若直接用 32 位 `int` 相加，
  当坐标处于极端区间（如 `INT_MAX` 附近的平移或越界拖拽）会发生 signed integer
  overflow（UBSan 报警），导致边界反转或崩溃；计算外包围盒右下坐标必须显式强转为
  `(long long)dx + (long long)dw` 再与 clip 区间取最小值。坑出处：`cpu_blit_img`
  在大坐标渲染时触碰 UBSan 符号溢出断言。
- **Windows Clang 下开启 ASan 与崩溃捕获冲突规避**：
  在 Windows x64 下用 Clang 开启 AddressSanitizer 时，若涉及 `__builtin_setjmp`，
  须带 `-mllvm -asan-realign-stack=16`，防止 ASan 32 字节栈重对齐与 Win64 SEH unwind
  展开要求（16 字节对齐）冲突导致 `offset is not a multiple of 16`；同时运行时异常捕获器
  （VEH）在检测到 `__SANITIZE_ADDRESS__` 或 `__has_feature(address_sanitizer)` 时必须跳过安装，
  避免进程在 CRT 析构（`__asan_unregister_globals`）退出时被 VEH 二次拦截并在未初始化的
  stdio 上 `fopen` 发生二次 access violation。
- **AI 管线三件套的定式（`System.Ai`，坑出处：`packages/Zan.Ai` 落地）**：
  ① **失败用返回值不用异常**：网络失败是常态（超时、断网、5xx），
  `Complete` 返回 `null` + `client.lastError` 给出可读原因，比抛异常顺手——
  调用方不必到处包 try/catch，也不会漏掉一个异常分支。
  ② **嵌入响应按 `index` 归位，不靠数组顺序**：协议里有 `index` 字段，靠
  顺序是典型的错位 bug 来源（某个网关重排了数组，向量就全对不上号，而且
  **不报错**——只是检索结果莫名其妙）。维度不一致直接报错，**不截断和稀泥**
  （截断会让相似度算错且错得隐蔽）。
  ③ **token 估算必须单一口径**：切块用的 `TokenCost` 与客户端用的
  `EstimateTokens` 是同一把尺子。两处口径不一致会让"切出来说每块 100
  token，一算却是 25"——这类 bug 不报错，只是账单与限流全错。
  ④ **POST 补全默认不重试**（`maxAttempts=1`）：补全没有幂等键，重试可能
  重复计费。5xx 与网络错误才重试（指数退避，上限 2 秒）；4xx 不重试。
  ⑤ **不内置模型价格**：价格会变，内置就是内置一份会过期的谎言。单价表
  由调用方传；估算是估算，要标出来。
  ⑥ **混合检索用 RRF 而不是分数加权**：向量的距离（1 - cos_sim，0~2）与
  BM25 的分数（无上界）**不在同一量纲**，加权求和必须先归一化，而归一化
  方式（min-max？z-score？按查询内分布？）每种都有坑且对查询敏感。RRF 只看
  **排名**不看分数（`score(d) = Σ 1/(k + rank_i(d))`，k=60 取自原论文），
  对量纲完全免疫。两路原始排名与分数都保留在命中实体上，要自己加权时可用。
  ⑦ **依赖用子命名空间而不是包根**：`System.Ai` 依赖
  `System.Data.ZanDb`（HNSW 索引，17 个编译单元）与 `System.Text`
  （BM25，37 个）——**不拖 ORM 与 sqlite 驱动**（那些在 `System.Data` 根与
  `System.Data.Orm` 下）。实测编译单元数是判断依赖代价的硬指标。
  ⑧ **`double` → `float` 的转换代价要实测再写文档**：向量索引用 `float[]`
  （单精度，因为索引常驻内存、量大），embedding 服务返回 `double`。实测
  分量相对误差约 1e-8、余弦偏差约 1e-9，对检索排序没有可观察影响——但要
  与服务端原始向量逐位一致时**不能经过这层**。
  ⑨ **动手前先查"平台是不是已经有了"**：ZanDb **原生**就有向量索引
  （`EnsureVectorIndex` HNSW）、混合检索（`SearchHybrid`，用的就是同一个
  RRF 公式 `1/(61+i)`）、检查点与世代机制、`RebuildSearchIndexes`。自己
  在应用层拼一套 HNSW+BM25 是**重复造轮子**，而且丢掉了事务一致性。
  坑出处：`Zan.Ai` 先做了内存版 `VectorStore`/`Retriever`，写持久化版时才发现
  `ZanDb` 早已把整条路铺好了。
  ⑩ **自己拼的索引没有锁 = 不支持并发写**：`VectorIndex.Add` 改
  `chunks`/`nodes`/`idSlots` 三个共享结构且**无锁**，并发写会撕裂图结构
  （不是"结果不准"，是内存结构损坏）。`ZanDb` 那侧靠 `AsyncRwLock`
  （`InsertAsync`/`UpdateAsync`/`UpdateAtomic`）串行化写，索引变更走
  journal + 检查点，与文档写入同事务。**应用层拼的索引要么自己加锁，
  要么明说"单写者"**——别让调用方以为它能并发用。
  ⑪ **两个后端做同一件事，语义必须对齐并断言**：内存版与持久化版都用
  RRF（k=60）、都返回同一个命中实体，但**光"看起来一样"不够**——conformance
  要有一条断言比两边的融合分数**逐位相等**（实测 0.032787）。否则从内存切
  到持久化，检索结果会悄悄变样，而没人会发现。
  ⑫ **远端写的 request id：一个 id 只能配一个请求，重试才复用它**：
  服务端按 id **绑定请求指纹**——同一个 id 配不同的请求体会被判
  `id_conflict`（不是静默覆盖，也不是当成两次独立写入）。所以：
  ① 一次操作里有多个不同请求时**各自派生 id**（实测：建文本索引与向量
  索引共用一个 id，第二次被拒 `id_conflict`，连带向量索引没建成、检索
  报"search index does not exist"——**错误信息指向症状而不是根因**，
  排查时先看第一个失败的请求）；② 同一个请求**重试时用同一个 id**，
  这才是幂等（服务端按 id 去重，网络超时后重试不会写重）——所以 id 要
  在重试循环**外面**取一次。

- **远端协议封装：解析响应要看信封结构，不看结果字段**：远端响应是
  `{v, id, ok, revision, result|error}` 的信封，`result` 才是载荷，
  `error` 是 `{code, message}`。判成功要读 `ok` 而不是看 `result` 存不
  在——**失败时 `result` 也可能有值**（比如空数组），只看 `result != null`
  会把失败当成功。
- **起服务端的用例必须 await 停止方法，否则 leakcheck 报泄漏**：
  `RequestStop()` 只是**请求**停止（置标志）；`await StopAsync()` 才会
  停监听、排空在途连接、关掉数据库。少 await 一步，退出时一堆对象仍
  可达 → leakcheck 报几百个对象泄漏，而**直接跑程序是过的**（只有
  leakcheck 变体带 `--check-leaks` 才看得见）。坑出处：远端检索器
  conformance 用 `RequestStop()` 收尾，leakcheck 变体报"515 object(s)
  still reachable"，换成 `await StopAsync()` 即净。
- **leakcheck 变体的失败要重跑一次再下结论**：并行跑时可能撞端口或
  撞 Windows 的 AV 短暂占文件（`STATUS_SHARING_VIOLATION`），表现是
  "单跑绿、并行红"。但**别用它当借口**——先看泄漏报告里点名的分配点
  （`file.zan:LINE`），那才是根因。

- **编排层要抽象存储后端，就抽接口而不是加参数**：编排层（`Rag`）原来
  只认内存档的检索器，接本地/远端档就得绕开编排层自己拼提示词——那
  等于没有编排层。抽两个契约：
  ① **读侧接口要同时有同步与协程两版**：本地档（文件 + 内存索引）没有
  IO 等待，同步更直白；远端档**必须**协程（阻塞会停掉 worker 的事件
  循环，把并发限制在 worker 数量上）。**异步专属的后端，同步方法要
  明确报错**（抛异常），不能悄悄阻塞——后者伪装成"能用"，实际把并发
  能力废掉了。仓库先例：`MySqlConnection` 的同步接口直接抛。
  ② **写侧接口的 id 由后端分配**：数据库的 id 是服务端在事务内发的；
  让调用方给 id 就得"先查最大 id 再 +1"——那是竞态（两个进程同时写
  会撞 id）。接口返回**后端分配的 id 列表**。
  ③ **检索器与写入器必须来自同一个后端**——写进 A 库、从 B 库读是配
  错了。接口能同时声明同步与协程两套方法（委托做不到：一个委托只有
  一个签名），所以这里用接口而不是传委托。
  坑出处：`Zan.Ai` 的 `Rag` 绑死内存档 → 抽 `IRetriever` / `IIndexWriter`。

- **每次请求新建 `HttpClient` = 每个请求泄漏一条 keep-alive 连接**：
  `HttpClient` 内部有连接池，每次新建等于每次丢掉池子里的连接——连接
  被服务端留着，客户端这边已经把它丢了。leakcheck 的症状很典型：
  "N 个 TcpClient / HttpRequest / ByteBuffer 泄漏"（N = 请求数）。
  正确做法：**按端点复用**（连接池是按 host/port 建的，换端点就得换客
  户端，所以按端点规范化串做键缓存），并给客户端加 `Close()` 关掉
  全部。坑出处：`Zan.Ai` 的 `AiClient` 每次 `PostJson` 新建 `HttpClient`
  且从不关 → leakcheck 报"每个请求一个 TcpClient 泄漏"。**同款代码在
  `Zan.Mvc` 的 AI 模块里也有**（同一个坑，两处）。

- **增强层失败要降级，不能拖垮主流程**：像重排这种"锦上添花"的层，
  失败时**降级继续**（用未增强的结果），而不是让整个请求失败。降级的
  失败原因要**单独记**（`rerankError`），与主流程的 `lastError` 分开
  ——两者语义不同："主流程失败了" vs "主流程成功了但没增强"。降级时
  还要**把结果裁回主流程期望的规模**（宽召回 50 条 → 降级后取 topK），
  否则降级路径会把宽候选集直接塞进提示词。
  坑出处：重排降级后忘了裁剪，20 条候选全进了提示词。

- **不同来源的分数不能混**：重排服务的分数（0~1 或无上界 logit，各家
  不同）与 RRF 分数**不同量纲**。要融合就用 **RRF**（只看排名不看分
  数，量纲免疫）；要用原始分数就**各自存各自的字段**，别塞进同一个
  字段——混在一起下游没法比，而且"看起来能用"。
  重排改变了顺序，就让**顺序**表达重排的结论。

- **重排后文本必须跟着命中一起重排**：顺序变了之后**不能按位置取文本**
  ——位置变了，文本和命中就对不上，而且**不报错**，只是答案里混进了
  别的块（错位 bug 最常见也最难发现的形态）。要按 **id** 找回对应文本。

- **让模型打分用行号协议，不用 JSON**：模型吐 JSON 的失败率不低（多一
  个引号就废一次调用）。让模型回"编号 空格 分数"再解析，容忍格式噪
  音。解析要**容错**：只认能解析出的行，认不出的**跳过**（不猜、不硬
  套）——少几条参与打分比整批报废强。
  坑出处：`LlmReranker` 用行号协议，conformance 专门验"部分行解析不
  出来时其余仍可用"。

- **会话历史要按"轮"裁，不按"条"裁**：一轮 = 一条用户消息 + 一条助手
  消息（成对）。裁到一半（一半提问没回答）上下文就是断的——模型看到
  "我上一轮问了什么"却没有"我上一轮得到什么"，容易重复提问、答非所问。
  超预算整轮丢，从最老的开始。
  配套两条边界：**当前轮不裁**（最后一条还没助手回答的用户消息正在进
  行中，裁了就丢了当前提问）；**至少留最新一轮**（预算小到装不下一轮
  时宁可超预算也不清空历史——有历史总比没历史好）。
  坑出处：`Conversation.Trim` 第一版把"进行中的半轮"和"完整轮"混为
  一谈，裁出来的历史断成两截（conformance 抓到）。

- **多轮上下文里，历史不进检索只进提示词**：检索只用**当前问题**——历
  史里的旧问题对当前问题没有帮助，混进去会拉偏召回。失败时**不写历
  史**——写进去会让下一轮带着一条失败的问题，模型以为自己问过而没得到
  答案。

- **起 HTTP 服务的用例：`Stop()` 之后要等在途连接收尾**：
  `server.Stop()` 只停监听，**已在服务中的连接还在跑**——不等就退出，
  leakcheck 会报连接、请求、缓冲区一串泄漏（`Socket`/`HttpRequest`/
  `ByteBuffer`/`TcpListener`/`CancellationToken` …）。标准收尾：
  `Stop()` 之后 `for (int w = 0; w < 20; w = w + 1) { await Task.Delay(10); }`。
  另一个要点：起服务用 `Task.Spawn(server.Start())`（`Start()` 是
  `async void`，直接调会立刻返回，端口还没监听起来）。
  坑出处：重排 conformance 起初 `ai.Start()` 后直接跑、`Stop()` 后直接
  退，leakcheck 报 13 个对象泄漏、arcguard 报 `null reference`。

- **列存的缺失值用掩码位表达，不用哨兵值**（坑出处：`Zan.DataFrame` 补
  缺失值处理）：列存是强类型的 `List<long>`/`List<double>`/`List<string>`，
  格子里**总有值**，所以"这行没填"与"这行填了 0"**分不开**。
  ① **不用 `0` 当哨兵**：`0` 与 `""` 都是合法值，混为一谈后两种情况的
  处理方式常常相反（前者该 drop，后者该保留）。pandas 的 `NaN` 好使是因为
  double 有专门的 NaN 位；Zan 列存是强类型的、没有 NaN 语义，所以要用
  **独立的布尔掩码**（与值列表等长，空 = 该列无缺失，不占额外内存）。
  ② **空字段不参与类型推断**：否则 `1,,3` 被推成 Str，数字列退化成文本列；
  整列全空 → Str。
  ③ **写入即"有值"**：写非空值清掉缺失标记，写空串到 Int/Float 列**重新**
  置为缺失。两者必须对称，否则出现"值是 3 但标记仍缺失"的自相矛盾状态。
  ④ **聚合跳过缺失**（求和/均值/最值），不是当 0；全列无有效值返回 0.0
  而不是 NaN——NaN 会顺着下游的平均再平均，污染出一片垃圾。要区分"没有
  有效值"与"有效值恰好是 0"，另给一个有效值计数。
  ⑤ **带小数的填充值要先升型再填**：往 Int 列填均值 1.5 会被截成 1，
  **静默算错**（同一个坑在窗口均值上踩过一次）。所以填小数前把列升成 Float。
  ⑥ **掩码随数据流传播**：任何"复制行"的新表（`Filter`/`Copy`/`SortBy`/
  `Concat`/重采样）都要带走掩码，否则一趟变换就丢了缺失信息。
  ⑦ **掩码按需增长**：只在"掩码为空"时补齐是不够的——追加第二个缺失格
  时掩码不跟着长就越界。正确做法是标记第 row 格时把掩码补齐到 row+1。

- **设置面板接入定式（Game.Foundation 三件套）**：
  一个项目要"有设置"，不重写存盘与档位计算，只写三样：
  ① **声明**：`GameSettings` 里声明选项（`AddBool`/`AddIntChoice`/`AddOption`）；
     离散档位用 `AddIntChoice`（`AddInt` 按 min..max 枚举会摊出几十档，按钮
     组没法用）。
  ② **档位逻辑**：`SettingsModel` 把声明翻译成"一行几个档、当前第几档"，
     选档写回 + 存盘。**视图各项目自绘**（美术风格不同），但这段易错的
     逻辑只有一份。
  ③ **接线**：HTML 设计稿里档位按钮的 id 与事件名统一取
     `opt_<键>_<值>`（如 `opt_aiDiff_hard`）——按语义而不是按行号，增删
     选项不会让按钮与行错位；场景类遍历 `SettingsModel` 生成同名事件。
  ④ **高亮必须 class 驱动**：视觉属性写进 `<style>` 的 `.opt` / `.opt.on`，
     行内 style 只留几何（行内 style 在 HTML 构建器里会合成 `.zgen-N` 规则，
     会把 class 高亮压掉，详见 gui-design skill）。状态类用
     `SetClassIn("optstate on", on ? "on" : "")`——group 里必须带上要清除的
     状态类，否则取消高亮时清不掉。
  ⑤ **落盘要跑两遍**：设置只在内存里 = 没有设置。测试用例首跑写、次跑
     （新进程）读回断言，两遍的值必须不同（首跑默认值、次跑上次写的值）。
  坑出处：xiangqi / gomoku / weiqi / breakout / ddz 五个项目接设置时，
  高亮不生效的根因是"行内 style 压过 class"（实为没写那条 class 规则），
  `SetClassIn` 的 group 不含状态类会留下脏 class。

- **协程入口与 `Task.Run` 静态析构竞态**：`Task.Run(AsyncFn())` 是即发即忘
  （fire-and-forget）；从同步 `static void Main()` 调出后，主函数立刻返回并执行
  `__zan_static_cleanup()`，把所有 `static` 字段（常量查找表、单例、零值对象）
  释放并置空，正在后台跑的协程随后访问静态字段即空指针崩溃。需要等待协程跑完的
  入口与探针必须声明 `static async void Main()`。

- **容器与指针原语契约三则**：① `List<T>` 没有 `int` 容量构造器（`new List<T>(n)`
  运行期崩溃），预分配一律 `new List<T>()` + `.Reserve(n)`；② `Queue<T>.Count()`
  是方法而非属性（与 `List<T>.Count` 属性相反）；③ `nint` 构造器与形参不隐式接受
  整型字面量 `0`，须显式强转 `(nint)0`。

- **无确定性 `Dispose()` 对象的异步串行化**：`AsyncRwLock` 内部创建 OS `Mutex`
  且必须显式 `Dispose()` 回收；对于没有确定性关闭生命周期的代理/会话对象，需要串行
  执行异步操作时应使用 `lock (this)` + `bool busy` + `Queue<AsyncGate>`（零 OS
  互斥量，随对象 ARC 回收不泄漏）。锁内只做状态切换与出队，`AsyncGate.Set()` 唤醒
  一律放在 `lock` 块外，避免重入回调引发死锁。

- **数据库连接池、驱动锁与原生句柄生命周期**：
  ① 跨线程共享的连接门锁（`AcquireLock`/`ReleaseLock`/`Close`）必须用 `lock (this)`
  保护状态位与等待队列，`Close()` 将等待队列摘出并在锁外逐一唤醒；② 建连/握手/打开
  文件在任何提前失败路径都必须调用 `Close()` 回收已分配的 TLS、socket、认证上下文、
  `RwLock`/`Mutex`，对象销毁时逐级释放子组件持有的原生互斥量；③ 使用
  `AsyncTlsStream` 的连接在连接池归还遇到残留事务时，严禁降级调同步 `Rollback()`
  直接读写底层 fd（会绕过 TLS 帧层或破坏密文流），须异步派发 `RollbackAsync()`
  完成后再归还或关闭剔除；④ POSIX 文件描述符有效性判据是 `fd < 0`（`0` 是合法 fd，
  严禁 `fd <= 0`）；⑤ 跨进程/多工作进程并发写锁或租约文件（如分布式锁/会话表）
  须带 `Environment.ProcessId` 前缀，避免不同进程内自增计数器撞号。

- **不受信任输入、协议缓冲与系统互操作边界**：
  ① 解压帧（LZ4）、磁盘快照（HNSW `batchLen`）、分块流与加密迭代次数（PBKDF2/SCRAM）
  在分配或循环前必须校验剩余字节与安全上限（如 KDF 迭代上限 `10_000_000`），防止
  恶意输入触发 OOM 或 CPU DoS；② 图像像素缓冲区宽高乘积与步长计算用 64 位整数防
  溢出，缩放在 `w==1` 或 `h==1` 边界独立钳制插值索引；③ 只读 JSON 响应解析优先使用
  零树分配的 `JsonDoc`（`FirstChild`/`NextSibling` $O(N)$ 遍历，严禁循环调用
  `At(i)` 退化为 $O(N^2)$），RPC 请求统一走 `JsonRpcBuilder`，JS 参数注入统一走
  `JSON.parse(JsQuote(...))`，URL 主机解析兼容 IPv6 `[host]:port`；④ 高频协议
  编码缓冲与密钥流（TDS/Firebird/RESP）用紧凑 `byte[]` + `NativeMemory.Copy` 替代
  `List<int>` 逐字节拼接，订阅表退订直接槽位置 `null` + `activeCount` 实现 $O(1)$
  并即刻释放闭包引用；⑤ Windows GDI `SelectObject` 必在 `try/finally` 中恢复旧对象
  再释放 DC，剪贴板/动态库搜索目录（`SetDllDirectoryA`）必在 `finally` 还原。

- **多线程并发用例防启动竞态**：测试读线程与写线程并发时，若主线程在写线程结束后
  等待 `while (readRounds == 0)`，而读线程写成 `while (writeDone == 0)`，高负载下
  写线程抢先跑完会导致读线程零轮退出、主线程永久死等。读线程循环须保证至少执行
  一轮（`bool first = true; while (first || writeDone.Load() == 0) { first = false; ... }`）。

- **重度治理实证四则（Waves 14–16：内存重叠、资源生命周期、加密常量时间与 64 位防溢出）**：
  ① **重叠内存搬移禁直调 `NativeMemory.Copy`**：`NativeMemory.Copy` 底层是 `memcpy` 语义，同缓冲向后重叠搬移（`dst > src && dst < src + count`，如 `RingBuffer` 头部右移）会导致数据损坏，须改用从尾部向前的倒序搬移或按 `memmove` 语义分支处理；
  ② **资源块语法与一次性对象清理**：Zan 的 `using` 资源语句须带括号块 `using (Type x = ...) { ... }`（不支持 C# 8 无块 `using Type x = ...;`）；每次创建的 `HttpClient` / 临时连接与事务必须在 `try/finally` 内关闭或按 `committed` 标志自动 `Rollback()`，socket `Close()` 须按 `sock >= 0`（而非 `connected`）释放，避免建连失败或已断线路径泄漏句柄；
  ③ **密码学与二进制协议边界**：JWT/MAC 比较（`fixedEquals`）须避免按长度提前返回或对短数组跳过访存，静态查表/曲线懒初始化（`Aes`/`Ecdsa`）须用双检锁先填充局部表再发布；64 位文件偏移、Tensor 维度乘积/行偏移、网络帧长 `PacketFramer`/`ArpgMessage` 扩容与校验须在 `long` 域算完并检查上界，严禁 `int` 相加或翻倍回绕；
  ④ **ECS/容器 swap-remove 遍历与循环依赖**：在 `Dispatch`/`Tick` 中遍历允许 `KillSelf()` 触发 swap-remove 的数组时，命中当前槽被替换不得盲目递增下标（会跳过被换入的尾元素）；`AssetStore` 递归加载依赖须带 `visiting` 集合防循环依赖死循环，且依赖引用计数 `depCount + 1` 须在自身 `LoadBytes` 成功之后再提交。

- **重度治理实证五则（Waves 17–19：FFI/OS 边界、溢出与 NaN、协议/SQL/SSE 解析、句柄/队列与泛型 `__Canon` 派发）**：
  ① **FFI/OS 原语安全边界**：`NativeMemory.PutString` 不写末尾 NUL 且 `NativeMemory.Alloc` 未清零，传给 C/POSIX/Win32 FFI 的窄串/宽串（`Process.StartDetachedSafe`、`Window.GetText`、`Registry.GetString`/`ListValues`）须显式补写末尾 NUL 并检验 API 返回值；`kill(pid, sig)` 与 `ProcessControl.*` 入口必须先拒 `pid <= 0`（防误杀进程组/全体进程），POSIX `PRIO_PROCESS` 常量为 `0`（`1` 是 `PRIO_PGRP`）；Windows `SetDllDirectoryA` 须加静态锁且仅对绝对目录调用。
  ② **算术溢出、浮点 NaN 与 32 位时间戳**：容量预检（`ByteBuffer.EnsureRoom`、`ZstdBuffer.Ensure`、`Tar`/`Zip.Create`）、像素矩形裁剪与步长偏移（`ImageBuffer.FillRect`/`Crop`/`Blit`）、分页偏移（`Select.Page`）和会话过期时间戳（`Sessions`/`SessionEntry.expiresAt`）必须在运算前或全程于 `long` 域校验上界；曲线/累计防回绕成 0 用饱和算术（`LongMax`）；浮点闭区间校验必须写 `!(v >= min && v <= max)` 拦截 `NaN`。
  ③ **协议/SQL/SSE 解析与序列化防线**：HTTP/2 控制帧（`SETTINGS`/`PING`/`WINDOW_UPDATE`/`RST_STREAM`/`PRIORITY`/`GOAWAY`）、MySQL 列定义/行包、Zstd 8 字节 `fcs`（防 MSB 置位变负）、TDS 累计消息体须先验载荷长度与上限再索引；`Dialect.QuoteIdentifier` 不得凭首尾字符盲信已加引号（须匹配当前方言并校验内部闭合引号均成对转义），DDL 列名走 `QueryBuilder.RequireIdent`；SSE 帧须剔除事件名 CR/LF 并按 `\r\n`/`\r`/`\n` 拆分 `data`；反射 JSON 序列化须设递归深度上限（512）防环引用栈溢出；Prometheus `Key()` 须转义标签值防撞键，累计直方图桶不可在 `Snapshot()` 二次累加。
  ④ **语言语义与句柄/队列模式**：Zan 中 `async void Foo()` 是标准可 `await` 的无返回异步签名；`JsonDoc.Parse` 返回整数节点句柄 `.root` 而非 `IDisposable`；持有 `FILE*`/OS `Mutex`/`SseSink`/`HttpResponse` 的类须实现 `IDisposable` 并在 `try/finally` 收口（`ZipWriter.Close`、`HttpFramer.Save*ToFile`、`AiStream.Pump`、`RwLock`/`TaskCompletionSource`、`Com.Create`）；有界缓冲淘汰用头索引摊还 $O(1)$ + 阈值紧凑替代 `List.RemoveAt(0)`，纯成员查重用 `HashSet<int>` 替代 `Dictionary<int, bool>`。
  ⑤ **编译器泛型接口 `__Canon` 派发契约**：`ifc_edge_matches_inst`（`src/compiler/irgen_call_spec.c`）匹配实现泛型接口的类时，除泛型类的类型形参边外，非泛型类实现具体引用类型实参（如 `class UniqueEmailRule : IAsyncValidationRule<UserDto>` 在 `ValidationPlan<__Canon>` 内调用）同样须在 `sa->is_canon` 分支解析 `ea` 并校验 `type_is_canon_eligible`，否则运行期报 `interface dispatch has no implementation`。

- **重度治理实证六则（Waves 20–22：协议/签名、加密/沙箱、并发/Dispose 与缓冲/PAL 边界）**：
  ① **对象存储与 HTTP 响应所有权**：OSS/S3/COS 请求路径须逐段 RFC 3986 编码且只编一次（`S3Client.SignRequestWithQuery` 直接复用已编码的 `RequestPath` 避免二次 `%2520` 编码），查询参数按编码后键值升序排序（SigV4 / 阿里云子资源），`PublicUrlFor` 须保留非默认端口与 pathStyle 前缀；同步提取 `HttpResponse` 的包装对象（`WechatRawResponse.FromHttp`）必须深拷贝 `headers` 列表并在 `try/finally` 释放 `HttpResponse`，避免 `Dispose()` 清空原列表导致悬空空头；下载失败应保留 `.progress` 并通过 `http.LastDownloadError()` 回传根因。
  ② **协议解码、异步套接字唤醒与加密原语**：Kafka/AMQP/Protobuf/CBOR/BSON/MsgPack/XML 解析器必须对记录边界、header 计数（`<= rem / 2`）、嵌套深度（AMQP 32 / XML 512）、无符号 64 位负数分支（CBOR `arg < 0` 走 8 字节 `ai == 27`）与写入总长做前置上界防护；`KafkaConnection`/`AmqpConnection` 关闭套接字前须在锁内原子摘出句柄并先调 `Socket.ShutdownBoth(s)` 唤醒挂起的异步读；支付随机串一律走 CSPRNG（`RandomNumberGenerator.GetBytes`）且失败即抛错，严禁时间戳降级；密码学入口（SM2/SM4/HMAC/SHA/HKDF/RSA/BigInt）须校验 `len <= buf.Length`、曲线点 `IsOnCurve`、输出槽非空以及偶模数 `ModPow` 二进制平方乘分支；Lua 沙箱须额外清除 `os.getenv`/`tmpname`/`setlocale`、`package.loaded` 并以 `"t"` 文本模式拒收预编译字节码。
  ③ **并发/Dispose 生命周期与协作式异步防死锁**：在对象可被并发访问或销毁的容器/连接中（`ServiceProvider`、`AmqpConnection`、`BackgroundService`），优先用 `lock (gate)` 替代裸 `Mutex.Destroy` 杜绝 `Dispose()` 竞态 UAF；协作式异步基类（`BackgroundService.Dispose`）严禁同步 `Thread.Sleep` 等待自身协程退出（单线程调度器下会自锁），应以锁保护的 `loopRunning` 标志交接清理权；`ServiceProvider` 须检测跨线程 AB-BA 等待环与每线程 Transient 递归环；`AtomicInt.NativeCompareExchange` 返回旧值（`== expected` 才算 CAS 成功），用于 `MediaPlaybackSession.Play` 互斥起线程。
  ④ **排序溢出、二进制缓冲与 PAL 跨平台细节**：降序排序严禁用 `sign * key`（对 `int.MinValue`/`long.MinValue` 溢出反转），须直接分支比较 `desc ? (a >= b) : (a <= b)`；`Slice`/`Skip` 在 `long` 域饱和累加；`BinaryBufferWriter`/`BinaryBufferReader`、`QrRenderer`/`BarcodeImage`/`VideoProcessor` 的切片与像素容量统一在 `long` 域验上界；POSIX `PosixEnvironment` 须用 `#if WASI`（而非 `#if WASM32`）并对 `/dev/urandom` 短读循环补齐，`PosixThreading.WaitForSemaphore` 绝对截止时间须在 `EINTR` 重试循环外计算一次，`PosixTextMarshaler` 须校验 UTF-8 续字节与合法码点范围；Windows `MultiByteToWideChar`/`GetCurrentDirectoryW` 须检查返回值并在超长时动态扩容，禁止读回未初始化堆内存。

- **重度治理实证七则（Waves 23–25：存储锁/时间与浮点边界、正则与游戏循环不变量、stdlib 流/并发/互操作安全）**：
  ① **跨进程文件锁、ARC 回调环与浮点→整边界**：`FileJobStorage` 跨进程状态变更（`Claim`/`Delete`/`CompleteUnlessDeleted`/`Snapshot`/`Find`）须在 `ReloadInternal()` 之前先获 `File.TryLock` 并持锁覆盖校验与追加，避免多进程读到陈旧状态；向调度器/容器注册回调（如 `JobScheduler.Register`）严禁闭包捕获宿主自身形成 ARC 循环引用，应仅捕获叶存储/服务或在收尾显式注销；IEEE 754 `double` 转 `long` 的安全上界是 `9223372036854774784.0`（$2^{63}-1024$），`9223372036854775807.0` 会舍入到 $2^{63}$ 强转溢出；Zan 字符串字面量不解释 `\xHH` 字节转义，须直接写 UTF-8 字符。
  ② **正则编译器防爆、游戏池回调重入与环形存档对齐**：`RegexProgram` 编译须设指令上限（65536）并在 `ApplyQuantifier` 用 64 位预估增量、`ParseInt` 饱和消费剩余数字、`ParseHexEscape`/`ParseFixedHex` 跟踪溢出防 32 位回绕绕过；对象池遍历回调（`BulletPool` 命中回调）必须先缓存字段并完成 `DespawnAt` swap-remove 再调用户回调，防止回调重入销毁/分配破坏池不变量；带外部存档上限的环形事件序号（如 `ForgeEvents.sequence` 遇 `ForgeSave` 1e9 上限）须在 2 的幂对齐边界（`999999936`）回绕，避免 `sequence & 63` 错位。
  ③ **stdlib 流关闭不可复活、二进制 `string` 兼容与并发 Dispose 纪律**：`MemoryStream` 须维护独立 `bool closed` 标志，禁止 `Close()` 后再被 `WriteByte`/`WriteFrom` 触发 `Grow()` 暗中重新分配泄漏原生内存，`Grow()` 在 `1 GiB < needed <= 2 GiB` 时应钳制到 `int.MaxValue` 而非翻倍越界误报；`StreamWriter.Close`/`CloseAsync` 须用 `try/finally` 确保 `Flush` 抛错仍关闭自有底层流；`FileStream.ReadInto`/`WriteFrom` 须校验 `readable`/`writable` 与 `buf != 0`；`Encoding.Base64EncodeBytes`/`Sha1` 存在 `byte[]` 以原始指针形态传入的兼容调用（如 `WebSocketAccept`），不可调 `data.Length`（会按 NUL 截断二进制摘要），但必须前置校验 `digest.Length >= 20` 防止跳过硬件快路径后写爆软件兜底；`CancellationToken`（且保护 `None` 单例不可被关闭/取消）、`BlockingQueue`、`SemaphoreSlim`、`Channel`、`AsyncRwLock`、`ConcurrentDictionary` 的 `Dispose()`/`Close()` 须先唤醒挂起者并把句柄清零再销毁，读写入口须缓存句柄到局部变量判 `0` 并用 `try/finally` 释放锁；队列紧凑条件改用 `head >= n - head` 消除 `head * 2` 溢出，`Marshal.Copy` 用减法判界 + 64 位字节偏移防止 `startIndex + length` 与 `* 4`/`* 8` 回绕。

