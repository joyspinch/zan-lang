# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：[ ] 未完成 · [~] 进行中 · [-] 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 [x] 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出，已闭环内容移至 git 历史。

## 未完成

### [~] A383 图表引擎整代语义差：官方 470 个 option 键里 278 个（59%）从不读取

- **第一步已落地（2026-10-24）：未知 option 键告警进引擎。**
  `ChartOptionWarn.WarnUnknown`（Models/ChartOptionWarn.zan）挂在
  `ChartOption.FromJsonValue` 顶部，解析前走树对账：注册表 = 引擎源码
  访问器字面量（.Get/.Int/.Str/.Bool/.Num/.Double/.Has）读到的 368 个键名
  （与 `scripts/chart_gap_audit.py` 的 read 集同源同 blind spot）+ GeoJson
  的 UTF8Encoding/UTF8Scale + 数据载荷子树键；载荷子树（data/children/
  nodes/…）不下钻。注册表外的键 = 静默丢弃，`Console.ErrorWriteLine`
  打到 stderr（同名键去重只报一次，不污染 stdout 黄金）。
  - 配套内建：`Console.ErrorWriteLine(string)`——builtin_api.c 成员表 +
    irgen_call.c 发射（收窄成 string 单签名，非串实参当场报错）+
    `zan_rt_eprint_line`（rt_io.c；wasm 运行时集不含此符号，wasm 侧
    待需要时补）。
  - 生成坑（已修）：本环境 ugrep 对 CRLF 源文件 `-o` 捕获会把行尾 \r
    粘进每个键——首版注册表 368 个键全是 `key\r`，除 echo 追加的载荷键
    外全部误报；生成管道必须 `tr -d '\r'`。
  - 验证：探针（title.text 已知静默、title.shou/axsiX/series.wat 各报
    一条、stdout 干净）；图表 conformance 全量回归见同批提交。
- **第二步已落地（2026-10-25）：高频 option 键下沉解析，未读率降至 36%。**
  补齐 `animation` / `animationDuration` / `animationDurationUpdate` /
  `animationEasing` / `animationEasingUpdate` / `animationThreshold` /
  `readOnly` / `sublink` / `realtime` / `shadowOffsetX` / `shadowOffsetY` /
  `textAlign` / `verticalAlign` 等 14 个高频配置键解析与结构化存储；
  未读键数从 183（39%）降至 169（36%）；ChartOptionWarn 注册表同步刷新。
- **第三步已落地（2026-10-25）：批量补齐高频未读 option 键，未读率降至 19%（91 个）。**
  针对 `scripts/chart_gap_audit.py` 识别出的广泛未读键实施系统化补齐：
  1. 渐变反序列化盲点：`__grad`, `x1`, `y1`, `x2`, `y2`；
  2. 仪表盘与数值动画：`pointer.icon`, `anchor.icon`, `detail.valueAnimation`, `label.valueAnimation`, `label.minMargin`；
  3. 地理与交互缩放：`aspectScale`, `zoom`, `layoutCenter`, `layoutSize`, `filterMode`, `zoomOnMouseWheel`, `moveOnMouseMove`, `moveOnMouseWheel`, `throttle`；
  4. 动画与样式：`bounding`, `loop`, `delay`, `easing`, `keyframes`, `percent`, `scaleX`, `scaleY`, `lineDash`, `lineDashOffset`；
  5. 轴系与断轴：`gridId`, `axisPointer.snap`, `axisPointer.handle`, `breakArea.zigzagZ`, `zigzagMaxSpan`, `breakLabelLayout`, `showMinLabel`, `showMaxLabel`；
  6. 组件级与系列控制：`tooltip`（`confine`, `transitionDuration`, `showDelay`, `hideDelay`）、`toolbox`（`feature.mark`）、`legend`（`itemGap`, `itemWidth`, `itemHeight`, `icon`）、`useUTC`, `blendMode`, `bmap`, `brush`（`toolbox`, `xAxisIndex`, `brushLink`, `outOfBrush`, `inBrush`）、`singleAxis`, `visualMap`（`outOfRange`, `controller`）、`series`（`universalTransition`, `effect`, `rippleEffect`, `leaves`, `progressive`, `progressiveThreshold`, `edgeSymbolSize`, `avoidLabelOverlap`, `clip`, `sampling`, `realtimeSort`, `legendHoverLink`, `nodeAlign`, `singleAxisIndex`, `polyline`, `showEffectOn`）；
  7. 同步更新 `ChartOptionWarn.zan` 注册表；未读键从 169 个（36%）降至 91 个（19%），影响面 >3 文件的所有高频键全部清零。测试全绿无回归。
