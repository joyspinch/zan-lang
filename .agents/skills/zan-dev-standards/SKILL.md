---
name: zan-dev-standards
description: Zan 开发规范总纲——可落地的标准与纪律，供 AI 开发/维护/调试任何 Zan 项目时遵循。涵盖：开发工作流（先查 API 再动手、编译运行验证闭环）、代码规范（自绘禁令、组件纪律、注释纪律）、验证纪律（截图锚定 PID、探针断言、无头仿真）、经验沉淀纪律（教训须通用可落地、不带流水账）。用 Zan 写任何程序（GUI/游戏/服务端/工具）、改 stdlib/templates/examples、或做实机验证与调试时使用。
---

# Zan 开发规范总纲

定位：给 AI 的可落地标准。每条规则都是"动作 + 为什么"，不含项目流水。
领域细则：GUI 排版见 `gui-design`；游戏帧循环见 `game-dev`；服务端架构见
`server-dev-standards`；数据建模见 `data-modeling`；SQL 细则见
`server-db-design`；编译器内部契约见 `zan-compiler-internals`；
版本提交/任务台账/会话卫生见 `zan-workflow`。

## 一、开发工作流（固定闭环）

1. **定向**：明确改的是哪一层（编译器/stdlib/模板/应用代码），读相邻同类的
   现有代码作为风格与写法基准。
2. **查证后再写**：不确定的 API 用符号索引（zan_api_search/lookup_symbol）
   查到签名再落笔，禁止凭印象造接口。
3. **编译-运行-验证**：`zanc <file.zan> --auto-stdlib -o out` 后必须运行
   并观察输出；"编译过了"不是完成标准。
4. **改编译器/stdlib 必须带 conformance 用例**（`tests/conformance/`），
   并跑受影响的最窄测试档；探针只算预检，不算验证。
5. **修根因，不绕行**：Zan 代码失败若源于编译器/runtime 缺陷，去修编译器，
   禁止把业务代码改写成绕开缺陷的形状。
