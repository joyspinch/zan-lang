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
- 事实（`docs/archive/CHART_CODE_GAP_LEDGER.md`）：对照 ECharts 6.1 的 596 个
  `.ts` 源，官方 option 用到的 470 个配置键里，引擎**从不读取**的有 278 个（59%）。
- 表现：用户照官方示例写 option，键被静默丢弃——不报错、不告警，图形少一块也
  不知道少了哪一块。
- 方向：先补"未知 option 键"的告警（廉价，立刻让缺口可见），再按官方示例
  覆盖度逐个补实现；不要一次铺 278 个键。

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
- 2026-10-25 补充排查（**仍无一复现**，可构造形态全部正确）：
  1. 跨文件同名类级 `T`：`a453.zan` 里 `HolderA<T>` 用 `HolderB<T>`（各自文件
     里的 `T`），主文件先解析、b 文件先解析两个顺序都跑通，输出正确——跨文件
     "先到先得"在类字段/方法形参/返回值的常见形状里没有产生行为差异；
  2. 泛型类上的**方法级同名类型参数**（类 `P2<K2,V2>` + 方法 `T Conv<T>(T x)`）：
     实测方法级形参按方法自己的实参定型，类级字段按类级实参定型，互不顶掉
     （控制实验：同一份代码改名后全绿；最初用 `class Dict<K,V>` 探针失败是
     **内建同名顶掉**，与类型参数逻辑无关 → 单独立 A551）；
  3. 实参个数对不上的形状（2 个形参的类被 1 个实参实例化、反之）实测绑定按
     形状报错/退化，没有出现"错位合一静默通过"。
  结论：三层/跨文件/个数三个方向的疑点在能构造出来的语法形状里都验证正确。
  这条降级为**纯设计债**：要根治仍需按层隔离注册（上面的方向），影响面大，
  单独一条做；不再等待复现用例。
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

- **2026-10-24 实测校准（缩小口径）**：归档按符号缺才拉的机制已验证兑现——
  hello 的 `--publish` 产物符号表里 `zan_hw_*` 计数 **0**（完全不拉）。
  本条对**发布体积**的诉求已由 A493 归档化 + 发布 `--gc-sections` 兜底实现；
  剩余价值仅剩**开发构建**（无 gc，任何一个 `zan_hw_*` 引用仍拉整块 53 KB）
  与不跑 gc 的消费方。按族拆成员（下）仍值得做，但优先级降为开发体验项。
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


