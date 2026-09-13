# html_gallery —— HTML 设计稿组件全量画廊

用 **.html 设计稿**（`data-kind` 协议）声明组件库全部可声明组件的演示：
**74 个组件、一张卡片一个、8 个分类页**，顶部 ToolStrip 点分类切换。
所有 UI 都在 `App.html` 里；`App.zan` 只做分类切换与启动自检，
一行手写建树都没有。

## 运行

```bash
build/zanc examples/html_gallery/App.html examples/html_gallery/App.zan \
    --auto-stdlib -o html_gallery.exe
html_gallery.exe
```

- **设计稿必须是编译输入的第一个文件**（zanc 从第一份设计文档合成
  窗口与控件字段）。
- 编译没加 `--subsystem windows`，故意保留控制台：启动自检会逐组件
  打印结果，最后一行 `SELFTEST fails=0 of 74` 即全部建成且类型正确
  （`data-kind` 拼错会静默落 Element 占位，自检让它变成可机检事实）。
  正式发布想要纯窗口，加 `--subsystem windows` 重编即可。

## 覆盖面

组件目录全量 78 个，本画廊展示 74 个，剔除 4 个并说明原因：

| 组件 | 原因 |
|---|---|
| `ButtonGroup` / `Popover` / `FormField` | 构造需要参数（立即模式助手/带类型参数），设计稿 `data-kind` 通道 today 造不出来 |
| `ChoiceGroup` | 设计稿通道缺陷：它是 RadioGroup/CheckboxGroup 的基类，无显式零参构造，GenForm 发射 `new ChoiceGroup()` 得到 labels/children 为 null 的半初始化实例（已挂账 TASKS，待修） |

## 这个示例教的事

- `data-kind="组件类名"` 直接实例化注册组件；`data-label` / 几何
  （`data-fx/fy/fw/fh`、`data-dock`、`data-span`）是建模键；
  `data-x-<键>='<JSON>'` 透传组件扩展属性（如 ToolStrip 的
  `data-x-options`）。
- code-behind 是 `partial class <设计稿 body id>` + `static void
  OnLoad(Form form)`：设计稿里每个带 id 的节点都是生成的同名字段
  （`Cats`、`Page0..7`、`DemoButton`…），直接引用即可。
- 自检通道（`Check(字段, 期望 Kind, id)`）是把"HTML 能不能渲染出
  组件"变成断言的定式，值得抄。
- 已知命名细节：`SelectBox` 的 `Kind()` 返回 `"Select"`（类名与
  Kind 名不一致）。
