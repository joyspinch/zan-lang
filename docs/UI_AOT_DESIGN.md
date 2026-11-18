# UI AOT 通道设计契约（B-ID225 承重墙）

状态：设计定稿待评审 · 2026-11-19 · 前置事实依据：Zan.Gui 运行期管线事实地图
（本轮探查报告，file:line 锚点以其为准）。北极星：B-ID225 四原则。

## 0. 目标与非目标

**目标**：把"静态的 UI 事实"从运行期解释挪到编译期——结构、静态属性、事件
接线、静态样式——使帧工作量趋近 O(变更)，发布产物摆脱按名反射接线，开发期
获得编译期诊断。

**非目标**：
- 不删运行期 HTML 解析器。设计器往返（`DesignerHtml` 编解码共享 `WDoc`）、
  `Html.Load(string)` 动态装载、`data-for` 模板行克隆（`Html.Clone`）都依赖
  运行期通道（管线地图 §1/§2/§7）。
- 不删运行期 CSS 级联匹配器。`:hover/:active` 缓动混合、`@media` 运行期求值、
  live/组合器选择器（dependency==2 禁缓存）要求样式表运行期仍可匹配（§3）。
- 不新增包 C 钩子（B-ID222/223 裁定）。生成器挂载走 B-ID223 契约。

## 1. 核心决策：AOT = HTML → 既有"代码建树"范式，不是第二套语义

Zan.Gui 本就双范式：HTML 建树与代码建树共享同一 Control 树与属性协议
（`Html.Build` 的属性循环 :393-587 是唯一权威语义）。**生成器把 HTML 编译成
代码建树调用**——生成物调用与手写控件完全相同的公开原语
（`AddClass`/`SetProp`/`DeclareBinding`/`SetHandler`/`MountSlot`/`ApplyXProp`），
不重实现属性协议。漂移风险收敛为"生成器是否覆盖 Build 循环的全部属性形态"，
由金标回归锁住（见 §6）。

双通道契约：

| 通道 | 输入 | 何时解析 | 消费方 |
|---|---|---|---|
| AOT（新，默认） | `src/X.html` + CSS | 编译期（生成物烘进 exe） | `X : Form`/`ChildWindow` 的文档装配 |
| 运行期（保留） | 任意字符串/散盘 | 运行期 | `Html.Load`、设计器、动态页、金标参考 |

`Form.Show` 的 `assembleDoc` 装配序不变；唯一变化是文档来源从
`FindDoc+Html.Parse` 换成"若存在 AOT 生成物则直接调用，否则回落运行期解析"
——回落即兼容通道，也是迁移期的回滚阀。

## 2. 生成物形态（写死，防体积膨胀）

一个 `X.zan` 的同名文档生成一个静态构建方法：

```zan
partial class X : Form {
    static void __BuildDoc(X vm) { ... }   // 合成，勿手改；__ 归编译器
}
```

- **静态结构 = 直调序列**：`new Button()`/`AddKid`/`AddClass`/`SetProp` 按文档
  序直出。深大页面体积受控的原因：控件构造是共享例程，生成物只是调用流，
  与等效手写建树同量级；不逐元素内联展开数据（B-ID225 ② 的禁令）。
- **静态属性 → `SetProp` 直调**；`data-x-<key>` JSON 复合值预解析成
  `ApplyXProp` 的现成参数（运行期不再拼串）。
- **事件接线静态化**：`@click="OnOk"` 生成
  `c.SetHandler("click", vm.OnOk)` 形态的方法组直调——按名 `Invoke` 反射与
  publish 的 `zan_embed_collect_html_handlers` 扫描补洞（main_frontend.c:446）
  对 AOT 页面双双消失。副作用（有意的行为变更）：handler 缺失/拼错从运行期
  exit 1 前移为编译错误，诊断面更优；`DocWire.AutoWire` 对 AOT 页面跳过。
- **`bind`/`:prop` → `DeclareBinding` 直调**；`z-mount`/`slot` → `MountSlot` 直调。
- **`data-for` 模板行**：行子树保留运行期构建——生成物只落
  "原型子树构建委托"（`RegisterTemplates` 的注册参数从"HTML 片段字符串"换成
  "构建委托"），keyed diff/`ScopeRow`/`Clone` 机制原样复用。
- **`<include>`**：编译期按同一 VFS 口径（publish 加密 VFS / 开发态散盘，
  `Form.Show.zan:184-199`）展开 8 层封顶，语义与 `Html.ExpandIncludes` 对齐，
  金标回归锁往返。

