# Zan 生产级语言标准：AI 写、机器验（设计提案）

状态：**进行中**（2026-11-24 立项，P1 已开工）。全文除标注「当前实现」的段落外，
**在对应能力落地前均不为真**。设计提案的收缩与退出规则见 `WORKSPACE_CONVENTIONS.md`
附录 §7：能力项落地时，同一次提交把语义写进对应规范，并把本文该条目改为一行"已落地，见 X"。

一句话定位：**像 C# 一样好写，像 Rust 一样可靠，第一门让 AI 写的 GUI 与游戏可以
被机器证明正确的原生语言。**

## 0. 问题与原则

### 0.1 为什么要升级标准

AI 时代写代码的边际成本趋近于零，瓶颈转移到两处：

1. **验证**：一段代码对不对，机器能多快、多确定地回答。
2. **理解**：AI 能否在有限上下文里准确掌握现有代码与语言语义。

本仓库自身就是证据：`AGENTS.md` 的 17 条硬规则、`.agents/skills/` 下的
`language-facts.md`、ctest 定向命中面机检，本质都是**用流程补语言/编译器没提供的
能力**。本标准把这些能力收回到语言与编译器里。

### 0.2 三条铁律（不增加上手成本）

1. **默认推导，显式是可选锁**。新手什么都不写，编译器推导并在 IDE 悬停/文档里展示；
   写出来即成为契约，违反即编译错误。
2. **只用 C# 开发者已认识的形式**：特性 `[...]`、接口、`where`、插值字符串、
   `switch`。**不新增关键字**（`weak` 已有）。
3. **严格度分档**（参照 C# `<Nullable>enable</Nullable>`），在 `zan.proj` 中配置：

| 档位 | 适用 | 行为 |
|---|---|---|
| `Learn` | 教学、脚本 | 只报错误，本标准新增检查全部静默 |
| `Standard` | 模板默认 | 安全/并发/泄漏问题报警告，并附 fix-it |
| `Production` | `--publish` 强制 | 上述问题一律报错；包能力必须显式声明 |

新手体验与现在完全一致；严格只在发布门槛生效。

### 0.3 明确不做

- 新增关键字、生命周期标注、强制效果注解。
- 自然语言编程、语言内嵌 prompt（不可验证，与本标准方向相反）。
- 语言层 LLM 关键字：Schema 派生、tool-calling 绑定等做成 `packages/` 里的库
  （如 `packages/Zan.Ai`），不进语法。
- 循环回收器（cycle collector）：会破坏 ARC 的确定性卖点；循环问题靠 §3 的静态
  检查与 debug 诊断解决。

---

## 1. AI 接口层：编译器是 AI 的一等接口

### 1.1 当前实现

- `zanc --emit-symbols` / `--gen-meta` 可输出类型索引与编译单元元数据 JSON
  （见 `docs/ZANC_CLI.md`）。
- 编译期诊断为人读文本；zan-lsp 内部持有完整语义信息，但未以 CLI 形式开放。

### 1.2 目标设计

| 编号 | 能力 | 说明 |
|---|---|---|
| S1 | `zanc --diag=json` | **已落地，见 `docs/ZANC_CLI.md` §1.6**（JSON Lines、编号、位置；fix-it：E0032 缺 `;`、E2084/E2085/E1005 拼写建议、E2073 `=`→`==`、W2111 补 `weak`；结束位置 `end`；zan-lsp 经 `data.fix` 下发补丁、codeAction 给 quickfix）。余：指向 SPEC 小节与 conformance 用例的链接；E2110 入库后补 `await` 补丁 |
| S2 | `zanc query <符号>` | 已落地为 `zanc --query`，见 docs/ZANC_CLI.md §1.7a（catch 扣除、override/接口实现展开、方法组）；经委托变量的调用目标运行期才定，不展开 |
| S3 | `zanc --affected <diff>` | 已落地为 `scripts/affected.py`（映射要读 ctest 注册信息，不归编译器）+ `zanc --emit-deps` 记真实依赖，见 docs/ZANC_CLI.md §5 |
| S4 | `zanc --summary <包>` | 已落地，见 docs/ZANC_CLI.md §1.7：公开签名 + 效果列（S2 的 throws/能力）+ 一行文档；余：契约列（P4） |
| S5 | `zan check` | 第一刀已落地为 `scripts/zan_check.py`，见 docs/ZANC_CLI.md §1.8：编译诊断 + 泄漏门禁合成单份 JSON；余：ui/replay 步骤（随 P2/P3）、SDK 原生命令形态 |

