---
name: zan-designer-components
description: Zan 窗口设计器用户组件（components/*.zcomp）的全链路定式——引用节点（{"kind":X,"ref":X}）、对外 props 声明（{"props":[{key,label,kind,target,prop,def}]}）、实例值直通表、GenForm 展开、设计器预览、运行期 UserComponentRegistry、双击进组件编辑；附 stdlib/Json 与 .zform 的尖锐边（Put/Set 语义、role window vs control 的 static/instance 字段、ZanGen 缓存陈旧）与并行会话下 CMakeLists 局部暂存技巧。做或改设计器组件、.zcomp/.zform 管线、GenForm 生成器、UserComponentRegistry、或踩到 JsonValue Put/Get、多实例叠前缀名、role:window static 字段报错时使用。
---

# Zan 设计器用户组件全链路

> 提炼自三期组件化改造（引用化 924e1566 / props 全链路 a7b0ead5 /
> 双击编辑 48d3f130 / BatchJobProgress dogfood 1218aa2d /
> 运行期注册表 c1adb56a）。每条都是踩坑后验证过的事实。

## 架构：一份组件文档，三个消费方，一种属性语义

- .zcomp 根可带 `"props":[{key,label,kind,target,prop,def}]`（v1 只消费
  text 型）：target = 组件内部控件名，prop = 控件 SetProp 认的键。
- 实例值存引用节点 extra 的 `"props"` 直通对象（键 = 声明 key）。
  `"props"` 不在 IsModeledKey 里，SaveJson/LoadJson 原样透传——不要
  给它另建模型字段。
- 三个消费方必须同语义（实例值优先、缺省取 def、落到 target）：
  GenForm 展开（编译期）、Designer 预览 PvCompCtl、
  UserComponentRegistry.Build（运行期）。预览与运行期已合到
  `UserComponentRegistry.ApplyProps` 一个入口，改语义只改这里和
  GenForm 两处。

## 展开（GenForm.ExpandRefs）的坑

- **组件文档缓存是共享的，展开会原地改写**：第一个实例的
  壳名前缀、dock、props 合并都写进 CompDoc 缓存。不 DeepCopy 就会
  出现第二个实例拿到 `Card2_Card1_InnerBtn` 叠前缀名。展开前
  必须 `GenForm.DeepCopy(cf)`（标量节点不可变可共享）。
- 壳上消费过的实例 props 必须清空（Set 成空对象）：EmitField
  会把字段级 props 逐键 `SetProp` 到壳上，Panel 不认的键会
  退化成样式类（SetProp 的 fallback 是 AddClass）。
- 改已有键必须用 `Set`：**JsonValue.Put 是追加不去重，Get 只认
  第一个**——曾导致 kind 改了不生效而新键全生效的灵异 bug。
  新建对象上的首次写才用 Put。
- 目标控件缺失时报指名错误（`component 'X' prop 'Y' targets
  missing control 'Z'`），不要静默跳过——那是组件作者的错。

## .zform / 生成代码的尖锐边

- **KindForType 是三头共用的唯一真相，改它必须三处对账**（2026-09-09
  "两个 datagrid" 实证）：FormField.TypeName（调色板显示名）、
  KindForType（ftype→控件类，决定 .zform 的 `kind` 写什么、GenForm 生成
  `new <kind>()` 声明什么类型）、TypeForKey（kind→ftype 逆映射）。
  23 表格/41 数据表格曾错位——23 返 `"Table"`（只是画布静态渲染器，
  不是 Control）、41 缺席回落 `"Input"`：数据表格放下保存再生成变成
  输入框；手写 `kind:"DataGrid"` 反查不到被当自定义组件；旧文档
  `new Table()` 编不过。铁律：**KindForType 每个返回值必须是真实可
  实例化的 Control 类**；两者共用一个控件类时（23/41→DataGrid），
  TypeForKey 给显式别名（`DataGrid/DataTable→41`）避免逆映射撞扫出
  另一头。GenForm.TypeOf 里对旧 kind 别名归一（`Table/DataTable→
  DataGrid`）保护存量文档。
- **role 决定字段形态**：role:control 生成实例字段（`new X()` 后
  `app.Field1`）；role:window 生成 **static** 字段，实例访问直接编译
  错（`'Job1' is a static field... qualify with the type name`）。
  无头测试一律用 role:control；入口文档必须是 window 才能用 window。

## 画布交互（拖动/嵌套/撤销）的定式（2026-09-09 交互面修复实证）

- **Undo/Redo 恢复快照必须走 `ApplyJson`，绝不能走 `LoadJson`**：
  LoadJson 尾部会 `ResetHistoryLocked()` 清空两个历史栈（那是给
  "打开新文档"用的）——Undo 里调它，Ctrl+Z 只能退一步、Ctrl+Y
  永远落空（探针实证：三次 Add 后第二次 Undo 直接"没有可撤销的
  操作"）。快照恢复和文档装入是两个语义，入口必须分开。
- **自由画布子级 fx/fy 是「父内容框相对」坐标，不是画布绝对**：
  渲染/命中都按 `父链原点 + KidInsetX/Y`（自由模式 0，Tabs 6/26，
  流式 6/26）累加。任何"选中项变了"的路径（框选、撤销、换父、
  代码置 sel）之后都必须 `SyncFreeSelBase()` 重算拖拽基
  freeSelPX/PY——基过去只在 FreePick 直接命中时顺带写入，框选
  一个嵌套子级再拖，组件按画布原点算位置直接"瞬移"出容器。
- **FreePick 的副作用会写 freeSelPX/PY**：同帧里先做光标探测
  （落点高亮 FreeDropContainer）再读基，读到的是被探测改写过的
  值。拖拽/缩放移动块里必须每帧 `AbsOriginX/Y(sel)` 按父链现算，
  不信字段。
- **自由画布换父 = `FreeReparentSel(cont)`**：摘除→设 childTab
  （Tabs/SplitPanel 进正在看的页）→挂入→fx/fy 按
  `AbsOrigin(旧) - ContOrigin(新)` 换算保持视觉位置不动（换算
  在函数内部自算，不依赖调用方先跑 FreeDropContainer）。
  InSubtree(sel, cont) 拒绝拖进自己的子树。悬停高亮用
  DrawFreeDropHi 画容器描边预告落点。
- **多选（Ctrl）允许容器+子级同选**：移动时子级若其祖先也在
  选区（sel 或 freeExtra）里必须跳过——两组各加一次增量会把
  子级移两次。框选路径用 HasAncestorIn 预先去重，Ctrl 点选路径
  只能在移动时兜底。

- 引用展开后内部控件不生成具名声明（匿名），运行期只能
  `shell.Find("壳名_内部名")` 触达。Label 的 text 属性 syncName：
  SetDesignText 会把控件改名为标题，Find 要用 caption 而不是设计名。
- Badge/Panel 等没有 `text` 属性，GetProp("text") 返回 ""；断言文本
  时选有 PropSpec text 的控件（Label/Button）。Progress 有数值型
  `percent` prop，SetProp/GetProp 走字符串形式（"30"）。

## 控件 Props() 补属性的定式（2026-09 审计实证）

- **普通字段直赋即实时绑定**：`spec.num = someIntField`（编译器合成
  访问器对，双向读写控件字段）。Progress.fillColor、ChoiceGroup.columns
  都是这么写的且已过测试；**不要信「设计期常量走快照 Binding 写不回」
  的旧注释**（InputNumber 那条是过时顾虑，已订正）——担心写不回就写
  往返探针（SetProp→GetProp）实证，别凭注释下结论。
- **枚举属性（PropSpec kind 3）SetProp 收选项名也收序号**：Write 先按
  `options` 精确匹配选项文本、命中就把下标写进 num/snum 绑定（绑定文本
  时存选项名本身），没命中才按整数解析。曾经只认数字序号，
  `SetProp("orient","vertical")` 经 strtoll 静默得 0、无声落回第一项
  （2026-09 Tabs 方向进设计器时探针实证后修复）。.zform 里枚举值存选项名
  （如 "vertical"）可读性最好，旧数字写法仍被容忍。
- **SetProp 管线是 PropOf 先于 SetExtra，快照 spec 会挡死真写入**：
  Image 曾同时有 Props() 里 `fit.num = FitIndex()`（方法返回值=游离
  快照）和 SetExtra("fit") 真写入——PropOf 命中快照 spec 即 return，
  SetExtra 永不可达，SetProp("fit",…) 静默无效。正解是 spec 直接绑
  字段（`fit.str = Fit`，枚举选项文本即取值），别给同一键开两条写入
  通路（2026-09 内置组件 props 直通时探针实证）。
- **内置组件的直通属性行**（Image/Countdown/NumberAnimation/InputOtp
  已接）：键值存 f.extra["props"]（与引用节点实例值同一张直通表，
  "props" 不在 IsModeledKey），GenForm 泛化发射 SetProp、画布预览经
  ApplyFieldProps 逐键落真控件；运行态开关（Countdown.active 等）不进
  设计器行。枚举键直接填选项名（PropSpec.Write 认名字）。
- **立即模式组件进设计器必须先有保留式包装**：ChartView 这类
  "宿主逐帧 Render" 的件不是 Control，控件树/生成代码无法持有——
  GenForm 只能归一成占位，画布预览再漂亮发布也消失（Chart 条目
  曾因此发布即蒸发）。正解是补一层薄包装（ChartHost：持有
  ChartOption+ChartView，OnPaint 转发 Render，SetProp("type") 切型）+
  SampleOf 示例数据工厂，设计器画布与生成窗体共用同一份示例，
  所见即初见；类型/标题走直通行，零新增发射逻辑。
- .zform 建模键 "orient"（Tabs 标签条方向，已入 IsModeledKey）：
  FieldJson 写 "horizontal"/"vertical"，GenForm 仅在 vertical 时发射
  `SetProp("orient","vertical")`；画布 KidInsetX/Y 与 RenderTabStrip 按
  `f.tabOrient` 换轴（纵排轨道宽 160 = 运行时 trackW 未缩放宽）。
- **class 不在控件 Props() 里声明（2026-09-13 起，39 处声明已删）**：
  class 是基类契约——GetProp/SetProp 对 "class" 短路应答、序列化有
  专用尾部字段（WriteNode/ReadNode）、.html 设计文档走 f.Class modeled
  key；per-prop 声明在所有消费点都是死行（gallery WatchTree 跳过
  !IsBound()，Designer Inspector 吃 FieldSpecs 不吃控件 Props()）。
  zform.json 的 class 条目由 ZformSchema.ReadControl 单点注入（78/78
  控件全覆盖），别再往控件里加回来。同理 "name"（基类特判，也从无
  声明）。
- Zan **没有 C# 式跨行相邻字符串隐式串接**，`"a"\n"b"` 直接报
  expected ')', got STRING_LIT——换行拼接必须显式 `+`。
- 颜色属性（PropSpec.Color，kind 4）的文档串形式就是 ARGB 整数十进制
  （Read/Write 走 Convert.ToString/ToInt32），探针往返用 "-65536" 这类。
- **Step(s, lo, hi) 一律置 hasRange**：hi=0 就是「上限钳到 0」，不是
  「无上限」——只配步进不配范围必须用 `StepBy(s)`（实测：step 属性挂
  Step(1,0,0) 会把步进值钳死成 0）。
- **派生键（文本是结构状态的视图：order/circles/options/fx）的三件套
  定式**：① Props() 里 spec 绑**表达式快照**（`spec.str = this.XxxText()`）
  供面板显示（PropertyGrid 每次 Bind 重建 specs，快照即新）；② SetProp
  覆写把写路径截到重建入口（快照绑定 `p.Write` 写的是临时值，直写=
  静默丢失——Pagination pageSizes 曾因此设计器写入全丢）；③ GetProp
  覆写派生应答（spec 命中即短路 GetExtra，裸 spec 的 Read() 恒 ""）。
  可重复写入的列表键 SetProp 里先 ClearOptions/重建（面板是反复编辑，
  追加语义只属于构造期直调）。
- **PropertyGrid 的写路径只走 `p.Write`（spec 绑定），不经控件的
  SetProp**：字段绑定（含 live pair）面板直写即生效；快照绑定面板只
  更新编辑器残值，正式应用靠宿主听 Change 后走 §6.1 SetProp——两种
  绑定写测试时都要覆盖（SetProp→GetProp 往返 + 重取 Props() 快照
  断言模型真变了）。
- 新增属性的最小验证：实例化→SetProp→GetProp 往返 +（有访问器时）
  断言公开 getter，一处探针覆盖九组件 24 断言即可全绿提交。

## 生成器与构建链

- **ZanGen 缓存按内容寻址，只有 zanc 跑设计管线才会重建**。
  直接调 `ZanGen_<hash>.exe` 做探针时若刚改过 GenForm.zan，先跑一次
  `zanc ... --auto-stdlib` 触发重建，否则在验证陈旧行为。多个
  缓存 exe 共存时按文件名排序不一定拿到最新。
- 驱动生成器：请求 JSON `{mode:"design", files:[{name,text,emitMain}],
  components:[{name,text}]}`，两个参数 = 入/出 json 路径。.zcomp 只进
  components，永远不被当作 Zan 源解析。
- 改 GenForm.zan 后 policy_zform_schema 会扫所有 ObjStr/ObjNum/ObjBool/
  .Get 读的键：新键必须写进 tools/mcp_server/zform.doc.json（反向也
  查：文档了但 GenForm 不再读→把读法改成 ObjStr 这种可扫描形式
  或移进 note）；生成器请求区的键（files/components）在测试的
  allowed_region_keys 白名单。设计器私有键（如 pvOff）走 UnmodeledKeys
  透传，写进 note，不建 key 条目。

## 并行会话下的工程动作

- CMakeLists.txt 被并行会话占着时，不要整文件 add：先改工作树，
  再用 `git show HEAD:file` 重构"只含自己 hunk"的版本，
  `git diff --no-index` 生成补丁后 sed 修正路径头，
  `git apply --cached` 只把自己的 hunk 进索引。手写 hunk 头容易
  corrupt，重构法稳。
- 验证阶段直接手跑受影响的 conformance 子集
  （`ctest -R "gui_compref|gui_batchjob|..."`），不跑全量 ctest。

## 测试地图

- tests/gui/compref_test.zan：编译期展开端到端（多实例/实例覆盖/
  默认回落）。
- tests/gui/compref_designer_test.zan：无头 Designer 验收（落节点/往返/
  预览/解包/双击请求/注册表）。
- tests/gui/batchjob_test.zan：dogfood：实例 props + 运行期 Find+SetProp
  驱动进度（批量任务进度窗工作流）。