## 3. 静态样式：预解析 + 预索引，级联匹配器不动

- 编译期对文档 `<style>`/`<link>`/皮肤链跑 `Css` 解析与 `StyleSheet.Index`
  分桶，产物 = 序列化的样式集对象（规则已按 byType/anyType/anyUniversal 分桶、
  `var()` 已展开、`@layer` 已摊平），作为烘焙资源随 exe（沿 `gen.embed` 现成
  键值表挂载点，main_sources.c:847——顺带让 `gen.embed` 从文档性键变成真消费）。
- 运行期 `ApplyAppCss` 直接装填预索引样式集，跳过文本解析；**级联匹配、
  状态缓动、`@media` 求值、`Style.CacheGet` 键控不变**。收益 =
  省每次装载的解析 CPU + `@import`/`<link>` 的磁盘 IO（HtmlParser.zan:219-226），
  不含级联成本本身——级联归 §5 增量机制管。
- inline `style` 的 `.zgen-N` 合成保持运行期（动态值通道）。

## 4. 挂载点（遵守 B-ID223 契约，零 C 钩子）

- 生成器实现为 Zan 侧编译期工具（selfhost 编译器管线或独立 pass，随 B-ID223
  定案落位），挂载声明沿包清单/工程键；C 宿主只保留通用读入与产物回注
  （`genmeta` 的 JSON→AST 回注先例已存在，genmeta.h:21-23）。
- 触发条件沿用现状启发式的显式化：`zan.proj type=gui` 且存在同名文档 → 生成
  （`gen.embed` 键转正为显式声明）；无文档 → 纯代码建树工程不受影响。

## 5. 与增量三旗（B-ID174）的分工

帧成本的两个根：每帧全树 Measure（无节点级测量缓存）+ 样式缓存命中仍逐节点
Clone StyleBox（`Style.zan:237-238`）。这两项**不依赖 AOT**，是 B-ID174 拆
FrameLoop 时立三旗的验收内容（needsStyle/Layout/Paint + 脏子树 Measure +
StyleBox 缓存命中免 Clone）。AOT 管的是"每次装载/换页"的一次性成本与接线
静态化；两者收益独立、互不阻塞，三旗先行。

## 6. 里程碑与验收门

- **P1 三旗增量**（承接 B-ID174，先行）：验收 = 大树（≥5k 节点）单属性变更帧
  耗与树规模解耦；金标 `gui_layout_*` 布局树全绿。
- **P2 AOT 结构与接线**：验收 = ① 全部 gui 模板走 AOT 通道，金标 = 同页面
  运行期解析建树 vs 生成建树的 CaptureSnapshot 布局树逐字节一致；②
  handler 拼错在编译期报错（新 conformance 用例）；③ 发布产物不再含
  html_handlers 反射保留段（AOT 页面）；④ 尺寸门禁沿 B-ID218 预算——
  同界面 AOT 前后 exe 对照，膨胀即设计错。
- **P3 静态样式预索引**：验收 = `Html.Load` 动态页回落运行期通道回归全绿；
  装载耗时（Parse 段）对照下降；样式缓存键与命中行为不变（现有
  style_cache 类用例全绿）。
- 双通道金标永驻：运行期解析通道不退役，作为 AOT 的对照参考与动态装载实现。

## 7. 风险与对应

| 风险 | 对应 |
|---|---|
| 生成器漏 Build 循环长尾语义（slot/设计半件/data-x/stop.enter 修饰符） | P2 金标 = 双通道布局树逐字节一致 + Build 属性形态清单驱动生成器验收（管线地图 §2 表逐行过） |
| 接线诊断从运行期前移编译期，测试基线变动 | 行为变更有意为之；docapp 离屏测试改走编译期诊断 + AOT 产物，旧 fail-fast 用例改写 |
| `Html.Load(string)` 语义争议 | 定死：字符串装载保留完整解析（动态页合法语义），只有 Form/ChildWindow 同名文档走 AOT |
| 设计器往返漂移 | 设计器继续消费运行期 `WDoc` 通道（非目标里已锁），生成器不做设计器格式 |
| 开发回路退化（改 HTML 要重编译） | 回落阀：开发态可在 zan.proj 关 AOT（`aot=off`）走运行期解析，热迭代不劣化；发布态强制 AOT |
| 生成物体积失控 | §2 形态写死 + P2 ④ 尺寸门禁 |