- 事实（`docs/archive/CHART_CODE_GAP_LEDGER.md`）：对照 ECharts 6.1 的 596 个
  `.ts` 源，官方 option 用到的 470 个配置键里，引擎**从不读取**的有 278 个（59%）。
- 表现：用户照官方示例写 option，键被静默丢弃——不报错、不告警，图形少一块也
  不知道少了哪一块。
- 方向：先补"未知 option 键"的告警（廉价，立刻让缺口可见），再按官方示例
  覆盖度逐个补实现；不要一次铺 278 个键。

### [x] A453 类型参数按名字全局注册——同名 `T` 先到先得 —— 定性：按名字统一是有意架构（闭账）

- 2026-10-26 静态审计收口（两轮探针无一复现之后，回答"为什么不可能错"）：
  全部 `TYPE_TYPE_PARAM` 消费方都按**名字 memcmp / kind / arity** 匹配，
  **没有任何一处比对对象指针**——`binder_type_params_equiv` 只比 arity；
  `checker_unify_method_tp` / `type_mentions_tp_checker` / `checker_type_equal`
  按名字；irgen 的 `di_subst_param` 按名字对照实例化类的形参表；`==` 比较走
  `checker_type_equal`；`ext_receiver_match` 按 kind。所以根作用域"先到先得"
  留下**谁的对象**在行为上不可观测：同名即同义，文件解析顺序不改变任何结果。
- 根作用域注册是**承重墙**，不是缺陷：checker 全程不设置 `current_scope`
  （checker.c 里零处赋值），`resolve_type` 的 `rt_scope` 缓存守卫
  （`rt_scope == current_scope` 才命中缓存）令 checking 阶段方法体内新出现的
  类型引用（局部变量、`new`、调用实参）全部落回**根作用域**解析——没有这份
  注册，泛型方法体一律 E1005。台账原方向"按层隔离注册"需要给 checker 全程
  作用域感知，而同名类型参数在按名字统一架构下语义本就相同，隔离的**行为
  收益为零**、回归风险大 → 方向作废。
- 动态钉形（2026-10-26）：五个探针全绿——跨类同名类级 `T`（含 `T[]` 字段与
  构造实参）、类级+方法级同名 `T`（方法实参定型）、同类两个方法各声明 `T`、
  基类子句用类级 `T`（`class MyList<T> : Store<T>`，resolve_bases 在根作用域
  下解析基类实参）、本地函数泛型（`T Conv<T>(T x)` 正常）；跨文件主文件先/
  后解析双序输出一致。钉形入库 `tests/conformance/type_param_same_name`
  （+ det 双档）通过；`binder.c register_type_params` 顶部留架构注释防未来
  "按层隔离"或拆根注册的重构。
- 关联：A448（真因不在这里，但同族）、A418、A551（内建同名顶掉，另一回事）。

### [x] A454 真装箱：值类型 → `object` 的堆对象（v1 闭环：显式 cast + is + 拆箱）

- **定性：已闭环（2026-10-26）**。无托管字段的结构体支持 `(object)s` 装箱、
  `o is S` 判定、`(S)o` 显式拆箱；错型拆箱走运行期检查软失败（try 内可捕获，
  捕获后 EH 状态完好、原有箱继续可用）；`--check-leaks` 干净；一万次装箱
  拆箱循环正确（box_stress：0+1+…+9999=49995000）。
- 实现（约定对齐 A449 探明路径）：
  - checker：`checker_struct_boxable`（无 managed 字段、非 explicit layout、
    递归校验子结构体）放行 `cast_is_valid` 的 OBJECT 分支；含托管字段的
    结构体维持 E2070（证据链：box_managed 探针）。
  - irgen：`emit_box_struct`（`rt_alloc(size_of_payload, desc_site, name)`
    + 载荷顶格写入 + 托管来源 retain）、`emit_unbox_struct`（`is` 校验 →
    载荷 load → 失败 `emit_runtime_check` 报
    "InvalidCastException: object is not an instance of the target struct type"
    并置零继续）、`emit_runtime_is_check_name` 增加 struct desc 分支。
  - ARC：装箱结构体也走 `get_class_release_decl` 壳——
    `emit_all_class_releases` 补第三循环，为非托管非泛型结构体 site 补
    释放函数体（标量 → 空体，只剩 rc+free），消除 verify 期
    "release 函数有声明无定义"。
