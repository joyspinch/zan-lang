# Gui 组件创作规约

> 适用范围：`packages/Zan.Gui/src/Gui/**` 下的全部控件，以及使用方
> （`src/ide_zan`、业务代码、第三方应用）。
> 目标：同一个词在所有组件上是同一个量；新组件的写法一眼可辨；
> 组件之间可以自由互嵌与调用。
> 配套：`docs/STDLIB_COMPONENT_STANDARDS.md`（通用库规范）、
> `packages/Zan.Gui/src/Gui/Styling/`（度量唯一真源）。

---

## 0. 一句话

**一个组件 = 一个类 + 一族 CSS 规则 + 一份绑定通道声明。**
类负责行为与几何计算，CSS 负责外观，度量一律取令牌——
三者各司其职，换肤即换外观，组件之间不互相抄样式。

---

## 1. 度量：唯一真源与阶梯

所有长度类度量只有四个真源文件，任何地方都不得另写一份：

| 文件 | 档位 | CSS 令牌 |
|------|------|----------|
| `Styling/Space.zan` | 内边距 4/8/12/16、间距 4/8/12 | `--padding-*` `--space-*` `--gap-*` |
| `Styling/Radius.zan` | 圆角 0/2/4/8/12/16/9999 | `--radius-*` |
| `Styling/WidgetSize.zan` | 控件高度 24/28/32/40（紧凑件 16/20） | `--height-*` `--height-micro-*` |
| `Styling/Motion.zan` | 动效 100/200/300ms | `--anim-*` |

规矩：

1. **组件代码里的兜底值必须落在同一阶梯上**。控件常写
   `s.HeightOr(app.Scale(24))` 作为"CSS 没写高度时"的兜底——这个 24
   必须是 `WidgetSize` 里的那一档，不是随手写的数。
2. **高度与内边距优先由 CSS 声明**。`tag { height: var(--height-tiny) }`
   这种写法让"档位"在样式层可查、可换肤；代码兜底只是最后一道保险。
3. **颜色与字号不写字面量**：颜色走主题语义令牌，字号走
   `--font-size-*`（见 `STDLIB_COMPONENT_STANDARDS.md` 附录）。

### 尺寸档（tiny / small / medium / large）

档位词汇表全库统一为四档，**class 名与档位名一一对应**：

```zan
Control.SizeClass("tiny")   // → "tiny"
// tiny/small/medium/large → 同名 class；未知值 → ""（默认档）
```

- 四档的几何写在 base.css 的 `.tiny/.small/.medium/.large` 规则里，
  取值来自 `--height-*`（标准族 24/28/32/40）或 `--height-micro-*`
  （紧凑族 16/20/24/28）。
- **medium ≡ 默认档**：没有 `.medium` 规则的组件，medium 档就是它的
  默认外观。因此不要为了"凑齐四档"给每个组件硬写 `.medium` 规则——
  写了反而会盖掉皮肤在基础规则里给的高度。
- 控件用 `Binding<string> Size` 暴露尺寸档（Naive UI 的 `size` prop），
  三个动作就够了：

  ```zan
  Binding<string> Size;                            // 字段
  override Binding<string> SizeOf() { return Size; } // 设计器发现它
  ps.Add(this.SizeProp());                          // 属性行
  this.ApplySizeClass();                            // 解析前落到 class
  ```

  `ApplySizeClass()` 是基类的统一入口：**档位互斥**——挂新档前先把
  `Class` 里其它档位去掉，所以 `Class = "small"` 与 `Size = "large"`
  同时存在时是 large 说了算；**medium 是默认档，不覆盖**用户在
  `Class` 里写的档位（`Class = "primary small"` 这种既有写法不能被
  静默改成 medium 档）。控件在解析样式前调用它、解析后把 `Class`
  还原，档位因此只是"这一帧的排布"，不污染作者写下的 `Class`。
- 未支持尺寸档的控件**不要**提供 `Size` 字段（避免"设了不生效"的
  假接口）。支持档位就必须把四档的 CSS 规则补齐。

---

## 2. 样式解析：统一入口

控件解析自己的样式，**一律走基类入口**，不要手写 `Style.Of`：

```zan
// 自己的外观（类型 = StyleType()，类 = Class，id = name）
StyleBox s = this.Box(app);            // 常态
StyleBox h = this.BoxState(app, Style.SHover());   // 悬停态

// 借用别的组件的外观（部件、复用其它族）
StyleBox o = this.ResolveStyleAs(app, "select", Class, Style.SNormal());

// 部件表面的**基线**外观（列表行、滚动条轨道、弹层、分割线…）：
// 不带本控件的 Class、不带 id、不套行内覆盖
StyleBox r = this.BareBox(app, "listrow", Style.SNormal());
```

三种入口的分工：`Box`/`BoxState` 解析**自己**的外观；`ResolveStyleAs` 借用
**别的类型**的外观（带自己的 Class）；`BareBox` 取某个类型的**裸基线**
——部件表面用它，因为套上本控件的 Class 会串味（列表写成 `.primary`
就把每一行都染成主色），套行内覆盖同样不该发生。

为什么不让直接调 `Style.Of(app, "button", ...)`：

1. **行内样式会丢**。基类入口会套用控件自己的行内样式（`style="..."`），
   手写路径不会——同一个控件，两条路径外观不一致。
