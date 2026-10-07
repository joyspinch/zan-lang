# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：`[ ]` 未完成 · `[~]` 进行中 · `[-]` 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 `[x]` 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出

## 未完成

### B-ID118 [ ] GameViewport 离屏渲染（逻辑/渲染彻底解耦的最后一块）
插值渲染（GameClock.InterpolationAlpha + Entity.PrevX/PrevY + Scene.Render 双线性）与逻辑阶段统一驱动（GuiHost.AttachViewport → StepLogic，2026-10-07 已落地）已就位；剩余：游戏画面渲染进持久离屏位图、GameViewport.OnPaint 只做贴图合成——游戏帧率与 GUI 帧率解耦、游戏可独立线程渲染。前置：确认运行时离屏表面/渲染目标原语的可用面（Canvas 是否暴露 render-target），不足则先补运行时 API。未做原因：本Sessions运行时有其他会话在途改动，不宜动 src/runtime。

### B-ID119 [ ] GUI 控件级脏标记 + partialFrames 自动化
App.zan 的 partialFrames 已有矩形损伤裁剪但依赖调用方手动上报；自动化需要"每控件绘制命令记录 + 帧间内容哈希"基础设施（与 Game.Kit.SpriteBatch 命令缓冲同族），控件 OnPaint 出口比对哈希、不同才上报 DamageRect。改动面：App.zan（6500+ 行）全部控件绘制出口。

### B-ID120 [ ] GUI 全量图层合成（背景/控件/游戏/HUD 四层离屏）
背景层主题切换才重画、控件层控件脏才重画、游戏层吃 B-ID118 产出、HUD 层数据变才重绘；帧合成只做 alpha 叠加。blurSlots 缓存是该思路的局部实现，推广到整个渲染管线。依赖 B-ID119 的脏标记与运行时多离屏表面支持。

### B-ID121 [ ] SpriteBatch 运行时批量提交后端（每类型一次 FFI）
Game.Kit.SpriteBatch 命令缓冲已落地（预分配 SOA、类型归组、零分配），但提交侧仍是每图元一次 Canvas 调用——运行时没有纯色图元批量入口（Gui.Rendering 的 DrawSprites 只覆盖纹理四边形）。缺：`zan_canvas_fill_rects/fill_circles/fill_radials(canvas, data, count)` 批量 FFI + Canvas 包装，End() 改每类型一次提交。未做原因同 B-ID118（不动在途运行时）；落地后 GPU 后端只需在批量入口处接顶点批。

