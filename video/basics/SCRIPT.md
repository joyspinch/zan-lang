# Zan 语言基础 · 讲解片 (66s)

> 一镜到底 · 锁死正交机位 · 无缝循环 · HUD 全部画进画布（导出天然带字幕）
> **主交付：竖版 `1080×1920 @30`**；横版 `1920×1080 @30` 同一份源码、同一套内容。
> 产物：`index.html`（单文件，Three.js 场景 + WebCodecs 逐帧导出）+ `export/*.mp4`（不入库，见 `.gitignore`）

对标的不是品牌片（`video/start/`），而是**教学片**：每个知识点先给可编译的真代码，
再落一句人话结论。所有代码、API 名、限定条件都取自仓库当前实测口径
（`docs/SPEC.zh-CN.md`、`docs/STDLIB.md`、`README.zh-CN.md`），不写没把握的特性。

---

## 1. 全片结构（8 段，7 个知识点）

| 时间 | 段落 | 画面主体 | 字幕 |
|---|---|---|---|
| 0.0–5.4 | 概览 | 手绘字标 `zan` +三个锚点 chip | **Zan 语言基础** `面向系统级、桌面与实时应用的强类型语言。` |
| 5.4–13.4 | 01 程序结构 | HelloWorld 十行，逐行"长"出来 + 光标 | **命名空间即目录，using 按需拉入源码。** `using System; · namespace HelloWorld; · static void Main(string[] args)` |
| 13.4–21.4 | 02 类型系统 | 左右双卡：值类型（栈上）/ 引用类型（堆上，ARC） | **值类型栈上分配，引用类型堆上由 ARC 托管。** `var 局部推导 · int? score = null · score ?? 0 · where T : struct` |
| 21.4–29.4 | 03 面向对象 | `class Person : IGreeter` 六行 + 特性清单 | **单继承 + 多接口；属性 get/set；virtual / override / sealed。** `class · struct · interface · enum · public / private / protected / internal` |
| 29.4–37.4 | 04 控制流 | foreach / switch 类型模式 + `when` / `using` | **switch 支持类型模式与 when 保护；using 离开作用域自动 Dispose。** `if / else · foreach · while · do-while · try / catch / finally · lock` |
| 37.4–45.4 | 05 内存 · ARC | 三个 `ref` 引用逐个脱落 → 盒子 refcount 归 0 → 金色"立即析构"；下方 strong/weak 循环图 | **引用计数归零即刻析构 —— 没有 GC，没有停顿。** `weak 打破循环引用 · using 确定性释放 · 无悬挂指针` |
| 45.4–53.4 | 06 异步 · 互操作 | `async/await` 四行 + `[DllImport]` 三行 + chip | **async/await 协程调度；DllImport 免头文件直调 C 动态库。** `async → CPS 转换 · Channel<T> 并发通道 · [DllImport] 原生互操作` |
| 53.4–61.4 | 07 标准库 · 界面 | 左卡：源码级标准库清单；右卡：HTML/CSS 标记 → `zanc` → 原生窗口 | **标准库是源码，按需拉入；界面用 HTML/CSS 声明，编译成原生窗口。** `stdlib/*.zan · packages/Zan.Gui · LLVM AOT · 七个原生目标` |
| 61.4–66.0 | 落版 | 字标 + 三行定位 | **强类型 · C# 风格** / `LLVM AOT · ARC 确定性内存` / `一份源码 · 七个原生目标` |

常驻 HUD：横版左侧 7 项章节索引（当前项高亮）；竖版顶部横向数字条 `01…07`；
顶部品牌角标 + 画幅/时长，底部进度条。

## 2. 为什么这么剪

- **先代码，后结论。** 语言教学片的可信度来自屏幕上真的是一段能编译的代码，不是形容词。
  每章都是「代码逐行长出来 → 一句话收口」，观众的眼睛有固定落点。
- **代码位置全程不动。** 横版 `y0 = 322`（内容区 620→1800），竖版 `y0 = 700`。
  章与章只做 42px 横向滑入滑出 + 淡入淡出，避免"翻幻灯片"的眩晕。
- **字幕在章节过半处进**（`t0+4.4 ~ +4.6`，ARC 那章 `+5.2`）。整章 8s，太晚进字幕
  会让前 4s 变成"只有代码没有结论"。
- **类型系统用双卡对置，ARC 用计数脱落。** 这两个是 Zan 与主流 GC 语言差别最大的地方，
  必须给"看得见的证据"：一个并排对照，一个从 3 数到 0。
- **只留两个颜色锚点。** 薄荷青 `#9af7d3`（关键字 / 强调 / 引用）与火花金 `#ffc838`
  （字符串字面量 / 引用类型 / 析构瞬间），其余全是中性灰阶。与 `video/start/` 同一调性。
