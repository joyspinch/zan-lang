# 放置矿工 (Idle Miner) 游戏模板

基于 Zan 标准库纯 `Gui` + `SpriteBatch` 批量精灵渲染 + `Gui.Animation` 动画时间轴打造的高性能放置挂机游戏模板。

## 特性亮点

1. **零闲置开销 (0% CPU Idle)**：平时界面不产生无谓的刷新循环，只有在点击、金币滚动、面板动画进行时才按需触发硬件渲染。
2. **硬件级合批精灵渲染**：金币抛射与火花特效使用 `SpriteBatch` + 离屏动态烘焙（`Canvas.BakeSprite`），一次 DrawCall 绘制成百上千个平滑渐变粒子。
3. **弹性缓动与时间轴驱动**：核心矿石点击触发 `Tween` + `EaseType.BackOut` 弹性回弹，统计弹窗平滑浮动，手感丰满。
4. **纯标准库组件架构**：基于 `Form`、`Panel`、`Button`、`Label`、`Progress` 快速构建企业级工业风界面。