- **顺带修掉一个潜伏 EH 缺陷（box 探针炸出来的，规则 10 修根因）**：
  A528 的结构体 ctor 裸武装把 **EH 临时栈深度** 存进 armed 槽，共享 disarm
  却写回 **EH top**——ctor 正常返回一次就把 `top=-1` 冲成 `0`，此后第一个
  失败的运行期检查误判"在 try 里"，`longjmp` 进从未 setjmp 的 `bufs[0]`
  （全零 jmp_buf → rip=0 段错误）。此前未炸只是因为典型程序在第一个失败
  检查前总跑过 EH 相关代码。修复：armed 条目加 `restore_tmps` 标志，
  disarm 按类别写回各自的栈顶（irgen.h / irgen_emit.c / irgen_stmt.c）。
  回归：`case_arc_throw_struct_field`（A528 本尊）仍绿。
- v1 边界（挂账见 A556）：隐式装箱（赋值/传参自动装）、装箱值的虚方法
  分派（ToString/Equals 重写）、反射/序列化对装箱值的处理——均不在本期；
  含托管字段的结构体装箱需先给装箱对象挂"字段释放"析构（当前空体仅
  适用于标量）。
- 验证：`tests/conformance/struct_boxing.zan`（box→is→unbox→错型 try 捕获
  →抛后 EH 完好→循环 5050），`case_struct_boxing` + `case_det_struct_boxing`
  双绿；探针族（box_probe/stress/min/c4/b5/c1/managed/cls）全过。
- 关联：A449、A434、A556。

### [x] A556 装箱 v2：隐式装箱、装箱值方法分派（v1 边界的剩余两件）

- ~~含托管字段的结构体装箱~~ **已闭环（2026-10-26，A556-3）**：checker
  `checker_struct_boxable` 放开托管字段（仅保留 TYPE_TYPE_PARAM 拒绝），
  irgen 资格同步放宽；装箱按来源 owned 性决定字段级 retain（构造临时转移、
  既有变量 `emit_collection_value_retain`），箱的释放函数补嵌套结构体字段
  下钻（`build_class_release_body` 加 TYPE_STRUCT 分支，复用
  `emit_collection_value_release`）；拆箱为 C# 同款拷贝语义（目的捕获协议
  retain，箱保有自己的份）。关键配套修复：`expr_yields_owned_rc_value`
  对装箱 cast（object 目标 + struct 操作数）返回 owned——箱是 fresh 堆
  对象，消费方（object 变量/字段捕获）不得再对箱指针 retain（否则 rc=2、
  作用域释放只减到 1，箱泄漏）；`emit_box_struct` 的字段 retain 改判
  **操作数**的 owned 性。验证：`struct_boxing_managed`（+det 孪生）双绿，
  探针 t2/t6/m6 leakcheck 干净。
- ~~隐式装箱~~ **已闭环（2026-10-26，A556-1）**：checker
  `checker_type_assignable` 的 TYPE_OBJECT 分支放行 boxable 结构体；
  irgen 新增 `emit_implicit_box`（目标 object/接口 + 值形状 + boxable 才
  落箱），挂在六类流动点：赋值分派器、声明初始化三路径（ptr-class/var/
  通用 arc_own）、调用实参（emit_arg_typed）、集合元素槽
  （emit_collection_slot_store——List.Add/索引器/Dict 内联快路径不经
  emit_arg_typed，必须收在这）、字段写入（emit_rc_store_field）、普通与
  异步返回（async 路径箱后 ret_type 收敛为声明类型）。配套所有权协议：
  装箱节点登记进 `zan_irgen_t::boxed_exprs`，`expr_yields_owned_rc_value`
  查表判 owned（箱 fresh，消费方不得再 retain）；装箱实参是堆临时——
  `emit_release_owned_call_temp` 在 local-ident 早退**之前**查表并按
  object 释放（被调方借用不释放参数）。
