# Tailwind 工具类

GUI 的 class 列表除了皮肤自己的语义类（`.primary`、`.ghost`）之外，可以直接写
Tailwind 的原子类。引擎在解析样式时把每个能识别的 token 翻译成它本来就理解的
CSS 声明（`StyleSheet.Decl`），所以皮肤、过渡、`:hover` 缓动、DPI 缩放全部照旧。

```zan
Ui.Button(app, "确定",
          "flex items-center gap-2 px-4 py-2 rounded-lg bg-blue-600 "
          + "text-white font-semibold shadow-md transition duration-200 "
          + "hover:bg-blue-500 disabled:opacity-50");
```

实现在 [`stdlib/Gui/Tailwind.zan`](../stdlib/Gui/Tailwind.zan)，测试在
`tests/gui/tailwind_test.zan`（`ctest -R gui_tailwind`）。

## 规则

* **不认识的 token 原样留下。** `.primary` 这类皮肤语义类照旧由 `.css` 里的
  类选择器匹配，两种写法可以在同一个 class 列表里混用。
* **工具类最后应用**，因此和 Tailwind 一样胜过皮肤里的组件规则；同一个属性
  被写两次时，后一个 token 胜出。
* **间距刻度 = `n * 4px`**：`p-4` = 16px、`p-1.5` = 6px、`p-px` = 1px。
* **变体前缀** `hover: focus: active: disabled: checked: selected:`
  （以及 `focus-visible:`）映射到引擎的状态位，可以叠加
  （`hover:focus:bg-red-500` 需要两者同时成立）。引擎没有建模的变体
  （`md:`、`sm:`、`dark:`、`group-hover:`）整条 token 忽略——没有视口断点，
  响应式请用容器自己的布局。
* **任意值** `w-[240px]`、`h-[38px]`、`bg-[#0f172a]`、`text-[13px]`、
  `rounded-[10px]`，值里的 `_` 还原成空格。
* **颜色透明度** `bg-black/40`、`bg-slate-900/80`。
* 工具类只作用于控件本身；`type::part`（`select::option`、`slider::thumb`）
  仍然只能用 `.css` 选择器描述。

## 支持的工具类

| 类别 | 拼写 |
|------|------|
| 显示 / 定位 | `flex` `inline-flex` `block` `hidden` `static` `relative` `absolute` `fixed` `sticky` `overflow-hidden/auto/scroll/visible` `visible` `invisible` |
| flex | `flex-row` `flex-col` `flex-wrap` `flex-nowrap` `items-start/center/end/stretch` `justify-start/center/end/between/around/evenly` `flex-1` `flex-none` `grow` `grow-0` `order-<n>` |
| 间距 | `p{,x,y,t,r,b,l}-<n>` `m{,x,y,t,r,b,l}-<n>`（可带负号 `-mt-2`）`gap-<n>` `gap-x-<n>` `gap-y-<n>` `space-x-<n>` `space-y-<n>` |
| 尺寸 | `w-<n>` `h-<n>` `size-<n>` `w-full` `w-1/2` `w-auto` `min-w-<n>` `min-h-<n>` `max-w-<n>` `max-w-md`（T 恤码）`max-h-<n>` |
| 排版 | `text-xs..text-9xl` `text-left/center/right` `font-thin..font-black` `leading-<n>` `leading-tight/normal/loose` `tracking-tight/wide/widest` `uppercase` `lowercase` `normal-case` `truncate` `whitespace-nowrap` `align-top/middle/bottom` |
| 颜色 | `bg-<色>` `text-<色>` `border-<色>` `accent-<色>` `shadow-<色>`；色板为 Tailwind 默认调色板（`slate gray zinc neutral stone red orange amber yellow lime green emerald teal cyan sky blue indigo violet purple fuchsia pink rose` × `50..950`）加 `white` `black` `transparent` |
| 渐变 | `bg-gradient-to-{t,r,br}` + `from-<色>` `via-<色>` `to-<色>` |
| 边框 / 圆角 | `border` `border-<n>` `border-{t,r,b,l,x,y}[-<n>]` `border-{t,r,b,l}-<色>` `border-solid/dashed/dotted` `rounded[-none/sm/md/lg/xl/2xl/3xl/full]` `rounded-{t,r,b,l,tl,tr,br,bl}-<档>` |
| 效果 | `shadow[-sm/md/lg/xl/2xl/inner/none]` `opacity-<n>` `backdrop-blur[-sm/md/lg/xl/2xl/3xl]` |
| 动效 | `transition` `transition-none` `duration-<ms>` `ease-linear/in/out/in-out` `scale-<n>` `translate-x-<n>` `translate-y-<n>` `rotate-<n>` `animate-spin/pulse/breath/shimmer/float/glow/aurora/motes` |
| 其他 | `top/right/bottom/left-<n>` `inset-<n>` `inset-x/y-<n>` `z-<n>` `columns-<n>` `cursor-pointer/text/move/...` |

## 与 Tailwind 的差异

引擎的样式模型比浏览器小，因此几处刻意的近似：

* 行高和字距按像素存：`leading-tight` 等比例关键字按 16px 正文换算
  （`leading-tight` = 20px），`tracking-wide` = 1px。
* 渐变只有四个方向（下、右、右下、上），最多三个色标；`bg-gradient-to-l`
  这类不支持的方向退化为向下。
* `shadow-*` 是单层阴影（引擎的阴影模型），不是 Tailwind 的双层阴影。
* `animate-*` 映射到引擎内置的关键帧，名字见 `Style.AnimKind`。
* `w-screen`、`w-dvh` 视作 `100%`；`mx-auto`、`shrink-*`、`ring-*`、
  `divide-*`、`italic`、`underline` 等没有对应字段的类被忽略。
