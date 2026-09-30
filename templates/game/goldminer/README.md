zan Gold Miner — 黄金矿工（模板版）
====================================

玩法
----
- 钩子自动摆动，点击 / 空格 / 下键 / 触屏点按 放钩
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
- 鼠标点击 / 空格 / ↓ / 回车 / 触屏点按   放钩、QTE 节拍点
- Q / E                 使用（消耗）炸药：钩住重物拉不动时直接炸掉
- Esc                   结算画面退出（存档）；关窗即存档退出

手机上玩
--------
```bash
build/zanc templates/game/goldminer/src/main.zan templates/game/goldminer/src/Game.zan \
  --auto-stdlib \
  --publish --target android-arm64 --emit-apk goldminer.apk \
  --apk-package com.example.goldminer --apk-label 黄金矿工
```
`--publish` 会把 assets/ 内嵌进 libmain.so（磁盘优先、内嵌兜底的加载
逻辑不变），APK 直装手机即可；横竖屏都能玩——设计分辨率 960×720 经
等比铺满，竖屏上下留黑边，点按屏幕任意位置即放钩。

界面架构
--------
GuiHost 版（Game.Foundation.Gui）：窗口/事件/呈现由 packages/Zan.Gui/src/Gui 原生
外壳承担，不依赖 SDL。全部画面走 Canvas 直绘：

- 场景层（src/main.zan 的 DrawAll）：背景、矿工、抓钩（矢量三爪钩，
  随摆角旋转——画布暂无旋转贴图）、物品、粒子、飘字、QTE 圈，
  经 Game.Kit 的 CDraw/CText 绘制；
- HUD 层（DrawFallbackHud）：半透明顶栏（关卡、金币进度、厘秒
  倒计时、连击、炸药）与结算/拆弹面板，同一套 CText 场景层自绘，
  信息始终可读。
- 音频：System.Audio 原生驱动（AudioClip）；打开失败自动哑火，
  不影响运行。

构建
----
仓库根编译（产物 build/goldminer.exe，运行所需 DLL 自动落在
同目录）。assets/ 不需要任何编译指令：只要代码引用了内嵌读取
API（File.EmbedExists 等），zanc 检测到源文件旁的 assets/ 目录
就把整个目录自动内嵌进 exe（资源名 "assets/<文件>"，与代码里的
查找路径一致）：

    build\zanc.exe templates\game\goldminer\src\main.zan ^
                   templates\game\goldminer\src\Game.zan ^
                   --auto-stdlib ^
                   -o build\goldminer.exe

独立 exe 发布（单文件、免第三方 DLL、双击无控制台窗口）
------------------------------------------------
加 `--publish --link-mode static --subsystem windows`，并设为 GUI
子系统（不弹控制台）；`--subsystem windows` 必须显式给，zanc 命令行
默认 console 子系统（IDE 会对 GUI 项目自动加，CLI 不探测）：

    build\zanc.exe templates\game\goldminer\src\main.zan ^
                   templates\game\goldminer\src\Game.zan ^
                   --auto-stdlib ^
                   --publish --link-mode static --subsystem windows ^
                   -o dist\goldminer\goldminer.exe

素材
----
assets/ 下全部 PNG/JPG/WAV 均可替换；编译时整个目录自动内嵌进
exe。运行时先读 exe 旁的 assets/（磁盘优先，替换即生效），没有
磁盘副本就直接从 exe 镜像里读内嵌字节进内存（File.ReadAllBytes →
LoadImageFromMem / LoadWavFromMem），全程不落盘，单文件分发也
出声出图。
IDE「新建项目」复制本模板后 assets/ 与 src/ 同级，同样自动内嵌，
无需额外参数。
注意：自动内嵌靠"入口源文件的目录"定位 assets/，所以命令行里要
写带目录的入口路径（如上）；在项目根目录下裸写 `zanc main.zan`
时编译器只能看到 `.`，会定位不到 assets/——换绝对路径或带
`src\` 前缀即可。

文件
----
- src/main.zan    入口 + 场景渲染/输入/兜底 HUD/无头自测
                  （MINER_SIM=1 平衡仿真，不建窗口）
- src/Game.zan    核心玩法：摆钩、抓取、物品表、连击/QTE/炸弹、关卡与存档
- zan.proj        IDE 项目文件（IDE「新建项目 → 游戏」可直接套用）
- template.manifest 模板清单（IDE 新建项目对话框展示）