- 箱布局升级（分派基座）：**首字 = 结构体 vtable 全局地址，载荷 +8**。
  接口分派按接收者首字做身份 tag 比对，箱此前没有这个字（tag 是载荷前
  8 字节当指针，永远命中不了候选）；tag 匹配后结构体方法的 this 指向
  载荷（+8），接口分派单候选与链式两处都做了 SYM_STRUCT 校正。
  `is`/拆箱 tag 走描述符名表（头部），不受载荷位移影响；拆箱读载荷改
  +8，箱释放函数对 SYM_STRUCT 先 GEP 8 再按结构体布局下钻。
  **修复了潜伏段错误**：checker 早已放行 struct→接口（derives_from），
  但 irgen 把聚合值裸塞进指针槽、接口调用解引用垃圾——
  `IShape f = s; f.Area()` 直接 crash（探针 i2 复现→修复后输出 9）。
- 验证：`struct_boxing_implicit` 新用例（object/接口变量 + 接口分派 +
  传参 + 返回 + 字段 + object[] 元素 + List<object>.Add + is + 拆箱，
  含托管字段结构体）+det 孪生双绿；async 族 111 例全绿；
  struct_boxing/struct_boxing_managed 孪生在新布局下依旧双绿；
  13 个 `_scratch` 探针（含 arg/slot/field/array/async 返回逐语句二分）
  leakcheck 全净。
- 边界（记录）：整数/枚举/decimal 装箱仍拒（沿用 A454 v1 口径，检查器
  报错不腐化）；三元无法把 struct 分支与 object 分支统一（E2063 明确
  报错）；`object.ToString()`/`Equals` 方法族**对任何接收者都还不存在**
  （纯类对象 `a.ToString()` 也 E3068，与装箱无关）——立 A558。

### [ ] A558 System.Object 方法族：`object` 上无任何可调用成员——ToString/Equals/GetHashCode 连类接收者都绑不上

- 现象（2026-10-26，A556 装箱 v2 收尾时实测）：`object a = new Q();
  a.ToString()` → **E3068**（no method matches）——`object` 类型的成员
  访问没有任何解析目标，纯类接收者同样报错，与装箱无关。
- 影响：C# 的 object 作为通用基底的体验（打印、相等比较、哈希）在 Zan
  不可用；接口分派所需的身份 tag 机制已就绪（箱首字 = vtable，A556），
  缺的是 object 虚方法槽位的定义与解析。
- 方向（动手前先定口径）：Object 内建方法族（ToString/Equals/
  GetHashCode/GetType）作为 TYPE_OBJECT 的成员表进入 checker 解析；
  类的 override 按既有 vtable 槽位机制分发（get_virtual_method_index）；
  箱按 A556 的 vptr 基座参与同一分派；结构体未重写时回落 object 缺省
  （ToString 返回类型名，C# 同款）。注意与 A443 的内建 Convert.ToString
  特判让路口径一致（精确匹配让路，其余形状走内建）。

### [x] A557 object 局部变量赋值不释放旧值（含 `= null`）——连带守卫异常泄漏

- 现象（2026-10-26，A556-3 装箱探针的 leakcheck 炸出来的，**与装箱无关，
  纯类对象即复现**）：
  - `object a = new List<int>(); a = null;` → 泄漏 List（t3）；
  - `object a = new List<int>(); object c = new List<int>(); a = c;` →
    泄漏 a 的旧 List（t8），且 a、c 共享 rc=1（assign 未 retain，作用域
    退出两次 release_dyn，靠运行期 rc≤0 守卫兜住）；
  - `object a = <箱或对象>; a = null;` 之后首个 try/catch → 被捕获的
    守卫异常也泄漏 1 个（m7；无 `a = null` 的同构 g2/g3 干净）。
- 根因（诊断打印 + IR 定案）：赋值分派器（irgen_expr.c
  emit_expr_assignment）里 `local_is_dyn_obj`（kind==TYPE_OBJECT）**优先
  命中 emit_obj_local_store**，但普通（非 async、非闭包）object 局部量
  没有 obj_rc_flag——该函数对它整个退化成裸 store：`owns=0`
  （obj_slot_owns_value 不认 TYPE_OBJECT、标识符 RHS 不算 owned）→ 不
  retain；`emit_release_obj_local` 无档案直接 return → 旧值不释放。而
  obj 机器对普通局部量**从不建档**（建档只在 emit_obj_local_store 内
  owns||flag 时发生），所以这条抢占从未产生过正确行为。声明侧走
  emit_rc_capture_local（arc.old + obj.probe/srel/orel 释放链）+
  arc_own_local 压槽，语义本就正确；IR 里的 `tmp_push(slot)` 是声明期
  arc_own_local 的槽注册，与赋值语句无关。
