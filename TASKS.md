# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：`[ ]` 未完成 · `[~]` 进行中 · `[-]` 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 `[x]` 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出

## 未完成

### B-ID118 [ ] GameViewport 离屏渲染（逻辑/渲染彻底解耦的最后一块）
插值渲染（GameClock.InterpolationAlpha + Entity.PrevX/PrevY + Scene.Render 双线性）与逻辑阶段统一驱动（GuiHost.AttachViewport → StepLogic，2026-10-07 已落地）已就位；剩余：游戏画面渲染进持久离屏位图、GameViewport.OnPaint 只做贴图合成——游戏帧率与 GUI 帧率解耦、游戏可独立线程渲染。前置：确认运行时离屏表面/渲染目标原语的可用面（Canvas 是否暴露 render-target），不足则先补运行时 API。未做原因：本Sessions运行时有其他会话在途改动，不宜动 src/runtime。

### B-ID119 [~] GUI 控件级脏标记 + partialFrames 自动化
已落地主干（2026-10-08）：RenderTreeInner 每控件自段置换命令流累加器，Canvas 28 个绘制原语入口折 FNV 哈希，自段出口与上帧存档比对、条带未盖住即 NoteDamage 补画；哈希只数"会画出的命令"、与条带无关故整帧/条带帧存档可比，partialFrames 关闭的 App 每原语仅多一次静态布尔读。探针 E2E（悬停条带帧里改 label 文本不声明任何损伤，两帧内上屏、其余区域字节级不变）通过。尾差：blur 槽/快照/原生层走各自既有机制未入哈希；新增 Canvas 原语须记得补喂入口。

### B-ID120 [ ] GUI 全量图层合成（背景/控件/游戏/HUD 四层离屏）
背景层主题切换才重画、控件层控件脏才重画、游戏层吃 B-ID118 产出、HUD 层数据变才重绘；帧合成只做 alpha 叠加。blurSlots 缓存是该思路的局部实现，推广到整个渲染管线。依赖：B-ID119 的脏标记已就位（2026-10-08），剩运行时多离屏表面支持（与 B-ID118 同一前置）。

### B-ID121 [ ] SpriteBatch 运行时批量提交后端（每类型一次 FFI）
Game.Kit.SpriteBatch 命令缓冲已落地（预分配 SOA、类型归组、零分配），但提交侧仍是每图元一次 Canvas 调用——运行时没有纯色图元批量入口（Gui.Rendering 的 DrawSprites 只覆盖纹理四边形）。缺：`zan_canvas_fill_rects/fill_circles/fill_radials(canvas, data, count)` 批量 FFI + Canvas 包装，End() 改每类型一次提交。未做原因同 B-ID118（不动在途运行时）；落地后 GPU 后端只需在批量入口处接顶点批。

### B-ID122 [~] 静态发布（--link-mode static）Postgres 驱动的 OpenSSL 静态库：win-x64 已补，linux-x64/macos 待补
真引用 Postgres 的程序静态发布时，libpq.a 的 pq.libs 索要 `-lssl -lcrypto`（libpq 这个第三方 C 库内部用 OpenSSL 做 TLS，与 Zan 自身的纯 Zan TLS 无关），缺档即链接死于 `cannot find -lssl`。**win-x64 已解决（2026-10-08）**：vendored MSYS2 `mingw-w64-x86_64-openssl 3.6.5-1`（msvcrt ABI，Apache-2.0）的 libssl.a/libcrypto.a 入 `drivers/win-x64/static/`，llvm-nm 符号预检 17/17 覆盖，`--publish --link-mode static` 探针链接过、运行优雅（详见 zan-compiler-internals 的补档定式）。剩余：**linux-x64**（libpq.a 是 musl 构建，须配 Alpine 静态 OpenSSL，glibc 源不通用）、**macos**（连静态 libpq.a 本身都缺，只有 dylib，需先出静态 libpq + krb5 全套）。2026-10-07，oneplus app 发布排查中发现（当时与幻影拉入混叠，幻影已根治）。

