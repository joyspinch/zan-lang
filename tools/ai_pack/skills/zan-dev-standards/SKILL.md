---
name: zan-dev-standards
description: Zan 开发规范总纲——可落地的标准与纪律，供 AI 开发/维护/调试任何 Zan 项目时遵循。涵盖：开发工作流（先查 API 再动手、编译运行验证闭环）、代码规范（自绘禁令、组件纪律、注释纪律）、验证纪律（截图锚定 PID、探针断言、无头仿真）、经验沉淀纪律（教训须通用可落地、不带流水账）。用 Zan 写任何程序（GUI/游戏/服务端/工具）、改 stdlib/templates/examples、或做实机验证与调试时使用。
---

# Zan 开发规范总纲

定位：给 AI 的可落地标准。每条规则都是"动作 + 为什么"，不含项目流水。
领域细则：GUI 排版见 gui-design；游戏帧循环见 game-dev；服务端架构见
server-dev-standards；数据建模见 data-modeling；SQL 细则见 server-db-design；
版本提交/任务台账/会话卫生见 zan-workflow。

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

## 二、代码规范

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
- **杜绝手拼 JSON 字符串与手拼 SQL 拼接（严禁裸字符串拼接与简陋的 Replace 引号）**：
  ① JSON 序列化一律使用标准库 `System.Json.JsonValue` 构建器（`PutStr/PutInt/PutDouble/PutBool/PutNull/PutJson` 与 `Append`），字符串成员经原生转义安全处理特殊字符（引号、反斜杠、控制字符 `\n`/`\r`/`\t`），杜绝因换行或引号破坏 JSON 报文语法；嵌套片段利用 `PutJson`/`AppendJson` 原生嵌入，禁止通过文本剪裁与字符串拼接嵌入；
  ② 数据库非查询/查询/聚合操作一律使用参数化 API（`DbParams` 与 `?` 占位符），禁止手写拼接 `Quote` 单引号或裸拼接变量（如 `"WHERE slot=" + key`），杜绝 SQL 注入漏洞并复用数据库预编译执行计划。
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
  有描述的公开端点曾被类描述隐式默认刷进侧栏，而监控类控制器的非根动作
  上已标注的 `[Custom(IsMenu=true)]` 又被反向吞掉（2026-09-25 GenRoute
  改约定后两端同时修复，一致性用例锁定）。
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

## 三、验证纪律（实机/无头通用）

- **截图必须锚定进程 PID、按窗口抓取**，禁止全屏抓图后肉眼找窗口——
  全屏抓到的是最前面的任意窗口（编辑器/旧实例），拿错误窗口的像素做判断
  会得出"程序坏了"或"修好了"的假结论。先记录启动 PID，再按 PID 枚举顶层窗口
  逐个抓取；抓不到目标窗口就重试，不要让工具猜。
- **UI 驱动用合成事件，不用真实 OS 点击**：driver 的 `clickid` 在点击时刻
  解析命中区中心并注入，天然免疫窗口框偏移；hit id 只在**同一次构建的
  同一次运行内**有效（控件增删会整体移位），点击前当场 dump。
- **ZanWeb 模板实机核对九坑**（2026-09-25 ListPage/FormPage/视觉重绘验证，每条都白折腾过一轮）：
  ① 静态资产挂在 `/static/*`（`StaticFiles.Mount(app, "/static", "wwwroot")`），
  curl `/js/x.js` 拿到的是 API 层 `{"code":"404"}` JSON——不是"服务了旧文件"，
  先核对 URL 再怀疑缓存；② 改 wwwroot 的 JS/CSS 必须同步升布局模板
  里的 `?v=N`：资产响应带 `Cache-Control: max-age=3600`，浏览器缓存键含查询串，
  只 reload 页面拿不到新 JS（新代码 "确认加载" 要看执行中的函数源码或版本参数）；
  ③ `app.exe start` 会 daemon 出脱离启动任务的 worker，杀后台任务杀不掉它——
  重建 exe 报 `Permission denied`、旧进程继续占端口继续服务旧视图，须
  `app.exe stop` **并按 netstat 确认监听端口 PID 已清**（stop 不干净时链接
  照样 Permission denied）；④ config/views/wwwroot 按进程工作目录
  相对读取，起服务必须 cd 到发布目录，且 views 改动要重启才生效（视图缓存）；
  ⑤ e2e 与手动实机核对共用临时沙箱目录时，e2e 收尾会按自己的端口重写
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
  必须全仓 grep 旧路径——构建测试注册里写死的源文件绝对路径漏改，
  就报 `cannot open file` 白挂一轮才发现。
- **共享工作树上的测试归责：先隔离再定责**。测试结果异常先查
  并发提交时间线（`git log --format="%h %ad %s" -3`）：共享树另一会话
  在途编辑 stdlib/编译器期间跑测试，产物混进 WIP 源，无关测试假挂假绿
  （2026-09-25 GUI 拖拽 ctest 挂死 vs 手动快速 FAIL 二相性， targeted
  `git stash push -- <自己的文件>` 复跑一次即证明与己无关）。
  同产物"ctest 挂死、手动跑通"先手动复跑再怀疑代码。
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
- **外壳非客户区行为用真实消息 + 显式泵断言**：双击标题栏、SC_MAXIMIZE
  这类 WndProc 路径，PostMessage 到真实 hwnd 再显式 PumpGuarded，消息在
  泵内同步派发，无 sleep、无时序依赖。两条坑：NC 双击的 wParam 必须是
  WM_NCHITTEST 的 HTCAPTION(2)——传 0（HTERROR）DefWindowProc 静默忽略，
  "双击被挡住"的断言是空洞的假绿；RunLoop 的帧体只在 needsRedraw 时跑，
  静止窗口上永不点火——场景别挂在帧体上，用 worker 线程发消息
  （PostMessageW/IsZoomed 线程安全）+ app.Post 回 UI 线程收尾
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
- **客户端程序链接报 `cannot find -lssl/-lcrypto`**：普通链接（非
  `--publish`）要求 TLS 导入库在搜索路径可达，报错不提示解法——设
  `ZAN_LIB_PATH` 指向 stdlib 的 System/Security/Cryptography/drivers/
  <plat>/（win 用 `;` 分隔，见 ZANC_CLI.md env 表）；运行期还要把
  libssl-3-x64.dll/libcrypto-3-x64.dll 摆到 exe 旁。

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
  一行摘要——挂账与结单的完整纪律见 zan-workflow。
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