6. **动包的公共 API 前全仓查消费方，grep 必须含 templates/ examples/**：包内
   类型被模板与服务端样例跨包消费是常态（2026-10-02 CSV 合并成包，两份旧
   `Csv` 共 8 处消费方，其中 4 处在 templates/ 里，漏一处 = templates_build 红）。
   改 Gui/stdlib 公共 API 同理——gui/async 重构给 gui-wechat 模板写了不存在的
   `Form.Close()`，模板自此编译不过，直到模板门复跑才暴露（2026-10-02）。
   删除/搬家包文件、改公共 API 之后用 `ctest -R templates_build` 收口——它是
   唯一编译全部模板的验收门。
7. **禁止手搓简化版复刻仓库验收脚手架**（run_templates.cmake 这类）：{{NAME}}
   内容替换、App→项目名重命名、manifest 三键（type/target/entry）解析、
   target 分支参数，少搬一步就是整批假阳性（2026-10-02 复刻缺 {{NAME}} 替换
   与入口逻辑，19 个模板假红空转一轮，真失败只有 1 个漏网消费方）。能
   `ctest -R <名>` 就直接跑；复刻仅用于定位单模板问题，且必须对照原脚本
   逐行搬齐。

8. **跨平台模板不固化开发机目标，实例配置以用户选择为准**：通用模板省略
   固定平台；创建项目时按键覆盖用户显式选择，不能只在键缺失时追加，也不能
   依赖图标等可选资源是否存在。未选择时按宿主 OS 与架构回退；读取平台只解析
   对应字段，禁止全文搜平台名。模板复制曾保留 Windows 默认值吞掉向导选择，
   而本机编译仍通过；验证必须断言生成的配置，并覆盖已有键、缺失键和多平台。
   模板自带的 zan.proj 同理要参数化：写死的 `name=` 会被发布管线读走当产物名
   （gui-workbench 实案：项目叫别的名字，发布出来还是模板名）；模板里的死键
   （`auto_stdlib` 全仓无消费方）与手搓平台 API（kernel32 设环境变量在非
   Windows 静默失效，stdlib 已有跨平台对应物时必须换）都是同族债。

## 二、代码规范

- **GUI 与游戏排版及实体建模统一规范（Flex + Float 单轨化，废除双轨与旧式 Dock）**：
  ① 界面与游戏 HUD 排版模型归一：全面收敛为 `Flow`（参与父级 Flex 弹性盒主轴与交叉轴伸缩、间距 Gap）与 `Float`（脱离文档流绝对/浮动摆位，结合九宫格 Anchor 锚点对齐自适应分辨率）；彻底淘汰传统桌面切边式 Dock 停靠算法与独立的游戏绝对像素画布；
  ② 游戏 UI 本质即 GUI 皮肤化：游戏世界视口（`GameViewport`）作为 GUI 控件树中的内容节点，上层 HUD、背包、血条、技能栏直接复用标准 GUI 控件（`Button`/`ProgressBar`/`Panel`/`Flex`），视觉差异纯粹通过 CSS 类、主题贴图与九宫格边框（Border-Image）实现；
  ③ 实体强类型与 Enum 规范：高频渲染、设计器文档模型与动作分发全面采用强类型实体（如 `SceneActionBinding`、`FieldPositionMode`、`SceneAnchor`）替代动态弱类型字典或通用 `JsonValue` 解包，彻底根除高频帧循环中的哈希查找与堆内存浪费；`JsonValue` 仅作为磁盘持久化边缘的序列化载体。
- **stdlib/Gui 界面禁止自绘**，全部用标准库控件与布局原语完成。
- 组件写法与同目录既有组件保持一致：命名、事件命名、state 管理同构。
- 注释只写代码看不出来的约束与"为什么"；不复述代码、不写日期流水、
  不写"本轮修改了什么"。
- 常量收拢：魔法数进常量；字段名引用 schema 生成物或常量，不散落裸字符串。
- 资源（图片/字体/大 JSON）走内嵌或声明式引用，不在业务代码里手写路径拼接。
- **高频基础数据热路径杜绝逐字符/堆分配循环**：
  ① 文本解码（`Encoding.UTF8.GetString`）优先利用 `bytes.IsAscii` AVX2 向量化探测，纯 ASCII 直通单次 `memcpy` 投影；十六进制编解码（`ToHexString`）禁止在循环体内使用 `Substring` + `StringBuilder`，必须单次预分配 `byte[]` 配合 16 字节字符常数表直接位运算寻址写入；图像像素批量处理（`ImageBuffer`）全面废除浮点乘除与逐点函数调用，统一采用定点数移位（如灰度加权 `(B*29 + G*150 + R*77) >> 8`）与向量化批处理；
  ② 子串检索与前缀后缀匹配全面对接编译器原生机器指令 `s.IndexOf(needle, from)` / `s.LastIndexOf` / `s.StartsWith` / `s.EndsWith`，彻底杜绝手写 `while (i + n <= h) { if (hay.Substring(i, n) == needle) ... }` 反模式（消灭千万级临时堆分配与 ARC 记账，实测提升 49 倍）；
  ③ 字符码点判断与十进制数字解析统一使用原生只读单周期字节索引 `s[i] & 255`，严禁通过 `NativeMemory.Alloc(8)` 写入再 Span 取首字节释放的堆外分配反模式，严禁逐位 `Substring(i, 1)`；
  ④ 文本转义、分词与模板解析统一采用分块游标切片（Window Chunking），无特殊字符时直通返回原始字符串（0 堆申请），含特殊字符时以区间切片追加，消灭逐字节碎片分配；字典键值查询优先使用 `vars.ContainsKey(key)` 与 `TryGetValue`，严禁遍历 `vars.Keys` 数组造成 $O(N)$ 性能降级；
  ⑤ GUI 与游戏高频渲染循环严禁闭包委托与多重 Span 重复构造：`YSortLayer` 等空间与深度排序结构采用紧凑索引直接访问（`GetSortedEntityId`）替代 `ForEachSorted` 委托闭包，消灭每帧闭包分配；`SpriteBatch.Add` 复用单一 `Span<float>` 实例完成 8 浮点装填；富文本与代码高亮词法探测（`CodeEditor.InSet/ContainsSub`、`Markdown` 行内解析、`ChatView.MdStrip`）全面直通原生 `IndexOf` 与分块游标区间切片；
  ⑥ 网络与通讯协议热路径（WebSocket、Redis RESP、HTTP 流）全面采用 AVX2/SSE2 向量化与 NativeMemory 零堆分配加速：WebSocket 数据帧 4 字节掩码在广播构造 64/128/256 位宽掩码向量后通过 `Vector256.Xor` / `Vector128.Xor` 单指令处理 32/16 字节，非对齐尾部按 4 字节整数步进异或；Redis RESP 文本行协议与 WebSocket HTTP 握手终止符（`\r\n` / `\r\n\r\n`）采用带界限限制的 `MemoryExtensions.IndexOf`（AVX2 `Equals` + `ExtractMostSignificantBits` + `TrailingZeroCount`）实现单周期 32 字节跳跃定位，杜绝逐字节标量比对与越界脏读；缓冲区扩容与搬移严禁使用 `for` 循环逐字节赋值，统一调用直接对接 libc `memmove`/`memcpy` 的 `NativeMemory.Copy` 与 `NativeMemory.PutString`。
- **ZanWeb/Mvc 视图 CSP 纪律（`[security].csp` 开启后 `style-src 'self'` 拦内联）**：
  `style=""` 属性、`<style>` 块、`onclick=` 内联 handler、JS 里 innerHTML 拼进的
  style 属性全会被拦；CSSOM 赋值（`el.style.width=`）不受限。视图写法：th/td 列宽用
  `width="N"` 表现属性；组合样式进 admin.css「视图去内联工具类」或语义类；模板条件
  样式把 `{{#if}}` 搬进 class（`class="chatrow{{#if mine}} mine{{/if}}"`）；动态百分比
  宽高用 `data-w`/`data-h` 交 admin.js 统一 CSSOM 回填；页面脚本一律外链 `/static/js/`
  并 `defer`。（坑出处：B-ID80(d) 框架+模板两次全量清扫，902+177 处内联样式；动态宽高
  与 tooltip 色点两种 CSSOM 形态实机验证。）
- **杜绝手拼 JSON 字符串与手拼 SQL 拼接（严禁裸字符串拼接与简陋的 Replace 引号）**：
  ① JSON 序列化一律使用标准库 `System.Json.JsonValue` 构建器（`PutStr/PutInt/PutDouble/PutBool/PutNull/PutJson` 与 `Append`），字符串成员经原生转义安全处理特殊字符（引号、反斜杠、控制字符 `\n`/`\r`/`\t`），杜绝因换行或引号破坏 JSON 报文语法；嵌套片段利用 `PutJson`/`AppendJson` 原生嵌入，禁止通过文本剪裁与字符串拼接嵌入；
  ② 数据库非查询/查询/聚合操作一律使用参数化 API（`DbParams` 与 `?` 占位符），禁止手写拼接 `Quote` 单引号或裸拼接变量（如 `"WHERE slot=" + key`），杜绝 SQL 注入漏洞并复用数据库预编译执行计划；
  ③ 实体持久化与业务数据访问优先使用原生 ORM 表达式与模型映射（`Select<T>()` / `Insert<T>()` / `Update<T>()` / `Delete<T>()` / `DbTable.Of()`），通过 lambda 强类型表达式（如 `a => a.slot == slot`）或类型化查询链，由编译器与 ORM 生成层自动完成列名白名单核验、类型绑定与参数化，从架构根源上根除手写裸 SQL 字符串的脆弱性。
- **ORM 条件只有值自带原语，裸 SQL 片段是生成代码专属**。用户面条件一律
  `WhereEq/WhereGe/WhereLe/WhereIn/WhereLikeAny`（列名经实体元数据校验，
  值全部参数化绑定）与类型化 lambda（`ids.Contains(a.id)` 降级为参数化
  IN；空列表恒假，不会生成非法 `IN ()`）；列表筛选收集成 `List<OrmCond>`
  后 `WhereConds(conds)` 一口吞（多列 LIKE 用 `|` 分组 OR）。批量更新/
  删除走 `DbTable.Of(conn, "表").Query().WhereIn(...).ExecuteUpdateAsync(...)`
  （列名过 RequireIdent 白名单）。`W("片段")+InI(...)` 这类片段/参数分离
  写法已整体删除，只以 `__` 前缀（`__Frag/__Pi/...`）存在于编译器生成代码
  里，手写代码不可见；要给查询链接名字仍用门面根
  `__DbQ_Post sel = __DbBind.Q_Post(conn);`（裸赋值 `this.Post` 编译不过）。
  坑出处：片段+参数靠位置配对，编译器查不出错位，历史上实烂三处
  （2026-09-25 根治，值自带后错位不可能发生）。
- **Zan.Web 菜单是显式选择（GenRoute 约定），别靠描述"顺便"进侧栏**。进
  管理菜单只认显式标注：`[Custom(IsMenu = true)]`（类级或动作级，图标/权限
  同写在那一处）、`[Menu]`、`[AdminAction(IsMenu = true)]`；类级
  `[Description]` 只做文档与标题回退，**不会**让控制器进菜单。非根动作
  没有方法级显式标注一律不进——类级菜单配置只作用于根动作。方法层显式
  标注永远赢过类级默认（两层合并类先方法后）。坑出处：登录/健康检查这类
  有描述的公开端点曾被类描述隐式默认刷进侧栏，而 Monitor 非根动作上
  已标注的 `[Custom(IsMenu=true)]` 又被反向吞掉（2026-09-25 GenRoute
  改约定后两端同时修复，web_menu_attrs 用例锁定）。
- **语言事实三则（2026-09-25 CRUD 声明化实机踩出）**：① `protected`
  是**仅子类可见**——同包同命名空间的协作类也访问不到（`Crud` 访问
  `AdminController` 的 `Can/Note/Saved` 直接编译错）。包内协作面要么
  放开为默认公开（如 Note/Saved 这类通用应答/审计），要么像
  `Screen(canUpdate, canDelete)` 那样由子类调用方取好掩码再当参数传；
  ② 关键字不能当标识符——`where`（lambda 语法）与 `set`/`get`（属性
  访问器语法）都是保留字：`where` 做变量名报 `unexpected token` 一串，
  `DbValues set` 参数把整段方法解析炸成上百条级联错（各改名实修）；
  另 `StringBuilder.Append` 返回 void，`sb.Append(x).ToString()` 链式
  取值编译不过，必须拆两条语句；
  ③ async 方法同一表达式里 await 前不得再求值带副作用的调用
  （`this.Ctx().path` 要先赋局部变量），编译期强制。
- **语言事实第四则（2026-09-27 全站假死排查踩出）**：`virtual` 在派生类
  里是**新开槽位**（隐藏基类实现），不是覆盖——挂接框架钩子必须写
  `override`。AppController 曾把 `OnBeforeAsync/OnAfterAsync` 声明成
  `virtual`：路由蹦床经 `Controller` 静态类型的槽位派发，新槽位永远
  打不中，跑的是基类空实现——请求租约借了不还，池按需扩张到上限后
  Acquire 全部挂满超时，全站 503/无响应（客户端只见偶发 000）；而
  `base.` 这类静态引用仍能打中新槽位，半通半不通最迷惑。排查定式：
  池 acq/rel 计数失配 + 钩子首行日志打不出来 = 槽位没接上。
  2026-09-28 起 zanc 对该形状直接告警（`--deny-warnings` 升级为编译
  失败），diag_virtual_hides_inherited_virtual 用例锁定，合法重载与
  真 override 不误报。
- **语言事实第五则（2026-10-01 XML DOM 包首日踩出）**：① 包命名空间按
  using 拉取：`packages/*` 里的类型必须先 `using System.Xml;`（对应的
  包命名空间）才入编，漏 using 直接报 `undefined type 'XmlNode'` 且不
  指向缺 using——新包首次使用先补 using 再排查别的（与 stdlib 按需拉入
  同一门槛，包从 packages/ 发现根扫描）；② 可空返回值不得链式解引用：
  checker 强制（`'RootElement' can return null; accessing ... faults at
  runtime`），既有代码惯例是存局部变量判空，无 `?.` 使用先例、别臆测其
  形态；深层取值逐层存局部判空，同根多条断言用 `if (x != null) {}` 包住
  整段，测试探针尤其常用。
- **语言事实第六则（2026-10-01 Zan.Toml 包首日踩出）**：① `JsonValue.At(i)`
  是**数组专属**——对象上调 `At(i)` 直取数组底层的 `items[i]`，对象该列表
  恒空，无边界检查直接段崩（编译期查不出）；对象遍历配对用
  `KeyAt(i)`/`ValAt(i)`，`Count()` 两种形态通用；② 无序格式的往返相等
  **必须按键集合深度比，不能比 ToJson 原文**——写出器把行内对象键合法地
  挪进 `[表头]` 区块，同层键序变化是语义不变的（TOML 表无序），拿 JSON
  文本判等会误报失败：递归 `Eqv(a,b)`（对象逐 KeyAt 查 Has+ValAt 递归、
  数组按序递归、标量按类型比）才是正确闸门。
- **语言事实第七则（2026-10-01 Zan.Protobuf/Zan.MsgPack 首日踩出）**：
  ① 数值类型只有 `int`/`long`/`double`/`bool`——没有 `float`，也没有
  `sbyte`/`short` 有符号小整型：32 位浮点字段做不了（当文档化取舍写进
  README），小整型符号扩展手工算（`v >= 128 → v - 256`），别臆测类型
  存在；② IEEE-754 位型互转没有 reinterpret，配方是复用
  `TdsMessage.DoubleBits` 归一化构造（**±∞/NaN 必须先收口**——归一化
  循环对非有限值永不终止）+ `MySqlWire.ieeeToText` 形状的逐位累加解码
  （各项 2^(b-52) 恰可表示、和精确）；解码侧造 ±∞/NaN 用
  `Pow2(1024)` 溢出与 `inf - inf`，**不能拿特殊位型递归自造**（位型
  仍是特殊值，无限递归）；③ 判 NaN 用 `x != x`，判 ±∞ 用
  `x > 1e308` / `x < -1e308`——`x == x` 对 ∞ 恒真，照 NaN 惯用法写
  会误报失败。
- **语言事实第八则（2026-10-01 Zan.Yaml 首日踩出）**：① 同类里
  **static 方法调用实例方法不报检查器错**，直到 LLVM verification 才炸
  （`Operand is null`，指向被调函数的 this 实参）——报错不提示
  "static 调实例"，纯帮助类小函数一开始就 `static` 化（`JsonValue`
  伴随 helper 如 `HexDigit`）；② 字符串转义集合有限：`\b`/`\f` 是
  编译错（invalid escape sequence），要控制字符用
  `Encoding.Utf8FromCodePoint(8)`（退格）/`(12)`（换页）落；③
  `while (true)` **合法**（stdlib TaskJoin/ByteBuffer/JsonTape 都在
  用）——别凭"语言不支持"把循环改写成标志位绕路（此前会话曾这样
  误绕，证伪撤案）；④ 可空返回链式取成员（`Get().Get()`）检查器
  强制"先落地局部 + 判空"，测试代码给一个永非 null 的哨兵 helper
  （缺失返 `NewNull`）一次消音全部链式断言。
- **语言事实第九则（2026-10-03 深审修复批连踩两次）**：转出型 `byte[]` 的
  `Length` 含尾随 NUL 槽——`ByteBuffer.ToBytes`、hex/字符串转字节这类 API 产出的
  数组比"逻辑数据"多 1 个收尾 0 字节（既有契约，bytebuffer_bounds 金样钉死）。
  拿 `.Length` 当数据长度喂底层（实例：RSA hex 密钥按字节数传参）会多传一个 0
  而被拒或错算；真实长度用写入/转换时记下的 n 传递，不反查数组。
- **语言事实第十则（2026-10-04 异步调度与高并发进程/同步原语重构踩出）**：
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

- **文本格式包的 BOM 防线（2026-10-01 Yaml/Toml/Xml 跨平台审计踩出）**：
  `File.ReadAllText` 已在文件层剥 UTF-8 BOM，但 `Parse(src)` 收网络报文/
  内嵌资源时没有这层兜底——Windows 工具产出的文本常带 EF BB BF，不剥
  首键/首标签带隐形字节或报"expected <"。任何文本格式解析器的 Parse
  入口自己剥一次 BOM（三字节判定 + Substring(3)），conformance 加
  BOM+CRLF 向量锁行为；换行侧在切行层吃掉 CRLF/孤立 CR，写出恒 `\n`。
- **声明驱动的 CRUD 定式（ListPage/Crud，2026-09-25 落地）**：一屏一个
  `ScreenDef()` 静态声明（列 Col/Tag/Flag、行内操作 Ops、筛选、工具栏、
  `Table/EditFields` 写白名单），渲染 `Screen()` 整段产出 screenHtml
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
  单块向量对上述两类 bug 全都测不出来（2026-09-25 RFC 7914 形状
  向量 + Hmac.Compute 对拍双保险锁进 tests/conformance/pbkdf2）。
- **动态加载系统 C API 时逐参数对照目标 SDK 头文件**：函数指针不得凭
  印象省略“编码类型”等中间参数；Crypt32 的 `CertAddEncodedCertificateToStore`
  曾少声明一参，编译和链接都通过，却在首次加入证书时进入系统 DLL 后
  崩溃。对负面策略做多次调用并在崩溃时取原生回溯；Windows SDK 结构
  成员还受头文件宏与版本控制，限时字段不可用时用缓存-only 策略失败
  关闭并明确互通限制，不能改成无界网络查询。
- **异常面 catch 裁决 taxonomy（B-ID31 全量裁决沉淀）**：Zan 代码里合法
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

## 三、验证纪律（实机/无头通用）

### 探针测量与脚本改文件的三条硬纪律（2026-10-02 B-ID48 排查沉淀）

- **测"超期/耗时"先核对单位与减数**：耗时量是微秒、延时参数是毫秒，`raw - delayMs` 少乘 1000 会把 0.5ms 真超期算成 50ms 假超期（本轮"2× 回归"查了两轮调度器，结果是自己探针的单位错）。测超期一律 `raw - delayMs * 1000`。
- **Stopwatch.GetMilliseconds()/GetMicroseconds() 是静态单调钟读数（墙钟），不是流逝时间**：拿来当 elapsed 用会打出 1 亿毫秒级的数（2026-10-02 B-ID83 探针打出 `build=102483857ms`，误判为计时器坏了）。测流逝要么 `Stopwatch.GetMicroseconds()` 前后两次相减，要么 `StartNew()` + `ElapsedMilliseconds()`。
- **trace 打点必须带绝对时刻，成对相对量串不成时间线**：`late_us` 只能回答"迟了几多"，回答不了"第几段路迟的"；push/fire/park/slept 各打点带 `zan_co_precise_us()` 绝对值，一轮就能定位迟滞在停车原语还是派发路径。
- **脚本批量改文件必须读原文→逐处断言命中→写回→复验大小**：`open(p,"w").write(变量.replace(...))` 里变量不是文件内容时写出 0 字节（本轮两个运行时源文件被截断，靠会话内 grep 证据+HEAD 重建）；写完 `len(...)` 不对就停，别继续跑。

- **截图必须锚定进程 PID、按窗口抓取**，禁止全屏抓图后肉眼找窗口——
  全屏抓到的是最前面的任意窗口（编辑器/旧实例），拿错误窗口的像素做判断
  会得出"程序坏了"或"修好了"的假结论。先记录启动 PID，再按 PID 枚举顶层窗口
  逐个抓取；抓不到目标窗口就重试，不要让工具猜。
- **UI 驱动用合成事件，不用真实 OS 点击**：driver 的 `clickid` 在点击时刻
  解析命中区中心并注入，天然免疫窗口框偏移；hit id 只在**同一次构建的
  同一次运行内**有效（控件增删会整体移位），点击前当场 dump。
- **ZanWeb 模板实机核对坑清单**（2026-09-25 ListPage/FormPage/视觉重绘验证起，每条都白折腾过一轮）：
  ① 静态资产挂在 `/static/*`（`StaticFiles.Mount(app, "/static", "wwwroot")`），
  curl `/js/x.js` 拿到的是 API 层 `{"code":"404"}` JSON——不是"服务了旧文件"，
  先核对 URL 再怀疑缓存；② 改 wwwroot 的 JS/CSS 必须同步升 `views/Admin/layout.html`
  里的 `?v=N`：资产响应带 `Cache-Control: max-age=3600`，浏览器缓存键含查询串，
  只 reload 页面拿不到新 JS（新代码 "确认加载" 要看执行中的函数源码或版本参数），
  且版本号升级放在该轮资产编辑**全部完成之后**——先升 v 后再改 js，会以新版本号
  缓存旧内容，之后怎么刷新都是旧的（2026-09-29 菜单图标不渲染即此故，再升一位才好）；
  ③ `app.exe start` 会 daemon 出脱离启动任务的 worker，杀后台任务杀不掉它——
  重建 exe 报 `Permission denied`、旧进程继续占端口继续服务旧视图，须
  `app.exe stop` **并按 netstat 确认监听端口 PID 已清**（stop 不干净时链接
  照样 Permission denied）；④ config/views/wwwroot 按进程工作目录
  相对读取，起服务必须 cd 到发布目录，且 views 改动要重启才生效（视图缓存）；
  ⑤ e2e 与手动实机核对共用 `_scratch` 沙箱目录时，e2e 收尾会按自己的端口重写
  甚至留下"无 config"状态——手动起服务报"未配置会话密钥"这类假故障，先重新
  生成沙箱 config 再查代码；⑥ `--publish` 不落盘 views/wwwroot——运行时
  从磁盘读，手动沙箱要自己把模板的 views/ wwwroot/ 拷进发布目录（e2e 也是
  copytree 布置的），漏了就是整页 `<!-- view not found -->`；
  ⑦ config `worker.daemon=false` 时 `app.exe restart` 会前台阻塞把脚本挂死——
  起服务用 `(app.exe start > boot.log 2>&1 &)`，别用裸 restart；⑧ 手动起的
  沙箱实例占着 e2e 的端口时，e2e 起服阶段不会报"端口占用"，而是对自己的
  数据目录跑断言、拿到空表 IndexError 之类的假故障——跑 e2e 前先按 netstat
  停掉自己的实例；⑨ e2e_mvc.py 不带 `--build` 会复用沙箱既有 app.exe——
  改完源码直接跑，旧断言全绿、新断言全挂（测的是旧二进制，exe mtime 早于
  改动），极像"功能写坏了"；改源后必须 `--build`，或先核对 exe 时间戳。
  另：e2e 末轮 forgot 流程会把 admin 密码重置为 `newpass-e2e-123`，手动
  补测登录拿种子密码 admin1234 会误判"登录坏了"；⑩ 移动包内文件后
  必须全仓 grep 旧路径——CMakeLists 测试注册的 ZANC_ARGS 写死文件
  绝对路径，漏改就报 `cannot open file`（A364 重组漏了
  AiEndpointPolicy，白挂一个 smoke 轮才发现）；⑪ 表单视图是"片段页"：
  `/xxx/form?id=N` 返回裸 `<form>`（含 lay-footer 的取消/保存按钮，但无
  head/link/script），css/js 全靠 data-dialog 弹窗注入宿主页后继承——
  脱离弹窗直接 goto 片段 URL 验收，会拿到"样式不生效、页签点击无反应"的
  假故障（2026-09-29 表设计器验收白走一轮），交互必须在宿主页弹窗语境测；
  ⑫ `app.exe reload` 会把 worker 搞挂且 master 仍握着监听 socket——端口
  LISTEN 但请求 000 超时（连接进 backlog 无人应答，两实例先后中招），
  刷视图/配置一律 stop+start；master+worker 常驻时 exe 被运行进程占用，
  zanc 链接 `app.exe` 报 Permission denied，先 stop 并 netstat 复核再编
  （2026-09-29 SSE 30s 断流修复部署即踩）；⑬ 浏览器整页截图连续超时（30s 连发）先 DOM 快照确认页面没坏再改 clip 小区域出图——是取帧层不稳，不是页面坏了（2026-09-29 仪表盘验收 3 连超时，裁剪即出图）；⑭ 路由按注册串精确匹配，Route("/admin") 不收 /admin/ 尾斜杠——goto 得 {"code":"404"} JSON 先核尾斜杠再怀疑服务挂了；⑮ 浏览器窗口级截屏会把旁边开着的窗口拍进同一张图，图上出现"第二个应用副本/双份侧栏"≠DOM 有两个壳——先 elementFromPoint 或壳计数定真相再动手（2026-09-29 coder 页验收把邻窗 8123 误读成页面复制体）；⑯ CSS 表格自动布局里 td/th 的 width 只是建议，列位紧张时被压到内容宽（zt-cell 定宽 36 被压到 13，勾选列贴死相邻列）——定宽列必须配 min-width 才是硬下限；overflow 容器裁剪绘制但 getBoundingClientRect 仍报全宽，量"是否溢出"要看 scrollWidth>clientWidth 或视觉，别信 rect（2026-09-29 窄视口审计 zt-cell 压缩 + 部门/知识库两页 rect 误报）；⑰ grid 子项的 margin-bottom 参与轨道行高——为去双倍间距把它清零，会把"最后一行卡片借给容器的 12px"一起清掉，网格之间归零贴死；块级兄弟的纵向节奏要给唯一来源 `.parent > * + * { margin-top }`（与卡片残留 margin-bottom 自动折叠取一份），修一处别再靠多层 margin 叠加（2026-09-29 监控页网格间 0 间距回归）；⑱ try 内 return 的返回值遇 finally 含 await 会写坏——返回值溢出到 $resume 入口 alloca，finally 挂起恢复时入口块重执行出未初始化栈格，裸 await/接住结果/循环三形态全坏、返回假 false 不进 catch（2026-09-30 Schema.Ensure 静默失败定位；当晨 46a7e83a 修愈：溢出改走堆 frame RETSPILL 槽，常设用例 async_return_in_awaiting_finally，2026-10-02 疤痕编译器考古独立复证四形全假→HEAD 全绿）。挂账时"裸语句是触发面/接住结果即安全"的归因是形状巧合，勿再引用；写跨 finally 挂起的 async 形态先跑该用例，包侧接住 async 调用结果再分支保留为可读性纪律；⑲ GUI 与 Game 排版统一以 Flex 流式与 Float 自由/九宫格锚定定位为唯一模型，彻底移除传统 Dock 停靠：Dock 存在切角顺序耦合且无法做主轴弹性拉伸，现代 UI（Web/Flutter/Game）全部收敛为 Flex 弹性流（Flow，direction/wrap/gap/align/grow）+ Float 脱离文档流浮动（left/top/right/bottom/anchor/z-index），旧 .zform/.zscene 的 dockSide 转换为 Flow/Float 锚定，内部数据结构全面采用强类型实体（SceneDoc/FormField/SceneActionBinding）与枚举（FieldPositionMode/FieldAnchorMode），JsonValue 严格限制在磁盘读写边界，热路径禁止动态哈希查找与装箱分配。
- **共享工作树上的测试归责：先隔离再定责**。smoke/e2e 结果异常先查
  并发提交时间线（`git log --format="%h %ad %s" -3`）：共享树另一会话
  在途编辑 stdlib/编译器期间跑测试，产物混进 WIP 源，无关测试假挂假绿
  （2026-09-25 GUI 拖拽 ctest 挂死 vs 手动快速 FAIL 二相性， targeted
  `git stash push -- <自己的文件>` 复跑一次即证明与己无关，TASKS A368）。
  同产物"ctest 挂死、手动跑通"先手动复跑再怀疑代码。编译器本身被并发
  改动搞崩时（zanc 段错误/未改源也 Access violation），不碰 build/ 与
  对方文件：`git worktree add _scratch/<名> HEAD` 后用主缓存同款编译参数
  （clang/ninja/LLVM_DIR 见 build/CMakeCache.txt）自建干净 zanc 验证
  自己的改动（2026-09-29 表格修复验证即此法，用完 `git worktree remove`）。
  进程静默死亡（无日志、无 WER 事件）同样先怀疑并行会话清场：按映像名的
  `taskkill /IM` 扫荡会波及同名的演示实例——重启同一二进制若稳定存活即坐实
  外部干扰，别急着改代码；可预配 WER LocalDumps（DumpFolder/DumpType/
  DumpCount 三注册表键）兜底，真崩溃会留转储，无转储+可复现存活就是环境账。
- **探针先验证探针本身（阴性对照）**：用"必然失败"的探针（如语法垃圾文件）
  确认检测通道真的会报错，再采信"探针没报错=无罪"——探针不炸只说明被测
  代码根本没进检测路径（如包文件未进解析集），这类阴性结果才有信息量；
  没做过阴性对照的"通过"不算证据。
- **编译健康普查必须按各程序的官方输入形态（2026-10-01 templates/examples
  普查教训，38 项里 4 项假红全是普查方法学造的）**：伴生文件要合编
  （gui_gallery 的 MapChinaData.zan 与 components/，gui_charts 甚至跨目录
  借 gui_gallery 的伴生文件——见 build_charts.ps1）；html 设计稿必须
  显式作编译输入（zanc **不**自动发现入口旁的 .html，模板 src 里的
  App.html 也一样要传；漏了报一片 undeclared，且可能被更早的类型遮蔽
  错掩蔽成单错）；无 Main 的教学快照库（examples/crypto_reference）按
  "零 error 行"判绿（不链接是预期）；单文件散装示例才逐文件独立编。
  拿不准就抄该目录 README/构建脚本的 zanc 命令行。
- **zanc 失败退出码恒 0**：编译出错也返回 0（自身崩溃除外），脚本判定
  必须 grep 输出里的 "error"，不能只看 `$?`。另注意 stdlib 跟 zanc 的
  exe 目录走、包跟 cwd 走（向上找 zan.proj）——换 zanc 做 A/B 时包解析
  随 cwd 变，对照组必须钉住同一 cwd。
- **PowerShell 合成点击四连坑（PrintWindow 抓窗 + mouse_event 注入流）**：
  ① 进程必须先 `SetProcessDpiAwarenessContext(-4)`——DPI 不感知时
  `GetWindowRect`/`SetCursorPos` 全在虚拟化坐标系，注入点整体漂 1.5 倍；
  ② 前台锁下 `SetForegroundWindow` 会被拒，点击会落进盖在上面的别的应用
  （浏览器等），须 `SetWindowPos` TOPMOST 再 NOTOPMOST 置顶回合；已最大化的
  窗口别再 restore/maximize 折腾——`SW_RESTORE`→`SW_MAXIMIZE` 与
  `GetWindowRect` 的竞态会读到中间态矩形，坐标全体错位；③ 抓像素用
  `PrintWindow(PW_RENDERFULLCONTENT)`，`CopyFromScreen` 抓的是屏幕合成，
  输给前台竞争就是别人的壁纸/别的应用；④ 含中文注释的 ps1 若是 UTF-8 无 BOM
  + LF，PowerShell 5.1 按 GBK 读，行尾汉字的尾字节会把换行吞进注释、把
  下一行代码并进注释——报"意外的 }"，注释行尾保持 ASCII 或存成带 BOM/CRLF。
- **GUI 多窗口回归的记录器按相位分槽，断言吃进 DPI 与既定边距**
  （childwindow_shape，2026-09-23）：两个子窗口宿主并发存活时，
  `parent.PumpGuarded()` 会顺带泵到已显示的子宿主——用**静态字段**记录
  "本次排版结果"会被另一宿主的重绘回写（plain 阶段读到 shape 的值），
  记录器必须按窗口/相位分槽。断言坐标时注意三套坐标系：`ShapeOffX()`
  返回**已乘 DPI 的画布像素**、`Control.bx` 是设备像素、
  ChildWindow.Render 在内容原点外还有一圈随 DPI 缩放的 5 逻辑像素
  呼吸边距——期望值写成 `offX + Scale(5)`，别拿逻辑像素比设备像素
  （100% DPI 的机器上碰巧相等，150% 上必红）。
- **无头/脚本驱动 Zan GUI 程序，帧循环要自唤醒**：`app.ProcessEvent()`
  在消息队列空时会阻塞等消息——WebView2 创建完、页面稳定后没人发消息，
  帧循环就停在那里（窗口"未挂起"、消息循环活着，但帧计数器不走，
  超时断言永远不触发）。探针每帧末尾调一次 `app.RequestRedraw()` 保持
  状态机推进，否则看起来像"卡死在某一帧"，实际是没消息可等。
- **并发会话共用一块屏幕时，点击验证必须在同一次调用内闭环**：
  多个自动化会话都把 TOPMOST 窗口摆同一坐标（如 40,40），别人的合成
  点击会落在自己的窗口上——表现为"没人操作，日志里却在切页签/按按钮"，
  甚至窗口被陌生手关掉。不要把跨时间差的日志增量归因于自己的操作；
  每步验证 = 发点击 → 立刻截图，以截图里可见的状态（选中页签、勾选框）
  为准，且每次脚本调用前重查目标进程还活着。
- **模态泵与空闲冻结**：事件循环空闲时 driver 脚本会停摆，需要保活
  （真光标在窗口内小幅移动）；模态子窗口不在主命中表里，按窗口矩形
  比例坐标真实点击，且**以日志/输出面板确认动作生效**，不以"对话框关了"
  为准——对话框可能关了但动作没发生。
- **无头仿真 + 像素复核**：逻辑用无头仿真跑，断言写成探针
  （读内部状态或像素），禁止"截图看一眼"式的判断；相邻两次截图字节级相同
  要怀疑"根本没重绘"，先排查再下结论。
- **冻结测试钟只冻时钟，不冻样式补间**（2026-10-05 B-ID97）：`freeze clock`
  冻的是 `GetTickMs`，`AnimToIn` 的补间靠 `elapsed = nowMs - startMs` 推进——
  时钟冻结后 elapsed 恒定，补间永不落定，控件每帧重武装自己的条带帧
  （`RequestAnimationFrameIn`）永动。无头像素比对若撞上"条带帧 ↔ 加宽帧"
  交替（`zan_frame_perf.log` 里 `pframe present=<控件矩形> miss=1 ↔
  present=<面板矩形> miss=0` 无限振荡就是签名），相邻 dump 会差一档色调，
  且失败点逐次运行固定（相位被冻结钟锁死），极易误判成"确定性缺陷在渲染"。
  诊断套路：uidrv 脚本命令级二分（哪个命令序列触发）→ 逐命令 dump 像素
  → 帧日志找 miss 振荡，别从像素值反推渲染逻辑。
- **局部帧（损伤裁剪）下玻璃模糊重算的源约束**：卷积源是画布上的已合成
  像素，玻璃矩形只要跨出本帧损伤条带一步，就会把条带外上一帧的合成结果
  （色调、文字）混进模糊——同一区域"条带帧渲染 ≠ 整帧渲染"。判定必须是
  "矩形完整落在条带内才现场重算，否则欠账给还账帧"，"与条带相交"不够
  （控件条带帧几乎总是嵌在面板玻璃内部）；测试冻结钟的"强制重算"豁免
  同样要受这条约束，否则冻结钟反而制造不等价。
- **注入事件的 GUI 测试，BeginFrame 归 App 循环管，帧体只画内容**：
  `PumpGuarded + FrameGuarded(Body)` 的 Body 里不要再调 `app.BeginFrame()`——
  SafeFrame 已经调过，双重置会把 `inputBlockPrevious` 链打断，
  `BlockHitsBelow` 每帧误判"模态首帧"而自动认领点击（clickClaimed），
  浮层收不到任何释放、result 恒 -1，测试静默测了个空还不报错。逐帧手工
  推进时同理：一次 `BeginFrame() + 内容 + PresentFrame()` 才是一帧。
- **像素转场扫描必须带末段归属**：沿列/行打印"颜色转场"做像素断言时，
  最后一次转场之后的区间保持的是前一个色值——把"后面没有转场"误读成
  "后面没画东西"，会把已生效的绘制当成缺陷，白走一圈排查。转场列表
  只回答"哪里变了"，不回答"那里是什么"：区间归属要么显式扫到终点，
  要么改用定点采样（读目标区域内部代表点）做正断言。
- 慢、卡、冻结先看帧节奏（每帧每段耗时），再看初始化路径（惰性初始化
  未预热是首操作卡顿的头号根因），最后才怀疑渲染。
- **Windows 驱动脚本两条铁律**：PS 5.1 把无 BOM 的 .ps1 按 ANSI 读，
  自动化脚本只用 ASCII 字符或带 BOM，否则中文注释/参数被读成乱码直接
  解析失败；中文输入法激活时，SendKeys 直发的字母会被 IME 吞掉替换成
  组字结果——文本注入一律走剪贴板粘贴（写剪贴板 + 全选粘贴）。
- **长连接探针的写锁与谓词**：心跳线程与请求线程共用一条 socket 时，
  发送前必须共锁——两线程并发写会把帧互相嵌进对方字节流，表现为随机的
  回复错位；断言谓词用目标回复的独有形状（如"含 rows 数组"），宽谓词
  （"有 ok 字段即中"）会被心跳/广播回复误命中；每个操作前清空上一操作
  的待收队列。
- **多进程服务的停机探针（Worker/RunCommand）**：`<app> start` 恒为
  master + worker 子进程（单进程捷径仅非 CLI 的裸 RunAll），HTTP 服务跑在
  子进程；start master、worker、stop 客户端**都会执行 Main**。证据标记必须
  **按 pid 分文件**——共享一个标记文件会被三个进程互相覆盖（读到的永远是
  最后写者，信号错乱难排查），子进程 stdout 可能被重定向、Console 断言会
  静默丢失；钩子内先 `await Task.Delay` 再落标记，可证明"被等待"而非
  fire-and-forget。排查停机路径先想"自己关的连接被自己当 master 之死"
  竞态（如 ChannelLoop 对 handoff 通道 EOF 无条件 exit(0)，把整个排空
  等待短路——B-ID20 实测）。
- **外壳非客户区行为用真实消息 + 显式泵断言**：双击标题栏、SC_MAXIMIZE
  这类 WndProc 路径，PostMessage 到真实 hwnd 再显式 PumpGuarded，消息在
  泵内同步派发，无 sleep、无时序依赖。两条坑：NC 双击的 wParam 必须是
  WM_NCHITTEST 的 HTCAPTION(2)——传 0（HTERROR）DefWindowProc 静默忽略，
  "双击被挡住"的断言是空洞的假绿；RunLoop 的帧体只在 needsRedraw 时跑，
  静止窗口上永不点火——场景别挂在帧体上，用 worker 线程发消息
  （PostMessageW/IsZoomed 线程安全）+ `app.Post` 回 UI 线程收尾
  （Post 契约即线程安全）。
- **GUI 客户端×服务端联调的实机冒烟定式**：客户端留
  `ZAN_<APP>_AUTOLOGIN="user:pass"` 环境缝（登录窗独立泵起手前预填并
  直走登录流，GUI e2e 复用同一缝）；断言优先取**服务端可观测痕迹**而
  不是读屏——客户端行为落 DB 的状态列直接 sqlite 断言（如"打开会话"
  必推进已读水位，>0 即证明打开链路真发生了）；「实时消息并入屏」用
  前后两张窗口截图**逐字节 diff** 判重绘发生（PNG 确定性编码，内容同
  则字节同），等待窗口给足——WS 建连与秒级轮询节拍都是秒级窗，3s 级
  等待会假阴，6s 起拍、不等再兜底重拍；启动带 `ZAN_GUI_OVERLAP=1`+
  `ZAN_GUI_LAYOUTLINT=1`+`ZAN_GUI_OVERLAP_LOG=<file>`，收尾断言日志空
  （重叠/尺寸自检零命中）。截图脚本按 PID+窗口类枚举取最大可见窗，
  `$pid` 是 PowerShell 保留自动变量，参数名不能叫 `$Pid`。
- **后台窗口不 present：截图 diff 断言前必须点击激活**：数据并入（甚
  至下载落盘回调都跑完）后，非前台窗口的呈现面停在上一个交互帧——实
  测合并后 7s 截图仍逐字节同，一次合成点击立即出新帧，纯 SetCursorPos
  hover 无效。所以「重绘发生」类断言的**每张**截图前注入一次中性点击
  （点在无控件画布上，纯泵帧），并用 SetWindowPos 把窗口钉到屏内固定
  位置再拍（默认布局偶尔比屏高，出屏窗口捕获几何不稳）。
- **`Directory.ReadPath` 不是 cwd 解析器**：它对 cwd 里已存在的路径回
  空串（「程序随附资源回落」语义——磁盘上有就用原路径），永远产不出
  绝对路径；要取绝对 cwd 用 `Directory.GetCurrentDirectory()`（跨平
  台）。想用 ReadPath(".") 拿 cwd 的写法静默失效，极难察觉。
- **截图 diff 断言必须选「内容必然变化」的动作**：PNG 确定性编码下，
  「重拉同一份数据再渲染」的重绘字节与之前完全相同（重绘发生了但断言
  假阴）——重开同一个详情面板这类动作判不了重绘，改判开/关切换（收起
  必然少一块像素）或数据必然不同的两步。
- **Windows 控制台命令的 subprocess 捕获别用 `text=True`**：tasklist
  这类工具按系统码页（GBK）输出，UTF-8 reader 直接 UnicodeDecodeError
  崩线程；拿 bytes 自己按 `errors="replace"` 解码。
- **TLS（libssl/libcrypto）已无随库驱动**：stdlib 曾带的
  System/Security/Cryptography/drivers/ 目录已随 OpenSSL 驱动下线而删除，
  老资料"设 `ZAN_LIB_PATH` 指向该目录"的做法失效。`ZAN_LIB_PATH` 机制
  本身仍在（任意放导入库的目录，win 用 `;` 分隔，见 ZANC_CLI.md env 表）：
  需要真 TLS 时指向自备 OpenSSL 导入库目录，运行期把对应 DLL 摆到 exe 旁；
  Windows 下只携带不触发 TLS 的程序可直接链接——不可解析的非系统库会被
  自动桩化（调用时才报错），不再链接期失败。

- **GUI 测试合成驱动的“静止泵”永挂坑**（2026-09-30 chatview_bubble/transfer/
  chart_stackedarea_aa 三测试在安静机器上集体挂死，同一二进制早晨有人用机器时全绿，
  cdb attach 抓栈定位）：`App.ProcessEvent` 在无挂起重绘、无动画截止、无待处理事件
  时走阻塞 `window.WaitEvent()`（GetMessageW 无限等；空闲零 CPU 是产品正确语义，
  不要改成轮询）。测试帧与帧之间常处于静止，静止泵只能靠环境消息流（鼠标活动等）
  偶然喂活——机器一安静就必挂，且挂点随时序漂移，极难归因。定式：守卫帧体
  （FrameGuarded 的 body）末尾续订 `RequestAnimationFrame(16)`（spinner 的文档化
  模式，animNextMs 截止让泵走 ≤16ms 非阻塞等待），或泵前 `RequestRedraw()`
  （datatable 系同款）；纯数据突变后必须请求重绘再泵。诊断抓
  `USER32!GetMessageW` 栈一锤定音。

- **并行会话共享工作树时，编译验证用"冻结床"隔离在途噪声**（2026-09-30 定式）：
  别的会话正把某个包/stdlib 文件改到语法破损的中间态时，直接用仓库工具链编译
  会看到与己无关的解析错误假象。隔离法：`git archive HEAD packages stdlib |
  tar -x -C <scratch床>`，再拷入 build/zanc.exe 与其 zanrt_*.obj 兄弟 obj——包/
  stdlib 发现跟 zanc **二进制位置**走，床内 zanc 只见 HEAD 冻结包，与工作树在途
  编辑完全解耦；tar 报 linux 驱动符号链接失败在 Windows 无碍。**zanc 必须连带
  捆绑件**：`ld.exe`、`ld.lld.exe`、`mingw/`（dllcrt2.o 等）拷到 zanc 同目录
  ——zanc 只找自己旁边的 ld.exe，缺了就静默回退 PATH 上的 ld（GNU-ABI 链接满屏
  undefined reference，档案里符号明明都在）。适合"要在别人施工
  时量出自己改动的真实基线"的场景（撞名定性、A/B 对照），日常验证仍走工作树。

## 四、经验沉淀纪律（skill 的准入标准）

- 沉淀的是**通用可落地的规则**：动作 + 理由 + 适用边界。三样缺一的
  不进 skill：没有"动作"的是感想，没有"理由"的是迷信，没有"边界"的是口号。
- **不带流水账**：日期编号、"本轮/本机/这个 demo"、一次性排查过程、
  具体文件路径——一律不进 skill 正文。教训先问"换个项目还成立吗"，
  不成立就扔。
- 被推翻的旧规则在同一提交里删除，skill 里不留"可能也对"的僵尸条目。
- 单个 skill 控制在约 300 行内：逐坑细节、长流程挪 `references/` 子文件，
  正文只留决策规则；description ≤2 行、只写真实触发场景。
- 机器专属内容（本机路径/尺寸/环境限制）写进项目文档或注释，永不进 skill。

## 五、避免制造垃圾（开发过程中的克制）

- **不加没被要求的抽象**：接口/配置项/泛型参数只在出现第二个消费者时引入。
- **不写没人读的文档**：注释、README、说明段落服务于"下一个改代码的人"，
  营销词、空话、复述代码的段落删掉。
- **不留一次性文件**：探针脚本用完删，日志/截图不进源码树，临时方案
  不"先这样"——临时方案没有转化计划就会变成永久垃圾。
- **重复即信号**：同一段逻辑出现第二遍就必须收拢（基类/工具函数/常量），
  第三遍就是债。
- **依赖最小化**：新外部依赖要有不可替代的理由；能 stdlib 解决的
  不引入第三方。

## 六、技术债清理（平台升级后回头维护代码）

平台（编译器/runtime/stdlib）每前进一格，昨天的高效写法今天就可能是
旧写法。**代码不会自己跟上平台——不维护就是技术债。**

- **债从哪来**：编译器能力升级后（新内建、新语法糖、缺陷修复、性能契约
  变化），旧代码里为绕过缺陷而生的写法立刻变成债——手写循环对应新内建、
  平行数组对应新字典、降级封装对应已修语义、死分支对应已失效的条件。
  功能没坏，所以没人改它；它只会在下一次变更时让正确修改变贵。
- **发现即挂账，修毕即结单**：看到"绕过写法/旧形态"的代码，立刻记入任务
  台账（编号+一句话+根因+探针位置）；修完把条目移出未完成清单，压缩为
  一行摘要——挂账与结单的完整纪律见 `zan-workflow`。
- **顺手修的范围**：改动所在文件内的旧写法顺手更新（同一提交）；
  跨文件的成片旧写法开专项（列表→逐条修→跑受影响测试档→闭账），
  不允许"这次先不动"无挂账通过。
- **清债顺序按毒性**：① 会误导后续开发的（死分支、假封装、误导性命名）
  ② 阻碍修 bug 的（绕过写法与根因修复冲突）③ 纯风格旧化的。
- **禁止新债**：提交里引入绕过写法必须有台账条目和转化计划；
  "临时方案"没有转化计划就是永久债。
- **平台升级是清债触发器**：编译器发新版/新内建落地时，扫一遍
  对应旧写法的重灾区（grep 已知旧模式），成片的立专项，零散的顺手改。

## 七、评审自查（每次交付前过一遍）

1. 工作流闭环完整吗？（编译→运行→验证→测试档→提交，见 `zan-workflow`）
2. 有没有绕过编译器缺陷的写法？有没有该进基类/常量的重复？
3. 截图/断言锚定 PID 与窗口了吗？判断基于探针还是基于"看了一眼"？
4. 本次新踩的坑，值得沉淀吗？按第四节标准过一遍，不合格不写。
5. 有没有制造垃圾？（无用抽象、没人读的文档、一次性文件、重复逻辑）
6. 本次引入或发现的旧写法，挂账了吗？（见第六节）
7. 删了该删的吗？（临时探针、废弃产物、被推翻的规则）