- 修复：分派器给 obj 路径加 `local->obj_rc_flag` 门禁——异步帧/闭包槽
  （所有权运行期可变）照旧走动态派发；普通 object 局部量回落
  `emit_rc_capture_local`（retain 新值 + 释放旧值，与类/委托/数组/字符串
  局部量同一条编译期定权路径）。
- 验证：t3/t8/m7 带 `--check-leaks` 全净（m7 被捕获守卫异常的泄漏随赋值
  修复一并消失，与"修复方向"预判一致）；`scripts/test.ps1 standard
  -Match "arc_|boxing|object_|cast_string|throw|catch|eh_"` 45/45 全绿。

### [x] A489 GPU 3D 片元着色器把光照写死成固定键光——没有材质与光源 uniform 通路

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
- 闭环（2026-10-26）：四份片元着色器共享宏拼装的 uniform 光照段——
  `uLightDir[4]` / `uLightColor[4]` / `uLightCount` / `uAmbient`，环境光 +
  至多 4 盏方向光半兰伯特累加；循环上界写常量 4、体内 break（GLSL ES 3.00
  循环条件要求常量表达式）。状态在 CPU 侧（`g_game_light_*`），缺省 = 键光
  dir(0.4,0.8,0.6) 贡献 0.45 + ambient 0.55，与历史固定光照**逐位一致**。
  新导出 `zan_game_set_lights3d(count, dirs, colors, ambient)`；uniform 按全局
  版本号在每个程序上惰性上传（程序间不共享 uniform 状态，批量通路中途
  `set_lights3d` 下一次提交即生效——批次每实例 20 float 的约束下光源走
  uniform，与方向预判一致）。`zan_gl_api` 补 `Uniform3fv`（唯一加载表
  gui_gl_context.c，GL 3.3 / GLES 3.0 均必有）。Zan 面：`Canvas.SetLights3D`
  （持久 NativeMemory 缓冲封送，null ambient 保留当前值）。Render3D 仍走
  CPU 着色入 tint（其像素表现有验收口径，接入 GPU 光照是独立一档，见 A559）。
- 验证：`runtime_gui_gl_3d` 直跑 "GL 3D: OK"——新增断言段覆盖：缺省逐位
  重现历史基线 h0（改灯前后各验一次）、非法参数拒绝、纯环境光≈140、
  正面灯≥240 / 背面灯≤20、双灯累加、批量实例吃同一份光照、**批中途换灯**、
  恢复缺省逐位回 h0；`case_game_render3d` 10.5s 绿（Render3D 包压在新
  着色器上无感）。
- 关联：`Zan.Game.Render3D`（`docs/ECOSYSTEM_ROADMAP.md` §1.30）；
  Render3D→GPU 光照接入立 A559。

### [ ] A559 Zan.Game.Render3D 接入 GPU 光照 uniform 通路（A489 落地后的一档）

- 背景：A489 已把 3D 管线的光照做成 uniform 通路（4 盏方向光 + 环境光，
  `Canvas.SetLights3D`，单网格与批量实例共用）。Render3D 目前仍是
  CPU 侧 `Lighting.Shade` 算完打包成 tint 传入，与 GPU 键光叠加成双重着色。
- 方向：Renderer3D 提交前把场景灯推给 `SetLights3D`，材质输出改传反照率
  （albedo）tint，让 GPU 做着色；CPU 兜底档（3D 不可用）保留现有 CPU
  着色路径。改动会变像素表现，需按 gui-design/game-dev 的像素复核口径
  验收（对照改前截图），不能只看编译过。


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

### [x] A499 crypto 归档按族拆成员 —— 定性不切（闭账）

