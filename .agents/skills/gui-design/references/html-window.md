# HTML 窗口开发定式（.html 设计稿 = 唯一格式）

窗口的布局/内容/样式进设计稿，交互与数据在 code-behind。编译期
HTML 通道（GenForm/GenHtml）已整体删除——**设计稿不是编译输入**，
zanc 对 .html 显式拒绝；唯一通道是运行期装载（2026-10-31，B-ID44）。

## 页面范式：.html 文档 + .zan code-behind + 模型宿主

一个页面 = 一份文档（结构与样式）+ 一段 code-behind（数据与事件）+
一个模型（JsonValue）。**不是"装载文档 + 命令式填值"**——那样
列表要手搭行模板、判断与绑定都写不进声明面。主窗口里的页面分区
同样适用（不必把页面做成独立子窗口）：

```zan
// 1) 装载文档（样式随装载并进 appCss，作用域类自动混入）
Control ui = Gui.Html.Load(app, html, null, ""); // html 来自文件/内嵌资源
ui.SetProp("style", "flex-grow: 1; min-height: 0;"); // host 使用 Flex 布局
host.Add(ui);                           // host 是已有的页面宿主
// 2) 挂模型宿主：事件按名字接线 + 绑定声明从此生效
ModelBinder b = new ModelBinder();
b.Handle("save", () => { ... });          // 无参
b.HandleArg("pick", (string s) => { });   // 带参（data-arg）
b.HandleSender("open-row", (Control c) => {
    object row = b.ScopeOf(c);            // 行内控件 → 行模型项（按实际模型类型使用）
});
b.SetRoot(ui, model);
b.Wire();
ModelHosts.Register(app, b);            // 主窗口的每帧泵驱动它
```

- **装载器不带处理器表**（`Gui.Html.Load(app, html, null, dir)`）：事件统一
  由 binder 的 `Wire()` 接线。装载时若也传 handlers，同一个控件会有两
  条订阅，一次点击触发两遍。
- **注册顺序：先 `Handle` 后 `SetRoot`**：早期 `SetRoot` 会重建处理器注册
  表，把之前注册的处理器全冲掉——页面照常渲染、按钮照常画出来，**点击却
  毫无反应且不报任何错**（坏的是看不见的部分，最难查）。现在 `SetRoot`
  只挂根与模型，注册表仅在为 null 时才建。
- **声明层自动安装**：`Gui.Html.Load` 经 `Html.Parse` 自动调用
  幂等的 `Install()`；页面入口不再调用 App 上的 HTML 装载方法。
  装载后仍须挂树；使用模型时还须注册 ModelBinder。
- **行点击要拿"是哪一行"**：用 sender 槽（`HandleSender`）+ binder 的
  `ScopeOf(control)`（取行模型项）；`data-arg` 只支持字面量，行内用不了。
  **行根也登记作用域**（2026-10-23 修）：`@click` 挂在 `<template data-for>`
  展开出来的**行根**上时，行根自己没有绑定声明，早期只给声明了绑定的控件登记
  作用域 → `ScopeOf(行根)` 返回 null → 处理器拿到 null 早退 → **行点击静默
  失灵**（界面正常、点击毫无反应、服务端一个请求都没发；点行内文字与点行留白
  都一样）。修在 `ModelBinder.ScopeRow`（行根无条件登记一次，撤行时对称摘除），
  conformance 用例 `gui_html_dynamic` 钉住：行根 `ScopeOf` 必须命中行模型项。
  排查这类"点了没反应"先量 `ScopeOf`：返回 null 就是登记面漏了，不是事件没接上。
- **列表用 `<template data-for>` + `data-key`**：行内 `data-bind` 以本项
  为第一作用域、回落根模型；`data-key` 走增量 Diffing（无 key 按长度
  整组重建）。要标准库组件的虚拟化/滚动就别用 data-for，改用 ListView
  绑数据 + 行模板（行模板是代码建的控件，样式只能放皮肤层）。
- **改模型后要出一帧**：帧泵每帧同步模型→界面，但窗口静止时不会自己
  出帧——改完模型记得 `app.RequestRedraw()`。

代码树也可配置 bindPath/bindProp 并挂同一 ModelBinder；模型路径不限于对话框或
JsonValue。`Binding<T>` 提供强类型字段直绑，两者都通过控件公开属性承载值，
不构成不同 UI 创作模式。

## 验证定式

- 纯逻辑（Html.Parse/绑定/同步）headless 可跑，无窗口：conformance
  形态的探针 + 金样（CRLF）。
- 布局几何：oracle 逐盒对比（同一份 .html 喂 Chrome 与引擎，
  getBoundingClientRect vs Arrange）。