2. **每帧多克隆一份 StyleBox**。基类入口带缓存（主题代次 / 度量缩放 /
   状态 / 类型 / 类 / id 全部进键），同一个控件在一帧里问两次样式
   不会重复解析。
3. **类型名不再手抄**。`StyleType()` 已经是类型选择器的唯一真源，
   手写字符串迟早与 `Kind()` 漂移（历史上 `radiobuttongroup` 与
   `radiogroup`、`selectbox` 与 `select` 就各写各的）。

---

## 3. 一个新组件的十步

```zan
public class MyWidget : Control {
    // 1) 构造：InitControl 设名字与停靠方式。
    MyWidget() { this.InitControl("mywidget", 0); }

    // 2) 测量：首选尺寸来自样式盒，不写死。
    override void OnMeasure(App app) {
        StyleBox s = this.Box(app);
        prefH = s.HeightOr(app.Scale(32));   // 兜底值取 WidgetSize 档
        prefW = s.WidthOr(app.Scale(120));
    }

    // 3) 绘制：颜色/字号全部来自样式盒。
    override void OnPaint(App app) {
        StyleBox s = this.Box(app);
        s.Paint(app, bx, by, bw, bh);
        app.canvas.DrawText(bx, by, this.Text, s.FgOr(app.theme.textPrimary),
                            s.FontOr(app.Scale(14)));
    }

    // 4) 事件：声明可绑定的事件名。
    override List<string> Events() { ... }

    // 5) 设计器：类型标识。
    override string Kind() { return "MyWidget"; }

    // 6) 设计器：属性清单（指针必须指向活的外部通道）。
    override List<PropSpec> Props() { ... }

    // 7) 序列化：额外的键值属性。
    override bool SetExtra(string key, string val) { ... }
}
```

配套还要有：

8. **base.css 里的规则族**：`mywidget { ... }` + 档位/状态/部件规则。
   类型名 = `Kind()` 的小写。
9. **conformance 用例**：`tests/conformance/gui_<name>.zan` + 同名
   `.out` 金样。断言的是**契约**（档位高度、圆角、事件、绑定通道），
   不是像素。
10. **画廊条目**：`examples/gui_gallery/` 里能看见、能试。

---

## 4. 互嵌与调用

### 互嵌

```zan
// 容器：Add 拿回子节点引用，With 追加并返回自身（可链式）
Button save = panel.Add(new Button { Text = "保存" });
panel.With(a).With(b);

// 插槽：子组件把自己的位置交给宿主
//   MountSlot("header", c) —— 宿主按名取回（NamedSlotHost）
```

要点：

- 子控件**只有一个位置**。同一个实例挂进两处会重复绘制、丢焦点；
  收养前基类会判环并拒绝。
- 复合控件（自己也是别人的子节点）不要在 `OnPaint` 里手摆子控件位置，
  走 `OnMeasure` + `Arrange` + 自己的 `OnPaint`，否则父容器的布局算不准。

### 调用（事件与绑定）

- **事件**：`BindEvent("click", ...)` / 控件自己的 `Click` 信号。
  事件名必须出现在 `Events()` 里，否则设计器与序列化器看不到。
- **数据绑定**：唯一协议是 `Binding<T>`。主值命名 `data`，尺寸档用
  `Size`，文案用 `text`。控件内部的状态缓冲（编辑缓冲、滚动位置）
  是私有的，不得当作对外绑定通道。
- **变体**：外观变体是 CSS class（`primary` / `ghost` / `outline` /
  `small` …）。写 `Class = "primary small"` 时注意互斥：同一根轴上的
  变体（尺寸、语义色、形态）只能留一个，控件提供了
  `SetClassIn(group, cls)` 负责先清后加。

---

## 5. 反面清单

| 别做 | 为什么 | 改成 |
|------|--------|------|
| `Style.Of(app, "button", Class, ...)` | 丢行内样式、多克隆、类型名会漂 | `this.Box(app)` |
| `app.Scale(18)` 这类随手数字 | 不在任何度量阶梯上 | 取 `WidgetSize`/`Space`/`Radius` 档 |
| `0xFF3366` 之类颜色字面量 | 皮肤永远改不动 | 走样式层 / 主题语义令牌 |
| 组件里写 `transition: 220ms` | 与全库动效档脱节 | `--anim-fast/medium/slow` |
| 给不支持的控件加 `Size` 字段 | 设了不生效的假接口 | 不加；要加就补齐四档 CSS |
| 自己造第二条绑定通道 | 与 `data` 语义打架 | 走 `Binding<T>` |

---

## 6. 守门

- `scripts/style_audit.py`：间距/动效令牌化检查（挂在
  `scripts/check_structure.ps1`）。
- `tests/conformance/gui_size_tiers.*`：尺寸档与圆角的度量表。
- `tests/conformance/gui_size_prop.*`：尺寸档作为控件通用能力的契约
  （四档生效 / 档位互斥 / 默认档不覆盖 class）。
- `tests/conformance/gui_style_box.*`：样式解析统一入口的契约
  （与老路径等价 / 行内样式生效 / 类进缓存键）。
- `tests/conformance/gui_skin_theme_vars.*`：皮肤层必须认得主题令牌。
- 三个只降不升的预算门：`policy_theme_color_budget`、
  `gui_theme_font_budget`、`policy_raw_color_budget`。
