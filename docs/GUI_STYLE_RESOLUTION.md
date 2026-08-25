# GUI 取色与取字的统一封装

控件绘制时的颜色、字号**一律经过样式层解析**，不允许直接读主题令牌或写死
十六进制字面量。这是"换肤即生效"的根基，也是三条策略门禁存在的理由：

| 门禁 | 拦截对象 |
|------|----------|
| `policy_theme_color_budget` | 绘制路径直读 `t.textPrimary` 这类语义色 |
| `gui_theme_font_budget` | 直读 `t.fontSizeSmall` 这类字号 |
| `policy_raw_color_budget` | 裸 `0xAARRGGBB` 字面量（皮肤永远无法覆盖） |

三个测试都是**只降不升的棘轮**：合法清偿后要同步下调预算行，防止债悄悄回潮。

## 标准模式（Widget 层样板见 `Badge.zan`）

```zan
// 1) 解析：类型 + 类 + 状态位 → StyleBox（CSS 规则已套用）
StyleBox s = Style.Of(app, "badge", cls, Style.SNormal());

// 2) 消费：每个属性带兜底值 —— CSS 设了就用 CSS，没设用兜底
int fg   = s.FgOr(app.theme.textPrimary);   // 兜底也走语义令牌
int font = s.FontOr(app.Scale(11));
c.DrawText(x, y, label, fg, font);
```

要点：

* **兜底值本身也要合规**：颜色兜底用语义令牌，字号兜底用 `app.Scale(n)`；
  没有控件上下文时用 `Style.FontFallback(app, "small")`。
* **部件选择器**是 `type::part.class`（如 `select::option`、
  `codeeditor::keyword`），皮肤在 `base.css` / `<pack>/skin.css` 里描述；
  控件侧只需要约定 part 名并在注释里列出。
* **数值缓存必须带失效键**：把解析结果烘进缓存的组件（换行布局、高亮
  运行），签名里要混入会改变解析结果的代次——至少
  `app.bgThemeGen`（每次换肤递增）。否则换肤后表面变了、缓存文字还是旧色，
  这正是"暗色皮肤下聊天看不见"那次事故的根因。

## 宿主覆盖次序

需要宿主点名配色的组件（如编辑器语法色），按三级优先：

```text
内置调色板兜底  <  皮肤 CSS 部件规则  <  宿主显式设置
```

* 调色板经 `Style.RootFromTheme` 注入成 `--code-*` 令牌，`base.css`
  把令牌映射到部件规则；
* 宿主要覆盖时就地调用组件的显式 API（如 `CodeEditor.ApplyTheme`），
  并把自己的值并入该组件的失效签名（见 `CodeEditor.HlThemeSig`）。

## 例外（允许字面量残留的地方）

不是所有八位十六进制都是"皮肤颜色"，以下形态在
`tests/run_raw_color_budget.cmake` 里逐行留预算并注明原因：

* Win32/DWM 标志位与 `GENERIC_*`（`Backend/**` 整目录跳过）；
* 哈希盐等非视觉常量（如 `CodeEditor.Render` 的签名异或掩码）；
* 插画式草图（Wizard 的模板缩略图按语义保持固定配色）；
* 纯透明度阴影（`Layer` 的 `0x22000000` 圈层）。

新增例外时：先问一句"皮肤该不该能改它"。能改 → 走 CSS；不能改 → 留预算行 +
代码注释。实现位置：`stdlib/Gui/{Theme,Style,StyleBox,Fx}.zan` 与
`stdlib/Gui/skins/base.css`；测试在 `tests/run_*_budget.cmake`。