AI 工作循环收敛为：**改代码 → `zan check` → 应用 fix-it**。

### 1.3 规范化写法

- 格式化器 + 分析器把惯用写法收敛到唯一规范形式，降低 AI 输出方差。
- 每条诊断都能追溯到 SPEC 条款；SPEC 每条规则都锚定一个 conformance 用例（§7）。

---

## 2. 高安全：换语义，不换语法

### 2.1 S6 上下文感知的插值字符串

写法与现在完全相同，语义自动安全（参照 C# InterpolatedStringHandler）：

```csharp
db.Query($"SELECT * FROM u WHERE name = {name}");  // 自动参数化
html.Write($"<p>{comment}</p>");                    // 自动 HTML 转义
Process.Run($"git log {branch}");                   // 按 argv 拆分，不经 shell
```

接收方按参数类型（`SqlInterpolation` / `HtmlInterpolation` / `ArgvInterpolation`）
决定转义规则；拼接成普通 `string` 再传入时在 `Standard` 档告警、`Production` 档报错。
AI 生成代码中最常见的注入类漏洞在语言层消失。

### 2.2 S7 包能力清单

- 编译器从调用图推导每个包实际使用的能力：`Net`、`File`、`Process`、`Native`
  （`DllImport`/`unsafe`）、`Registry`、`Clipboard` 等。
- `zan.proj` 声明 `Capabilities`；`--publish` 校验「推导 ⊆ 声明」。
- 三方包或 AI 生成的包「偷偷联网」即编译失败——供应链安全的语言级解法。
  Zan 源码分发 + AOT 的模型让全程序静态检查天然可行。

### 2.3 S8 可空引用流分析

`Standard` 档开启引用类型可空流分析（C# 开发者已熟悉的 `string?` 语义），消灭大部分
运行时空引用崩溃。当前 SPEC 中 `T?` 仅用于值类型。

### 2.4 S9 无未定义行为

`unsafe` / `DllImport` 之外不存在未定义行为：边界检查常开；整数溢出 debug 下检查；
未初始化读取在编译期拒绝。

---

## 3. 高可用：ARC 默认不泄漏

### 3.1 当前实现

- 运行时退出时报告仍可达对象，精确到分配点与类型（如 `Widget/Button.zan:134:29
  [List<PropSpec>]`）。
- `weak` 字段打破循环；`tests/leakprobe/` 有一次性泄漏探针。
- 退出泄漏数未作为测试门禁。

### 3.2 目标设计

| 编号 | 能力 | 说明 |
|---|---|---|
| S10 | 泄漏门禁 | 已落地，见 docs/ZANC_CLI.md「泄漏门禁（S10）」：run_case 默认 `--check-leaks`，基线 `tests/leak_baseline.txt` 已清零（上线 12 例：7 例 Exit 误报由 S13 剔除、5 例修掉），此后泄漏一律要修 |
| S11 | 类型图强环检查 | 已落地，见 docs/SPEC.md §6.4：W2111 报用户类之间的强引用环（含经集合/数组），fix-it 给回指针加 `weak`；委托边留给 S12/S13 |
| S12 | 事件订阅绑定订阅者生命周期 | 已落地，见 docs/SPEC.md §6.4：`+=` 目标以 this 为根时 lambda 弱捕获 this，订阅者释放即失效；其余目标保持强捕获 |
| S13 | debug 环诊断 | 已落地，见 docs/ZANC_CLI.md §1.5：`--check-leaks` 退出报告试探删除残留对象，逐条打印孤儿环路径；`Environment.Exit` 只计孤儿环 |
| S14 | 结构化崩溃报告 | 崩溃输出 JSON：调用栈、调度种子（§4.3）、最近输入帧（§6.2），可直接交给 AI |
| S15 | 资源未释放告警 | `IDisposable` 未进 `using` 且未转移所有权 → 告警 |

S11 + S12 预计覆盖 GUI 场景绝大部分泄漏来源（父子节点、事件回调）。

---

## 4. 高并发：线程安全靠推导

### 4.1 S16 跨线程共享推导（零标注 Sendable）

对象被传入 `Task.Run`、`Channel<T>`、其他线程时，编译器要求它满足其一：

- 值类型；
- 深度不可变（所有字段 `readonly` 且类型递归不可变，编译器推导）；
- 标注 `[ThreadSafe]`（作者承诺内部同步）。

