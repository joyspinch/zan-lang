# Zan 语言基础 · 讲解片 (66s)

> 一镜到底 · 锁死正交机位 · 无缝循环 · HUD 全部画进画布（导出天然带字幕）
> 产物：`index.html`（单文件，Three.js 场景 + WebCodecs 逐帧导出）

对标的不是品牌片（`video/start/`），而是**教学片**：每个知识点先给可编译的真代码，
再落一句人话结论。所有代码、API 名、限定条件都取自仓库当前实测口径
（`docs/SPEC.zh-CN.md`、`docs/STDLIB.md`、`README.zh-CN.md`），不写没把握的特性。

---

## 1. 全片结构（8 段，7 个知识点）

| 时间 | 段落 | 画面主体 | 字幕 |
|---|---|---|---|
| 0.0–5.4 | 概览 | 手绘字标 `zan` + 三个锚点 chip | **Zan 语言基础** `面向系统级、桌面与实时应用的强类型语言。` |
| 5.4–13.4 | 01 程序结构 | HelloWorld 十行，逐行"长"出来 + 光标 | **命名空间即目录，using 按需拉入源码。** `using System; · namespace HelloWorld; · static void Main(string[] args)` |
| 13.4–21.4 | 02 类型系统 | 左右双卡：值类型（栈上）/ 引用类型（堆上，ARC） | **值类型栈上分配，引用类型堆上由 ARC 托管。** `var 局部推导 · int? score = null · score ?? 0 · where T : struct` |
| 21.4–29.4 | 03 面向对象 | `class Person : IGreeter` 六行 + 右侧特性清单 | **单继承 + 多接口；属性 get/set；virtual / override / sealed。** `class · struct · interface · enum · public / private / protected / internal` |
| 29.4–37.4 | 04 控制流 | foreach / switch 类型模式 + `when` / `using` | **switch 支持类型模式与 when 保护；using 离开作用域自动 Dispose。** `if / else · foreach · while · do-while · try / catch / finally · lock` |
| 37.4–45.4 | 05 内存 · ARC | 三个 `ref` 引用逐个脱落 → 盒子 refcount 归 0 → 金色"立即析构"；下方 strong/weak 循环图 | **引用计数归零即刻析构 —— 没有 GC，没有停顿。** `weak 打破循环引用 · using 确定性释放 · 无悬挂指针` |
| 45.4–53.4 | 06 异步 · 互操作 | `async/await` 四行 + `[DllImport]` 三行 + 右侧 chip | **async/await 协程调度；DllImport 免头文件直调 C 动态库。** `async → CPS 转换 · Channel<T> 并发通道 · [DllImport] 原生互操作` |
| 53.4–61.4 | 07 标准库 · 界面 | 左卡：源码级标准库清单；右卡：HTML/CSS 标记 → `zanc` → 原生窗口 | **标准库是源码，按需拉入；界面用 HTML/CSS 声明，编译成原生窗口。** `stdlib/*.zan · packages/Zan.Gui · LLVM AOT · 七个原生目标` |
| 61.4–66.0 | 落版 | 字标 + 三行定位 | **强类型 · C# 风格** / `LLVM AOT · ARC 确定性内存` / `一份源码 · 七个原生目标` |

左右两侧常驻：左侧 7 项章节索引（当前项高亮），顶部 `ZAN · 语言基础` 与帧率/时长角标，底部进度条。

---

## 2. 为什么这么剪

- **先代码，后结论。** 语言教学片的可信度来自屏幕上真的是一段能编译的代码，不是形容词。
  每一章都是「代码逐行长出来 → 一句话收口」，观众的眼睛有固定落点。
- **代码位置全程不动。** 横版 `y0 = 322`（内容区 620→1800），竖版 `y0 = 700`。
  章与章只做 42px 的横向滑入滑出 + 淡入淡出，避免"翻幻灯片"的眩晕。
- **类型系统用双卡对置，ARC 用计数脱落。** 这两个是 Zan 与主流 GC 语言差别最大的地方，
  必须给"看得见的证据"：一个并排对照，一个从 3 数到 0。
- **只留两个颜色锚点。** 薄荷青 `#9af7d3`（关键字 / 强调 / 引用）与火花金 `#ffc838`
  （字符串字面量 / 引用类型 / 析构瞬间），其余全是中性灰阶。和 `video/start/` 同一套调性。
- **不写没把握的话。** `Console`/`Math`/`List<T>` 是编译器内建（`builtin_api.c`），
  标准库是 `.zan` 源码按需拉入 —— 这两条写进了屏幕。

---

## 3. 技术要点（改片子前先读）

- **所有动画都是 `t` 的解析函数**：`sin` / `smoothstep` / `env`。禁止累加、禁止 `Math.random()` 参与动画
  （随机只用于初始化星场布局，种子固定 `20261019`）。所以 `t=0` 与 `t=D` 天然同态，`__seek(t)` 逐帧可复现。
- **HUD 画在画布里**：字幕/章节索引/品牌/进度条全部是 canvas 纹理贴到正交平面的。
  DOM 覆盖层在 `VideoFrame` 抓取里会全部丢失 —— 这是"导出的片子没字"的头号原因。
- **`?encode=1` 时与窗口解耦**：`setPixelRatio(1)` + `setSize(EW, EH, false)`，
  画布缓冲区固定成导出尺寸，屏幕怎么缩放都不影响产物。
- **正交相机 + 设计单位**：视口高固定 2 个单位，宽 = 2×画幅比；`1920×1080` 与 `1080×1920`
  只换 `L`（锚点表）与几个 `PORT ? … : …` 分支，场景不重写。
- **真实景深**：HUD 平面 `transparent: true`，只有文字处不透明；340 个 `THREE.Points` 星尘放在平面**之后**，
  从透明区透出来，再经 `UnrealBloomPass` 起辉。这是全片唯一的"3D 感"来源 —— 克制，且不与文字抢戏。
- **导出只用 WebCodecs**：渲染一帧 → `new VideoFrame(canvas)` → `VideoEncoder`(avc1) → `mp4-muxer`。
  不用 `MediaRecorder`（墙钟时间戳，窗口被遮挡/掉帧会丢内容）。

## 4. 运行与出片

```powershell
# 预览（浏览器直接打开，自动循环播放）
start video\basics\index.html

# 抽帧复核：?shot=<秒>   竖版：加 &ratio=9:16
# start "video\basics\index.html?shot=21.5"

# 出片（页面带 ?encode=1 会逐帧编码并自动下载 MP4）
# 横版 1920×1080@60 / 竖版 1080×1920@60
node "C:\Users\<user>\.agents\skills\promo-video\scripts\encode.mjs" video\basics 60 1920 1080
node "C:\Users\<user>\.agents\skills\promo-video\scripts\encode.mjs" video\basics 60 1080 1920
```

产物落在 `video/basics/export/`。

## 5. 验收清单

- [x] 8 个关键时间点抽帧与分镜表一致（`?shot=`）
- [x] `?shot=0` 与 `?shot=D` 同构（残差仅来自胶片颗粒与星尘闪烁，PSNR ≈ 35 dB）
- [x] 竖版 `?ratio=9:16` 单独复核：章节索引改为横向数字条，卡片上下堆叠，无裁切/压扁
- [x] 字幕与品牌都在画面里（HUD 在画布内，非 DOM）
- [x] 静音看一遍能读出"Zan 是什么 + 六个基础知识点"