- **闭账决定（2026-10-25）：保持单成员，不再拆分。**证据链：
  1. 发布体积诉求已兑现（A493 归档化 + --publish --gc-sections：hello 产物 zan_hw_* 计数 0）；
  2. 免拆源闭包方案（ld -r --gc-sections 按族抽依赖）实测可行到第三步，死于 binutils PE COFF 展开信息悬垂（前一条留档）；
  3. 物理拆分勘察（2026-10-25）：x86/arm 变体按族相邻但 KAT 段（aes/ghash/crc32c 混在 1562-1808）与 ARM 辅助（bswap128_arm 跨四共用）交织，
     #ifdef 方案要插 25+ 个守卫且共享助手上提，热点密码代码的手术风险不成比例；
  4. 剩余价值只在开发构建（无 gc），且只影响"用了 crypto 却只用单族"的程序——crypto 程序通常多族同用。
  若将来出现真实的单族开发体验痛点，从本条的勘察（族分布 + 58 符号清单）重启，按 #ifdef 分批方案执行。

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
- 2026-10-25 复核：清单/触发路径修复确认在位（drivers.yml 提交清单含
  decimal/crypto/embed_api/inflate；toolchain.yml 触发路径含 embed_api/inflate）；
  **macOS 死腿已救活**（3b47a5587 修符号检查器、a3c50cafd 补齐
  `toolchain/macos/arm64/zanrt_decimal.o`）。剩 wasm32 / ios 两平台的对象
  job 未接（wasm32 配方是 .cmd、GUI 依赖外部 freetype checkout；ios 无配方
  工具链）——工具链受阻，维持 [~]。
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

### [ ] A544 子窗口（聊天记录窗/独立聊天窗/设置窗）在本会话无法像素验收
- 现象：`ZAN_UI_SCRIPT` 的 `dump pixels` 只抓**主 App** 的窗口；子窗口
  （`Gui.ChildWindow`：聊天记录窗、独立聊天窗、设置窗、通讯录管理窗）是
  各自的顶层窗口，抓不到。改用屏幕抓取（CopyFromScreen）在本会话拿到的是
  全黑桌面——没有可交互的桌面会话。
- 绕法（已落地）：把窗口**内容**（视图）塞进独立宿主 Form 再抓像素。设置窗
  （`WxSettingsView`）与通讯录管理窗（`WxContactManagerView`）用这招验过，
  像素正常（导航列、正文、底栏各归各位）。聊天记录窗与独立聊天窗把 root
  建在 ChildWindow 类内部，视图接口没暴露，暂用这招。
- 已定性（2026-10-23 19:0x，闭掉「待查」那半）：那 6 条「裁剪」命中**不是误报**，
  是真缺陷——**flex 收缩没有 min-content 下限**（CSS 的 `min-width:auto` 未实现），
  行一旦放不下，子项按比例缩：图标钮被切掉几像素，工具条项被压成 **0 宽**（按钮
  看不见也点不着）。复现：把客户端主窗口改窄（探针副本里 `Form.Create("微聊",
  520, 600)`，只改副本不动模板）后跑聊天页，`ZAN_GUI_LAYOUTLINT=1` 稳定命中 4 条：
  `Button[675,69 34x42]`/`[721,69 35x42]`（标题行图标钮，42→34/35 被切）、
  `Button[717,661 36x42]`/`[753,661 0x42]`（输入区工具条末项被压成 0 宽）。
  单独开聊天记录窗/独立聊天窗（宿主窗）各跑一遍都是 **0 命中**——子窗本身干净。
- 模板侧已修（gui-wechat）：皮肤层补全局工具类 `.shrink-0 { flex-shrink: 0; }`；
  标题行图标钮与各页工具条按钮挂 `shrink-0`；聊天页 `#ToolsHost` 改 `display:
  flex`（ToolStrip 是块级子项时宽度=宿主宽，窄窗口下项被压成 0 宽；做成 flex
  行后工具条是 flex 项，shrink-0 即可保持自然尺寸）。修后同一窄窗探针 **0 命中**，
  像素复验：标题行两个图标钮与工具条图标完整不再缺块（见 `_scratch/probe/
  narrow3.png`）；正常宽度十页重跑仍 0 命中、像素无变化。
- 框架侧缺口 A545 已闭（2026-10-25，c059bba3c：base.css 补 `.shrink-0` +
  flex 收缩 min-content 下限；停靠 FitSize 裁 0 宽定性为 WinForms 同款不改）。
- 前提：要有可交互的桌面会话（锁屏/远程会话会拿到黑屏），或让子窗口支持
	  把 root 暴露给宿主驱动。