否则报错，fix-it 给出「克隆」或「包一层锁」两种修法。

**附带性能收益**：编译器能证明不跨线程的对象，使用**非原子引用计数**。ARC 主要开销
来自原子操作，此项预期使多数对象的 RC 开销下降一个量级（需基准证明，见 §8 P4）。

### 4.2 S17 结构化并发

- 子任务生命周期不超出父作用域；取消信号自动向下传播；不存在孤儿任务。
- `async` 任务中未被观察的异常 → 编译期告警。

### 4.3 S18 确定性测试调度器

测试模式下 async 调度器使用固定种子并记录调度序列；失败时输出种子，并发 bug
100% 可复现。DAP 在此之上支持时间回溯调试。

### 4.4 S19 UI 线程归属推导

`Control` 及其派生类成员自动归属 UI 线程；后台线程直接调用 → 编译错误，fix-it 为
`await Ui.Post(...)`。

---

## 5. GUI 可验证：让 AI「看得见」窗口

### 5.1 当前实现

- 窗体以 HTML 标记编写（如 `src/ide_zan/src/IdeForm.html`），运行时解析。
- 控件属性经 `PropSpec` 描述，按字符串键读写
  （`packages/Zan.Gui/src/Gui/Core/PropSpec.zan`）。
- UI 自动化经 `UiDriver` 读取 `ZAN_UI_SCRIPT` 指定的文本命令脚本
  （`packages/Zan.Gui/src/Gui/Testing/UiDriver.zan`）。
- 已有主题配色/字体/帧预算等预算类测试（`tests/run_theme_color_budget.cmake`、
  `tests/run_frame_budget.cmake` 等）。
- `docs/GUI_REFACTOR.md` 已在推进类型化与唯一事实来源，本节与之方向一致，作为其后续。

问题：名字写错、绑定写错、布局溢出，都只能在运行时甚至肉眼发现。

### 5.2 目标设计

**S20 标记参与编译。** HTML 窗体在编译期生成强类型分部类：

- 元素 id → 类型化字段；未知控件名/属性名 → 编译错误。
- `{Binding Name}` 在编译期对照模型类型检查。
- 用户写的 HTML 与现在一致；**AI 得到的是编译错误，而不是一个空白窗口**。
- 与 GUI_REFACTOR 保留的「元数据驱动按名路径」兼容：设计器仍可按名操作，但编译期
  已验证名字存在。

**S21 语义树为唯一事实来源。** 每个控件暴露 role / name / state / bounds / enabled：

```bash
zan run app.zan --ui-dump json   # 输出当前窗口语义树快照
```

无障碍、UI 测试、AI agent 共用同一棵树。**AI 读文本即可理解界面，无需截图。**

**S22 `zan ui check` 布局不变量。** 无头运行，自动遍历项目所有窗体：

- 文字截断、可交互控件重叠、超出窗口、点击区域过小、对比度不足；
- 100% / 150% / 200% DPI 各跑一遍；
- 伪本地化：全部文本拉长 40% 再检查。

复用现有预算类测试的基础设施。

**S23 强类型 UI 测试 API**（由 S20 生成的类型驱动，替代字符串脚本）：

```csharp
var f = ui.Open<LoginForm>();
f.UserName.Type("admin");
f.Login.Click();
ui.Expect(f.Error).Hidden();
ui.Snapshot("login_ok");   // 语义树快照为主（文本、可 diff），像素比对为辅
```

**S24 模型可脱窗测试。** 绑定的 ViewModel 不依赖 `App` 即可实例化与断言，
窗体逻辑的大部分测试不需要起窗口。

---

## 6. 游戏可验证：仿真确定、设计可证

### 6.1 当前实现

- `Game.Foundation` 提供固定步长时钟与确定性随机
  （`packages/Zan.Game/src/Game/Foundation/FixedStepClock.zan`、
  `DeterministicRandom.zan`）。
- `Netcode` 提供回滚会话、快照与校验和
  （`packages/Zan.Game/src/Game/Netcode/RollbackSession.zan`、`Snapshot.zan`、
  `Checksum.zan`）。
- 游戏模块约定平台中立，窗口/输入/渲染在 `Gui`（见 `packages/Zan.Game/src/Game/README.md`）。

问题：确定性与逻辑/表现分离目前是**库的约定**，违反时不会被发现。

### 6.2 目标设计

**S25 确定性模式（一个特性开启）：**