- **不写没把握的话。** `Console`/`Math`/`List<T>` 是编译器内建（`builtin_api.c`）、
  标准库是 `.zan` 源码按需拉入 —— 这两条直接写进屏幕。

## 3. 技术要点（改片子前先读）

- **所有动画都是 `t` 的解析函数**：`sin` / `smoothstep` / `env`。禁止累加、禁止 `Math.random()`
  参与动画（随机只用于初始化星场布局，种子固定 `20261019`）。所以 `t=0` 与 `t=D` 天然同态，
  `__seek(t)` 逐帧可复现。
- **HUD 画在画布里**：字幕/章节索引/品牌/进度条全部是 canvas 纹理贴到正交平面上。
  DOM 覆盖层在 `VideoFrame` 抓取里会全部丢失 —— 这是"导出的片子没字"的头号原因。
- **`?encode=1` 时与窗口解耦**：`setPixelRatio(1)` + `setSize(EW, EH, false)`，
  画布缓冲区固定成导出尺寸，屏幕怎么缩放都不影响产物。
- **正交相机 + 设计单位**：视口高固定 2 个单位，宽 = 2×画幅比；`1920×1080` 与 `1080×1920`
  只换 `L`（锚点表）与几处 `PORT ? … : …` 分支，场景不重写。
- **真实景深**：HUD 平面 `transparent: true`，只有文字处不透明；340 个 `THREE.Points` 星尘放在
  平面**之后**，从透明区透出来，再经 `UnrealBloomPass` 起辉。全片唯一的"3D 感"来源 —— 克制，不抢戏。
- **导出只用 WebCodecs**：渲染一帧 → `new VideoFrame(canvas)` → `VideoEncoder`(avc1) → `mp4-muxer`。
  不用 `MediaRecorder`（墙钟时间戳，窗口被遮挡/掉帧会丢内容）。

### 3.1 两个踩过的坑（必读，改导出必看）

1. **`await requestAnimationFrame(...)` 会让编码循环永久挂死。**
   浏览器对不可见 / 被遮挡的窗口停发 rAF，那个 await 再也不会 resolve ——
   表现为进度停在某个帧号（本片第一次跑到 `2170 / 3960` 就没了），既不报错也不出片。
   解法：`yieldFrame()` 让 **rAF 与 `setTimeout(200)` 赛跑**，谁先触发算谁的；
   同时给 Chrome 加 `--disable-backgrounding-occluded-windows --disable-renderer-backgrounding
   --disable-background-timer-throttling`。
2. **编码队列不背压会把 codec 顶爆**，报 `Cannot call 'encode' on a closed codec`。
   解法：每帧 `encode()` 后 `while (encoder.encodeQueueSize > 8) await sleep(2)`
   —— 让编码器反过来限流渲染，比"每隔几帧主动让出"既稳又快。
   加上这条后 1980 帧从"跑不完"变成 **一分钟内出片**。
3. 顺带：`git-for-windows` 的 `ffmpeg` 已无 `-vsync`，抽帧改 `-fps_mode passthrough`。

## 4. 运行与出片

```powershell
# 预览（浏览器打开即自动循环播放）
start video\basics\index.html

# 抽帧复核：?shot=<秒>；竖版加 &ratio=9:16；短样片加 &dur=8（只留前 8 秒，调试用）
# start "video\basics\index.html?shot=28&ratio=9:16"

# 出片（页面带 ?encode=1 会逐帧编码并自动下载 MP4 到 export/）
node video\basics\encode.mjs video\basics 30 1080 1920   # 竖版（主交付）
node video\basics\encode.mjs video\basics 30 1920 1080   # 横版
node video\basics\encode.mjs video\basics 8  1080 1920   # 8 秒竖版样片（调试）
```

> `encode.mjs` 与 `promo-video/scripts/encode.mjs` 同流程，但：ratio 显式传 `9:16`/`16:9`
> （页面据此切版式）、超时放宽到 20 分钟（66s×60fps 跑不完 skill 版的 180s）、
> 禁用窗口遮挡节流。窗口尺寸不影响产物 —— 画布缓冲区在 `?encode=1` 下固定成导出尺寸。

## 5. 验收清单

- [x] 横/竖两版各 8 个关键时间点抽帧与分镜表一致（`?shot=`）
- [x] `?shot=0` 与 `?shot=D` 同构（残差仅胶片颗粒 + 星尘闪烁，PSNR ≈ 35 dB）
- [x] **竖版逐帧复核**：章节索引为横向数字条，卡片上下堆叠，代码/字幕无裁切压扁
- [x] MP4 回抽帧确认字幕与品牌都在画面里（HUD 在画布内，非 DOM）
- [x] `ffprobe`：竖版 `1080×1920` / 横版 `1920×1080`，`30 fps`，`66.000 s`，`1980` 帧
- [x] 静音看一遍能读出"Zan 是什么 + 六个基础知识点"
