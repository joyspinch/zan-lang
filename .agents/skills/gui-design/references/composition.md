# 页面搭配模式与反模式

SKILL.md 是规则主干;这里是可以直接照抄的组合模式(全部用 stdlib/Gui
真实 API)与历史上真实翻过车的反模式。

## 页面解剖(从外到内)

```text
┌─ RenderChrome(标题栏,App 负责)──────────────┐
│ ToolStrip(工具条,small 档,px-2)             │
│ ┌──────────────────────────────────────────┐ │
│ │ 内容区:一行一节,节间 gap-large(12)      │ │
│ └──────────────────────────────────────────┘ │
│ StatusBar(状态栏,tiny 字号,text-secondary)  │
└──────────────────────────────────────────────┘
```

- 工具条/状态栏用现成外壳(`Widget/ToolStrip.zan`、`Widget/StatusBar.zan`),
  不要用裸 Panel 拼一个像的。
- 内容区自上而下:区块标题(text-secondary、small、可全大写加字距)→
  内容 → 节间空隙。标题与内容的左边界共线。

## 常用模式

### 表单(标签列固定宽)

```zan
Panel form = Panel.Column().Gap(2);                 // 行距 8px(档位)
Panel row1 = Panel.Row().Gap(2);
row1.Class = "items-center";                        // CSS 类经 Control.Class 生效
Label lbl1 = new Label("名称:"); lbl1.Prefer(90, 0);  // 全表同一列宽
row1.With(lbl1).With(new Input());                  // 输入列吃剩余空间
```

多行表单:所有标签列同宽、所有输入框同高(默认 medium),主操作按钮
放在表单末行右对齐,与输入列右缘共线。

### 工具条(左功能右动作)

左组放导航/视图工具(ghost 变体),右组放动作;主按钮唯一、最右、
primary 变体,次动作 secondary/ghost。全部 small 档 + `px-2`。

### 卡片网格

```zan
Grid g = Grid.Of(3).Gap(12);                        // 或原子类 grid-cols-3 gap-3
g.With(new Card(...)).With(new Card(...)) ...;
```

卡片内边距统一 `p-3`,标题 small + text-secondary,正文 medium,
底部动作行右对齐。卡片高度由网格拉齐,不要逐卡 Prefer。

### 明细布局(上列表下详情)

`SplitPanel` 竖切;列表侧密度紧凑(small/tiny),详情侧标准密度。
两侧 padding 同值,切换选中项时只重画详情侧(损伤矩形)。

### 弹窗脚部

右侧按钮组:取消(ghost)在左,主按钮(primary)在右;与正文之间
`gap-large`,脚部自身 `p-3`。弹层底色不透明(select::popup 先例),
不要在弹层里再垫一层不透明底。

## 密度三档(同画面取一档)

| 档   | 卡片内边距 | 区块间隙     | 控件档  | 适用               |
| ---- | ---------- | ------------ | ------- | ------------------ |
| 舒适 | p-4(16)  | gap-large+16 | medium  | 展示页/设置页      |
| 标准 | p-3(12)  | gap-large    | medium  | 常规业务页         |
| 紧凑 | p-2(8)   | gap-medium   | small   | 工具条/表格/IDE    |

密度混用只允许发生在"主从"结构(紧凑列表 + 标准详情),不允许发生在
同级卡片之间。

## 反模式(每条都是真实修过的 bug)

- **每帧 new 交互控件** → WidgetId 漂移,拖拽/点击落不回控件。交互控件
  静态字段持有、初始化一次;纯静态展示行每帧重建无所谓
  (`gui_gallery.zan` Gallery.demo* 先例)。
- **覆盖层(水印/角标)在内容撑高的宿主里 0 高不显示** → 宿主必须
  `Prefer(0, 高度)` 定高(stack 面板不吃 CSS height);`Panel.Column`
  的样式类型是 `stack`,皮肤选择器写 `stack.xxx` 不是 `panel.xxx`。
- **OnMeasure 量得比 OnPaint 画的少** → 内容被裁/整批消失(Calendar
  300→340 的教训)。两条路径必须走同一 StyleBox/字号解析。
- **全局覆盖层(toast/Layer/photos)挂进某个页面分支** → 切页消失、
  隐形开态吞点击。全局状态每帧在页面之上渲染一次。
- **CellStyler/回调里硬编码浅色主题色** → 深色皮肤下不可读。回调
  `Bind(app)` 每帧从主题派生色。
- **动画帧里调 RequestRedraw()** → 整窗重绘打满 CPU。用
  `TakeDueFrame` 保住控件自己声明的损伤矩形;损伤矩形要覆盖整行区
  (行头/表头/合计随滚动的教训)。
- **自定义事件循环把 `PollEvent()` 当"有事件"** → 队列空时它立即返回,
  100% CPU 空转。空事件回退 `WaitEventTimeout(16)`;要用 UiDriver
  自动化就记得每帧 `UiDriver.Tick()`。

## 验证捷径

gallery 支持深链直达任一演示页 + 指定皮肤/语言/玻璃:

```bash
./gui_gallery Slider liquidglass zh     # 组件名 皮肤 语言
```

构建与启动的完整注意事项(含 Linux 环境坑)见 `testing-gui-gallery`
skill。改完先过自查清单,再换三个皮肤各截图看一眼。