```csharp
[Deterministic] namespace MyGame.Sim;
```

借助效果推导，在该命名空间内禁止：`DateTime.Now`、未注入种子的随机数、依赖哈希
迭代序的写法（运行时使用 siphash，需核实字典迭代序是否受种子影响）、线程与 IO、
引用 `Gui`/渲染模块。浮点切换到严格模式（禁止 FMA 收缩等）。
**逻辑/表现分离从约定变成编译保证。**

**S26 回放即测试。** 每次试玩记录种子 + 输入流；bug 报告即回放文件；CI 重放并逐帧
比对 `Checksum`；回放文件自动成为回归用例。复用 `RollbackSession` / `Snapshot`。

**S27 规则即不变量：**

```csharp
[Invariant] bool GoldConserved() => TotalGold() == initialGold + minted - spent;
[Invariant] bool HpValid()       => players.All(p => p.Hp >= 0 && p.Hp <= p.MaxHp);
```

debug 下每帧检查。配合**机器人模糊测试**：无头仿真以千倍速运行随机/AI 输入，
自动发现崩溃、卡死（softlock）、经济溢出。

**S28 设计结构可证：**

- 任务、AI、技能状态机以 `enum` + `switch` 表达；确定性模式下强制 `switch`
  穷尽，编译器报告不可达状态与死状态。
- 配置表（`Game.Data`）构建期对照类型校验；表间引用（物品/技能 ID）构建期验证。

**S29 帧预算静态检查。** `Update` / `FixedUpdate` 等每帧路径上的对象分配与高频
引用计数增减 → 告警；可用 `[FrameHot]` 显式标注其他热路径。

---

## 7. 规范与测试互锚

| 编号 | 能力 | 说明 |
|---|---|---|
| S30 | 语义规范补全 | SPEC 补写：ARC（参数借用/持有、返回值所有权、闭包捕获、`weak` 置空时机、RC 原子性、析构顺序、异常展开释放）、async（状态机字段生命周期、`await` 前后持有关系、取消语义）、泛型（单态化与擦除两条路径各自保证什么） |
| S31 | 规则锚定用例 | SPEC 每条规则链接一个 conformance 用例；诊断（S1）反向链接规则 |
| S32 | 泛型双路径差分 | 同一泛型代码分别以值类型/引用类型实例化，比对 `typeof(T)`、静态字段、默认值、相等性、反射结果 |
| S33 | 跨平台一致性矩阵 | 同一批 conformance 用例在 Windows / Linux / macOS 输出逐字节一致；平台能力缺失时链接期报错，不静默降级 |

---

## 8. 路线图

按依赖与收益排序；每阶段落地即按 WC 附录 §2 把对应小节改写为当前实现。

| 阶段 | 内容 | 验收 |
|---|---|---|
| **P1 地基**（B-ID222） | S1–S5 AI 接口；S10 泄漏门禁；S11 环检查；S12 事件自动解绑；S13 环诊断 | 现有 GUI 测试零泄漏；AI 修复循环可完全基于 JSON 诊断 |
| **P2 GUI**（B-ID223） | S20 标记编译；S21 语义树；S23 类型化测试；S22 ui check；S24 | 绑定写错编译期报错；全部 `templates/` 窗体通过 `zan ui check` |
| **P3 游戏**（B-ID224） | S25 确定性模式；S26 回放回归；S27 不变量 + 机器人；S28 穷尽 switch；S29 | `templates/game` 全部具备回放回归用例 |
| **P4 安全与并发**（B-ID225） | S6 安全插值；S7 能力清单；S16 跨线程推导 + 非原子 RC；S17–S19；S8、S9、S14、S15 | `--publish` 默认 Production 档；基准证明 RC 开销下降 |
| **P5 规范**（B-ID226） | S30–S33，贯穿各阶段同步补写 | SPEC 每条规则均有用例锚定 |

推荐起手：**S1 `--diag=json` + fix-it**。不改语言、收益立刻可见，且本仓库自身的
AI 协作是第一受益者。

## 9. 对外差异化验证

用数据回答「为什么不直接用 C# NativeAOT」：同一程序（JSON 服务 + GUI 小应用 +
一个模板游戏）对比启动时间、常驻内存、P99 延迟、产物体积、帧时间抖动。
预期优势在**产物体积**与**尾延迟**（ARC 无 GC 暂停），基于现有
`tests/run_size_budget.cmake`、`tests/run_frame_budget.cmake` 扩展为对比基准。
