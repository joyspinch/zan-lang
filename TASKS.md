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

### [ ] A380 协程帧的释放仍只在类型侧收口——运行时没有幂等保护

- 现状（A379 已闭）：`Task.Run(lambda)` 的句柄被检查器定型成"无结果任务"，
  把 lambda 句柄当 `Task<T>` 用去读 `.Result`/`.Wait()` 会在**编译期**被拒绝
  （`tests/diag/task_run_action_result_type.zan`），那条运行期 double free 路径
  因此不可达。
- 但运行时本身仍没有防御层：帧若真被释放两次，`__zan_co_frame_free`
  （`src/runtime/rt_io.c:4164`）的幂等守卫读帧首 8 字节的 `CO_DEAD` 位，而小对象
  分配器回收时把用户块首 8 字节写成 freelist 指针（`src/runtime/rt_mem.c:447`），
  守卫读到的已是垃圾 → 二次释放直接 abort（`double free of block`）。
- 方向（二选一）：
  - **a. 活协程表仲裁**：`zan_co_live_del(frame)` 改成返回"本次是否真的摘掉了
    这个帧"，谁摘掉谁负责 free，后到的 reap 看到"已摘"直接返回。活协程表本来
    就是帧的注册表（`src/runtime/rt_timer.c:956`，带墓碑），天然适合当所有权仲裁。
  - **b. 守卫位搬家**：把 DEAD 标记挪到分配器不回写的槽位（帧首 8 字节不能用）。
    但帧释放后那块内存会被复用，标记会变成别的对象的垃圾 → 只能把"误判为已死"
    的代价从 abort 降成泄漏，不彻底。
- 连带：`__zan_co_isdone` 在帧已被完成路径释放后仍会读它的 DONE 字段（先查活
  协程表、再读帧字段，中间有窗口）——4 字节 UAF 读，通常不炸但语义不严。
- ⚠ 改 `src/runtime/rt_io.c` / `rt_timer.c` 会动到自举固定点的 gen2 运行时包
  （`zanrt_sh_*`），改完必须重跑 `selfhost_fixed_point`。

### [ ] A381 SPEC 的"尚未实现"清单 7 项一项没动

`docs/SPEC.md:766-776` 明列、实测仍不可用的：

- `checked` / `unchecked` 关键字是 **no-op**（溢出检查恒关，环绕语义）
- lazy iterators：`yield` 物化成 list，没有 `IEnumerable<T>` 协议
- tagged union 枚举（代数数据类型）
- `params` 之后的可选实参组合未系统验证
- `$@"..."` 组合字符串、`"""` 原始字符串
- COW 集合：`List` 赋值是引用共享，修改即共享
- `[CImport]`、`project.zan` 项目文件、`zan` CLI（实际是 `zanc`）

- 方向：逐项单独立项实现，每项带 conformance 用例；不做半截实现。

### [ ] A382 编译器缺三件基础能力：诊断编号体系、告警级别开关、Marshal

实测（`src/compiler` 全量 grep 0 命中）：

- 无诊断编号：诊断只有自然语言文本，没有 `E001`/`W001` 这样的稳定编号——工具链
  无法按编号定位/抑制，也做不了 `--warn-as-error` 这类开关
- 无 `--warn-level`：没有统一的告警级别开关
- 无 `#pragma warning`：无法在源码里局部调级或禁用
- 无 `Marshal`（`DllImport` 之外的互操作面）
- 附带一条语义偏差：越界检查是有的（`src/compiler/irgen_generics.c:1313`
  数组/string/span），但 `checked`/`unchecked` 开关不了它——只有全局
  `runtime_checks` 标志能整体关掉，与 C#"checked 上下文才检查"对不上。

- 方向：诊断编号是**破坏性变更**（所有诊断文本与编号都要定版，且影响 golden
  诊断输出），先定编号规范与兼容策略；`--warn-level` 与 `#pragma warning`
  跟在编号之后做。

### [ ] A383 图表引擎整代语义差：官方 470 个 option 键里 278 个（59%）从不读取

- 事实（`docs/archive/CHART_CODE_GAP_LEDGER.md`）：对照 ECharts 6.1 的 596 个
  `.ts` 源，官方 option 用到的 470 个配置键里，引擎**从不读取**的有 278 个（59%）。
- 表现：用户照官方示例写 option，键被静默丢弃——不报错、不告警，图形少一块也
  不知道少了哪一块。
- 方向：先补"未知 option 键"的告警（廉价，立刻让缺口可见），再按官方示例
  覆盖度逐个补实现；不要一次铺 278 个键。

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
