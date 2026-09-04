zan Gold Miner — 黄金矿工（模板版）
====================================

玩法
----
- 钩子自动摆动，点击 / 空格 / 下键 放钩
- 规定时间内挖满目标金额即过关，关卡目标逐关递增
- 刺激机制：
  * 连击 FEVER：3 秒内连续回收有价物，倍率 x1.1 → 最高 x2，抓到垃圾清零
  * 收线 QTE：拉重物时出现白圈，白圈缩到金圈时点击 = 立即加速（连击有奖励）
  * 抓到炸弹：环形倒计时内连点炸药图标（中心变大爆炸清屏，失败炸自己 -100）
  * TNT 连锁：炸掉周围物品，收益归零，屏幕震动
  * 最后 10 秒：红字警报 + 心跳音 + 边框闪烁
- 物品：金块大中小 / 钻石 / 宝箱 / 神秘袋（开出随机物）/ 石头 / 骷髅
  / TNT / 炸弹 / 金币袋

操作
----
- 鼠标点击 / 空格 / ↓   放钩、QTE 节拍点
- Q / E                 使用（消耗）炸药：钩住重物拉不动时直接炸掉
- F11                   全屏；关窗即存档退出

界面架构
--------
两层 UI，模板示范了 Zan 游戏的推荐分层方式：

- 场景层（src/main.zan 的 DrawAll）：矿工、抓钩、物品、粒子、
  飘字、QTE 圈等"长在游戏世界里"的演出，自绘进 SDL 画布；
- HUD 层（HudTop / HudOverlay + GameHud）：窗口顶栏（关卡、
  金币进度、倒计时、连击、炸药）与结算/拆弹面板用 stdlib Gui
  控件树搭建，由 GameHud 接管宿主窗口，把场景帧作底、Gui 树
  合成在上（stdlib/Game/Ui/Hud.zan）。配色/圆角/描边全部落在
  skins/gamehud/skin.css 皮肤里，改风格不改代码。
- MINER_NOHUD=1 启动可退回纯自绘 HUD 路径（对照/兜底）。

src/Kit.zan 是模板自带的 SDL 游戏小工具箱（宿主窗口、资源
定位、Rng、整数绘图、系统字体文本），自包含、不依赖任何
examples/ 下的共享模块，可随项目自由修改。

构建
----
仓库根编译（产物 build/goldminer.exe，运行所需 DLL 自动落在
同目录）。**--embed 必须带上**：游戏启动先读 exe 旁 assets/，
找不到就回退内嵌副本；不 embed 且目录里没有 assets/ 时贴图、
背景与音效全部加载失败（黑屏）：

    build\zanc.exe templates\game\goldminer\src\main.zan ^
                   templates\game\goldminer\src\Game.zan ^
                   templates\game\goldminer\src\Kit.zan ^
                   --auto-stdlib ^
                   --embed templates\game\goldminer\assets=assets ^
                   -o build\goldminer.exe

素材
----
assets/ 下全部 PNG/JPG/WAV 均可替换；代码按 "assets/<name>.<ext>" 相对 exe
向上查找（Assets.Find），找不到再读编译期 --embed 进 exe 的内嵌副本
（File.EmbedExists / File.ExtractEmbedded），单文件分发也出声出图。
IDE「新建项目」复制本模板后，assets/ 与 src/ 同级，用 zanc 编译时
记得同样加 `--embed assets=assets`。

文件
----
- src/Kit.zan     模板自带工具箱：Host 窗口/Assets/Rng/Draw/Text（无外部依赖）
- src/main.zan    入口 + 场景渲染/输入/GameHud 控件树 HUD/无头自测
                  （MINER_SIM=1 平衡仿真、MINER_SHOT=1 截图、MINER_NOHUD=1 关 HUD）
- src/Game.zan    核心玩法：摆钩、抓取、物品表、连击/QTE/炸弹、关卡与存档
- zan.proj        IDE 项目文件（IDE「新建项目 → 游戏」可直接套用）
- template.manifest 模板清单（IDE 新建项目对话框展示）
