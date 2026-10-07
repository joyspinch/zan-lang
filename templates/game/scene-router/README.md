# HTML 场景路由演示（scene-router）

一个窗口三个场景（`menu` / `play` / `over`），每个场景 = **一个 `.html`
文件 + 一个 Zan 场景类**，由 `Game.Foundation.Gui.SceneRouter` 负责切换
（Vue Router 的心智模型：`router.Go("play")`）。

这个模板演示的是"AI 友好"的游戏结构：

- **UI 屏整屏 HTML**：主菜单、结算界面的布局与文案全在
  `assets/scenes/*.html` 里，改 UI 不用碰 Zan 代码。
- **玩法场景 Canvas 直绘 + HTML HUD**：弹球世界在 `PlayScene.Render`
  用 `CDraw.*` 直绘（舞台逻辑坐标），顶部工具栏是 `play_hud.html`
  叠层（设备像素、鼠标 1:1、标准按钮交互）。
- **事件接线**：HTML 里写 `data-on-click="start"`，场景类在
  `OnWire` 用 `handlers.Add("start", () => ...)` 接线。
- **装载后抓控件**：`OnEnter(root)` 里 `root.Find(id)` 定位、
  `Element.SetText` 回填（结算页的得分）。

## 场景生命周期

```
Register(名字, 场景对象)      场景对象常驻复用，切走再切回是同一实例
    │
    ▼ 每次进场景
OnWire(handlers)   →  装载 HTML  →  OnEnter(root)
    │
    ▼ 每帧
Event / FixedStep / Update / Render(世界层)
    │
    ▼ 切走
OnExit()  →  旧 HTML 卸载
```

`Go()` 只入队、帧首生效：点击处理器是在事件/渲染路径里触发的，
当场换树会释放正在绘制的控件树。

## 运行

IDE 新建项目选本模板，或命令行：

```
zanc src/main.zan --auto-stdlib -o build/game
cd <项目根> && build/game      # 场景 HTML 按相对路径 assets/scenes/ 定位
```

发布（`--publish`）会把 `assets/scenes/` 嵌进 exe，单文件分发。

## 玩法

鼠标移动挡板接弹球，每接一次 +1 分且球加速；掉底进结算。
Esc：主菜单退出、玩法回主菜单。
