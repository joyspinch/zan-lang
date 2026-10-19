# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：`[ ]` 未完成 · `[~]` 进行中 · `[-]` 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 `[x]` 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出

## 未完成

### [ ] A384 自举 IR 后端没有运行期类标识——`x is T` / switch 模式判不了

- 现象（2026-10-20 差分门 `ctest -R selfhost_diff` 实测）：同一个程序，C 宿主
  编译运行正确，自举编译器（gen1）要么拒绝编译、要么结果不同：
  - `tests/conformance/cs_b05_pattern_var.zan`（`case Dog d when ...` / `case string s`
    打在 `object` 类型的被匹配值上）→ 自举报诊断拒绝；
  - `cs_b06_switch_expr.zan`（switch **表达式**的 `Circle c when ... => 10`）→ 同上；
  - `pattern_value_types.zan`（`case Dog d` 打在基类 `Animal` 上）→ 同上。
- 根因：C 宿主用**分配点 site id**（对象头 obj-8）+ `__zan_site_tynames` 祖先名表
  做运行期类型判定（`src/compiler/irgen_expr.c:10603` `emit_runtime_is_check`）；
  ngen 后端用 obj-8 高半部的 site id（`src/selfhost/ngen.zan:8039`
  `EmitTypeTestNg`）。**自举 IR 后端（`src/selfhost/irgen*.zan`）两者都没有**：
  它的对象头只有 `[-16]=rc`、`[-8]=dtor`（`irgen.zan` 的 `EmitArcRuntime`），
  字符串更是裸 `malloc` 的 C 串（无头、无标记）。没有类身份，`object` 槽里的值
  在运行期无从判断是哪个类、是不是字符串。
- 现状处理：本轮已把"判不了"从**静默错答案**改成 **fail-closed 诊断**
  （`irgen_expr.zan` 的 `EmitTypeTestVal`），并在
  `tests/selfhost/diff_known_gaps.txt` 登记为已知缺口。功能上仍然缺。
- 方向（实现要点见 `.agents/skills/zan-compiler-internals/references/selfhost-closure.md`
  §9）：给对象头加第三个槽（obj-24 放类身份），类实例化时写入该类的 tid 全局地址
  （`@__tid_X` 的值指向基类 tid——`irgen.zan` 的 `EmitTid` 已经是这条链，catch 匹配
  就在用它），再加一个 `__zan_is_class(obj, tid)` 沿链走；字符串必须同样带上头，
  否则读 obj-24 是越界读，`object` 槽里的字符串会误判成类实例。
- ⚠ 改对象头会影响自举固定点的 gen2 运行时包（`zanrt_sh_*`），改完必须重跑
  `selfhost_fixed_point`；同时要把 `cs_b05_pattern_var` / `cs_b06_switch_expr` /
  `pattern_value_types` 从 `diff_known_gaps.txt` 删掉，让差分门开始强制它们。


### [ ] A379 Task.Run(lambda) 的帧被释放两次——读 .Result/.Wait() 触发 double free

- 现象：`Task<int> t = Task.Run(() => { ... }); t.Result` 运行期报
  `zan runtime: fatal (mem): double free of block 0x...`，进程 abort（退出码 3）。
  最小复现见 `tests/_probe/task_run_lambda_result_double_free.zan`（该目录不参与
  ctest 收集）。
- 根因（三层，缺一不成立）：
  1. lambda 形态的 `Task.Run` 走 **action 帧**（`zan.co.task_action_frame`，
     `irgen_call.c:get_task_action_frame_type`）。该帧把**自己**登记成自己的
     awaiter（`ASYNC_FRAME_AWAITER = 自身`、`AWAITER_STEP = __zan_co_reap`），
     于是任务完成时完成路径 reap 一次；用户读 `.Result`（`emit_task_member`
     mode 1）或 `.Wait()`（mode 3）时又 reap 一次 → 同一块内存释放两次。
  2. 运行时 `__zan_co_frame_free` 的幂等守卫读帧首 8 字节的 `CO_DEAD` 位，
     但小对象分配器回收时把**用户块首 8 字节**写成 freelist 指针，守卫读到的
     是垃圾 → 守卫失效（`src/runtime/rt_mem.c:447` 的 freelist 回写）。
  3. async 调用形态（`Task.Run(Prog.Work())`，keep_result）**不设**自 awaiter，
     帧只由用户 reap 一次，所以不炸——差别在帧所有权，与 lambda 无关。
  - 旁证：`() => 21` 这种表达式体 lambda 的函数体被整个丢掉（生成的
    `lambda_0` 只有 `ret void`），即**值返回 lambda 形态本就没实现**；
    `tests/csharp_compat/task_result_repeat.zan`（C# 对照用例）期望
    `Task.Run(() => 21).Result` 可用，但该目录**没有接进任何 ctest/脚本**，
    所以这个洞一直没被发现。
- 方向（二选一，优先 a）：
  - **a. 类型侧精确化**：`Task.Run/Spawn(lambda)` 的结果类型是 `long` 句柄，
    不是 `Task`/`Task<T>`；只有 async 调用形态才产出 `Task<T>`。检查器把这层
    区分开，"把 lambda 句柄当 Task 用去读 .Result/.Wait()" 就在**编译期**被
    拒绝（现在检查器对内建调用的返回类型宽松放行，`Task<int> t = <long>`
    不报错，类型谎言一路通到 IR）。
  - **b. 帧生命周期侧**：让 action 帧只被释放一次（完成路径与用户路径二选一），
    或让 reap 对已释放帧幂等（把 DEAD 标记放到分配器不回写的槽位）。
- ⚠ 现存代码里有这个形状（`tests/gui/code_navigation_snapshot_test.zan` 用
  `Task worker = Task.Run(delegate () {...});`），目前是**靠时序侥幸没炸**：
  用户 reap 先于完成路径 reap 时不炸，反过来就炸。修 a 会让这批代码编译不过，
  需要一并改写（改用 `long` 句柄 + `TaskJoin`/`Task.IsDone` 等待），属破坏性变更。
- 连带要补的覆盖：`tests/csharp_compat/` 整个目录（7 个用例 + `.cs` 对照 +
  `.expected`）目前**没有任何 ctest/脚本在跑**——接进门禁前先修这一条。

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

- `tests/templates/legend/test_navigation_layout.py` 是手跑的对齐检查器
  （不参与 pytest 收集，需喂 `dump tree` 的 JSON），它硬编码的是旧模型：
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