- 手工编 GUI 程序：zanc 编全部 .zan（入口走 zan.proj `entry`）+
  `--subsystem windows`；设计稿 .html 不是编译输入（运行期加载，
  发布 `--embed`）。跑起来截图对照（锚定 PID），交互用
  UiDriver 可重复驱动。
- **真窗冒烟要带主窗骨架，裸页截图会"缺导航栏"**：页面文档只含
  页内骨架，图标导航栏在主窗文档里——把单页文档直接装进裸 Form
  截图，看到的永远是"少了左侧导航"（不是回归）。要看真实形态就
  复刻主窗装配（Gui.Html.Load 主窗文档 + 各页宿主挂页），跳过登录泵。
- **截屏三坑（当天连踩五次才抓到图）**：① 带中文注释的 `.ps1`
  以无 BOM UTF-8 落盘，PowerShell 5.1 按 ANSI 误解析，多字节序列
  会吞掉下一行语句——参数/环境变量"神秘变空"多半是这个，自动化
  ps1 只写 ASCII；② bash 的 `$!` 可能是包装进程——以 ProcessStartInfo
  返回的子进程 PID 枚举窗口，并用 GetWindowThreadProcessId 核对归属，
  不按名字批量关闭；遮挡时用 `PrintWindow`（flags=2）抓整窗；
  ③ 150% DPI 下先核对逻辑尺寸、物理客户区和缩放：只有证实冒烟窗口
  按物理尺寸错误打开，才可把右缘裁切归因于环境；修正宿主尺寸后仍须
  比较同 DPI 的整窗几何与像素，不能用 headless 通过豁免截图差异。
- **功能验收（不是页面加载）要走"真服务端 + 状态断言"三段式**：
  页面能打开 ≠ 功能通。① 用服务端自己的接口把数据种进去（发一条私信
  就给对方造出一条通知——`Im` 发信即 `Notify.Send`；建空间/建表同理）；
  ② 客户端点完，**回服务端查状态变了没有**（未读数、记录数、版本号），
  光看像素不够——像素只证明"画出来了"，不证明"写进去了"；
  ③ 点击坐标从 `dump tree` 的 JSON 里按控件名取矩形中心
  （**顶层是 `{"root":{...}}`**，不是直接一棵子树；`name` 为空的行要靠
  容器节点的 kids 顺序认）。驱动脚本是静态文件：坐标只能来自**上一次**
  运行的树 dump，所以"定位"与"点击"天然分两轮跑。
- **别用字节值拼中文测试数据**：`[char]0xE8` 这类写法把 UTF-8 字节当成
  码位，服务端收到的就是乱码——排查"乱码"前先确认**造数据**的那一步
  用的是码位（`[char]0x8054`）还是字节（`0xE8`）。

## 游戏接入边界

游戏 HUD、血条、伤害数字、小地图 UI、菜单与 SceneDesigner 使用标准
控件树（HTML/CSS 或声明式对象树）；像素绘制封在控件 `OnPaint` 内。
高频刷新通过长生命周期控件、变值更新、脏区和缓存优化，不建立应用侧
直绘 HUD 或自制命中区体系。游戏世界渲染保留 Zan.Game 的专用渲染路径。
此处是开发规则；旧调用方是否完成迁移须分别核对，不能由文档规范推断。

- **GuiHost 退出走宿主生命周期**：游戏回调调用 `host.RequestStop()`；不要只写 `host.App().isRunning = false`，因为 `GuiHost.Run` 由 `host.running` 控制循环，只改 App 标志不能结束宿主。原生验证须确认真实窗口收到退出输入后正常结束，并在超时 sentinel 前退出。
  Match3 限定证据：`build/test-out/gui-native-pair-20261113T084058.019920Z/match3-escape/run.json` 记录可见 HWND、exit0、空 stderr、无 driver/runtime diagnostics，棋盘选择边框改变30个白像素；driver log 在 Esc 后结束，未到 `ESC_NOT_HANDLED`。同批 `match3-discovery/initial.png` 已目检 HUD 正确。Survivor/FPS 虽迁移同一修法，尚无各自原生证据；GUI form 回调当前失败，不属于这项语义验收。


## 主题档案索引

- 建树通道、声明面原语与语法糖（@click 等）、动态绑定选路、模板实例化、切页三路：[html-window-binding.md](html-window-binding.md)
- 浏览器等价布局子集、CSS 两层与基线坑、高度归属、浮动件定位、CSS 合并隔离、双范式布局、复合控件布局：[html-window-layout.md](html-window-layout.md)
- 真取色器、属性行契约、所见所得设计器：[html-window-designer.md](html-window-designer.md)
- 文档与原生控件混排五坑、内嵌真网页（WebView）三道坎：[html-window-embed.md](html-window-embed.md)
- 声明式事件体系与组件事件契约、焦点通知与结构退休、关闭回调 ARC、关闭排空 Task：[html-window-events.md](html-window-events.md)
