# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。**凡在仓库中发现缺陷、缺口、未决设计，必须先在此登记编号再行动**；完成时标记为 `[x]` 或注明完成提交 / 闭账日期，并运行影响到的测试闭环。
>
> 历史清理记录：
> * 第一次清理（2026-08-03）：将已完成条目压缩为一行摘要。
> * 第二次清理（2026-08-08）：合并重复项，清理已作废的设计方案。
> * 第三次清理（2026-08-31）：清理已完成的 A56-A79 系列条目。
> * 第四次清理（2026-09-13）：清理 A80-A310 系列已闭账项，只保留未闭环项与长效对照表。
> * **第五次清理（2026-09-21）**：全仓已闭账项系统性归档压缩。A44 Web等价GUI全阶段(P0~P8)、近期折线图/图表全量sweep、ZanDb四轮优化、A320~A341(含A341 struct值rc生命周期、A340 postfix !、A327-14工作流化、A332发布体积全部6条肥边解耦)等已完工项全部收拢为单行摘要。真实长期规划（A32-4/5原生EH大里程碑、A50-2/3 CEF可选缺口、A91 CSS cursor、A263对照表、B9③等）完整保留。
>
> 状态标记：`[ ]` 未开始 · `[~]` 进行中 · `[x]` 已完成 · `[-]` 已作废

---

## 维护约定

* **编号规则**：`A` = 编译器 / 运行时 / 语言语义；`B` = 标准库 / 生态模块；`C` = 文档 / 工程规范；历史扩展沿用 `A<编号>`。
* **增补纪律**：发现新问题随手在对应大类末尾立账，写明现象、复现路径或最小探针。
* **销账纪律**：修复必须附带 conformance 测试用例；通过对应测试层后在条目中写明 commit hash 与完成日期。
* **归档纪律**：已完成的条目压缩为一行摘要（保留编号、一句话结论与日期，原详细排查过程进 git 历史，避免文档膨胀）。

---
# A. 编译器 / 运行时核心能力（历史基线）

* **A0** ✅ 数值类型对齐 C#（2026-07-28 全部完成）：无符号整数、溢出语义对齐。
* **A1** ✅ `Span<T>` 编译器内建（2026-07-28 全部完成）：内建只读/可读写切片，栈分配零开销。
* **A2** ✅ FFI ABI 分类与调用约定对齐（2026-07-29 全部完成）：Win64/SysV 结构体传参及返回值分类。
* **A3** ✅ `zanc bindgen` C 头文件绑定生成器（2026-07-29 全部完成）。
* **A4** ✅ 无运行时执行模式（2026-07-29 全部完成）：`-nostdlib` 极简 freestanding 支持。
* **A5** [-] SIMD 向量化（已评估作为可选扩展，暂不在核心管线实现）。
* **A6** ✅ 编译器对外 API（2026-07-30 全部完成）：驱动 IDE 与 lsp 自托管的前置支撑。
* **A7** ✅ 泛型深度完善（2026-07-27 全部完成）：多泛型参数、嵌套约束与特化代码生成。
* **A8** ✅ ARC / 异常 / 协程交互漏洞（2026-07-27 ~ 07-30 全部修复）：跨帧异常逃逸清理与循环引用解环。
* **A15** ✅ 语言缺口全仓审计（2026-07-27 起）：61 项语法缺口全部修复；A15-5 switch 枚举穷尽性/死分支预警已于 2026-09-20 经 `ad1d541f` 彻底收官。
* **A16** ℹ️ CSS 支持面（参考记录）：详见 `docs/CSS_SUPPORT.md`，支持属性子集与 Web 等价排版基准已对齐。

---
# B. 标准库模块重构与演进

* **B1** ✅ 大体量模块改造（2026-07-29 全部完成）：保留在 stdlib，消除对 C 运行时的过度依赖。
* **B2** ✅ 字节缓冲：收敛 "calloc 返回 string" 用法（2026-07-29 全部完成）。
* **B3** ✅ 提炼与消除结构性重复（2026-07-29 全部完成）：统一集合与基础算法。
* **B4** ✅ 平台与错误处理（2026-07-29 全部完成）：统一 `Result<T, E>` 与操作系统错误码映射。
* **B5** ✅ GUI 框架迁移（2026-08 全部完成）：全面迁移至组件树与保留模式渲染。
* **B6** ✅ 工具链 Zan 化（2026-08 全部完成）：IDE 与 LSP 核心逻辑自托管。
* **B7** ✅ Runtime 边界复核（2026-08 全部完成）：消除非必要 C 导出，稳定 ABI。
* **B8** ✅ 网络与异步 IO 模型重构（2026-09 全部完成）：单线程非阻塞 + 多 Worker 跨进程广播。
* **B9** ⏳ 数据持久化层（部分完成）：
  - [x] ① 事务作用域 `TransactionScope`（已完成）。
  - [x] ② 强类型参数化查询防注入（已完成）。
  - [ ] ③ `GenDb` 仓储实体代码生成（待排期实现）。

---
# A44 · Web 等价 GUI 布局与声明式体系（✅ 2026-09-12 全部闭账）

> 从命令式固定排版到 CSS/HTML 等价保留模式的重大技术演进。全部阶段（P0~P8）均已验收闭环：

* **P0 布局基建**：流式 Flow 块级排版、display (flex/block/inline-block/none)、border-box 计算与定位模型全部落地。
* **P1 盒模型精细化**：CSS margin 塌陷算法、margin: auto 居中计算、视口滚动与 BFC 格式化上下文隔离。
* **P2 字体与行盒**：Chrome/Skia 级字体度量基准、行内文本混排行盒（LineBox）拆分与基线对齐。
* **P3 Flexbox 弹性排版**：flex-direction / justify-content / align-items / flex-wrap 完整支持与伸缩因子计算。
* **P4 滚动条体系**：非侵入式滚动条内衬与贴靠、像素级平滑缓动动画滚动。
* **P5 HTML/CSS 声明层**：支持 `.html` 设计稿解析、选择器匹配优先级、层叠继承计算与运行时投影。
* **P6 设计器与双向同步**：可视化设计器统一，支持属性面板直通调节与代码双向热更新。
* **P7 模板与生态全量迁移**：全仓模板转换为原生 `.html` 声明式形态，消除旧命令式排版。
* **P8 收尾清账与 .zform 退役**：彻底退役 `.zform` 旧格式，实现 `data-if` / `data-for` / `data-arg` 等动态模版语法与 LSP 全面索引。

---
# A17-A42 历史修复与长期路线

## A17-A31 历史修复记录（全部完成，一行摘要）

* **A17** ✅ 结构体传参拷贝语义补齐（2026-07-30）。
* **A18** ✅ 泛型约束 `where T : struct/class` 类型检查（2026-07-30）。
* **A19** ✅ 字符串插值与转义序列边界 Bug 修复（2026-07-30）。
* **A20** ✅ 数组切片跨边界访问 Panic 保护（2026-07-30）。
* **A21** ✅ 多维数组索引重载语义对齐（2026-07-30）。
* **A22** ✅ 局部变量逃逸进入闭包的生命周期提升（2026-07-31）。
* **A23** ✅ 接口隐式转换与虚表派发开销优化（2026-07-31）。
* **A24** ✅ 嵌套泛型函数符号签名修饰冲突修复（2026-07-31）。
* **A25** ✅ 静态构造函数线程安全初始化屏障（2026-07-31）。
* **A26** ✅ 弱引用 `WeakReference<T>` 与循环引用打破机制（2026-07-31）。
* **A27** ✅ 枚举底层类型指定 (`enum E : byte/int`) 兼容（2026-07-31）。
* **A28** ✅ 空联合操作符 `??` 与可选链 `?.` 短路求值修补（2026-07-31）。
* **A29** ✅ 析构函数调用顺序保证与终结器队列优化（2026-08-01）。
* **A30** ✅ 动态类型转换 `as` 与类型测试 `is` 模式匹配基建（2026-08-01）。
* **A31** ✅ 异常栈回溯行号映射表压缩（2026-08-01）。

---

# A32 · 异常与异步运行时收尾路线（长期里程碑规划）

> 独立大版本架构升级规划，保持里程碑排期，不与常规缺陷混批。

* [x] **A32-0**：异常抛出时跨栈帧 ARC 栈展开基础修复（已完成）。
* [x] **A32-1**：协程任务调度状态机内存泄漏治理（已完成）。
* [x] **A32-2**：线程池任务丢弃异常捕获与 unhandled rejection 漏斗（已完成）。
* [x] **A32-3**：C/Zan 互操作边界异常安全边界屏障（已完成）。
* [ ] **A32-4 · await 同步完成 fast path 与无竞争握手（L）**：
  - 现象：`ValueTask` / 已就绪 `Task` 进入调度器仍有一次入队出队开销。
  - 目标：探测已完成状态直接内联执行 continuation，消除上下文切换开销。
* [ ] **A32-5 · LLVM 原生 EH 迁移并删除补偿层（XL，最高风险，单独里程碑）**：
  - 目前采用 setjmp/longjmp 补偿模型，开销较大且与 C++ 不兼容。
  - **A32-5a**：建立目标无关 EH 抽象层（Win64 SEH / Itanium DWARF EH / Wasm EH）。
  - **A32-5b**：LLVM landingpad 与 ARC 析构 cleanup 自动绑定。
  - **A32-5c**：跨 async 状态机断点的异常零成本传递。
* [ ] **A32-6 · macOS 实机、代码签名与公证发布门（M + 外部环境依赖）**：
  - 外部阻塞：需要配置 macOS CI 签名证书及 Apple Notarization 服务。

---

# A33-A42 修复与决策记录

* **A33** ✅ 委托支持实例方法组与闭包捕获（2026-08-04 完成）。
* **A34** ✅ 编译器支持共享库输出 (`.dll` / `.so` / `.dylib`)（2026-08-01 完成）。
* **A35** ✅ 结构体嵌套字段赋值越界防御（2026-08-02）。
* **A36** ✅ 协程状态机在深调用栈下的栈溢出防护（2026-08-02）。
* **A37** ✅ 字符集转换 UTF-8 / UTF-16 零拷贝优化（2026-08-03）。
* **A38** ✅ 编译期常量折叠范围扩展（2026-08-03）。
* **A39** ✅ 文件流只读并发安全（2026-08-03）。
* **A40** ✅ 字符串子串查找 Boyer-Moore-Horspool 优化（2026-08-04）。
* **A41** ✅ 静态变量多线程重入可见性屏障（2026-08-04）。
* **A42** ✅ 浮点数格式化精度对齐 IEEE-754（2026-08-04）。
* **A43** ✅ C# 语言特性平移（A43-A/B/C）：泛型扩展方法、模式匹配核心语法等已完成；A43-C1 类型化查询待结合 B9③ 演进。
* **A45** ✅ `List<T>` 类型参数严格检查漏洞修复（2026-08-07，`2e1fe4b8`）。
* **A46** [-] 设计器内置控件映射已作废（随 .html 迁移已彻底解决）。
* **A47** ✅ 消除重复 openssl 与 build 自嵌套（2026-08-06 卫生修复）。
* **A48** ✅ 交叉编译共享库内置轻量运行时（2026-08-27，A48-1）。
* **A49** ✅ 整数文本化负数边界修复（2026-08-14）。

---
# A50 · CEF 浏览器控件的剩余缺口（可选原生驱动）

> CEF 驱动作为 Zan 的外置可选浏览驱动（Windows 默认使用系统 WebView2）。以下为 CEF 专有高级特性缺口挂账：

* [x] **A50-1**：CEF 离屏渲染与 Direct2D/Direct3D 纹理共享（已完成）。
* [ ] **A50-2**：原生右键上下文菜单定制与拦截接口缺失（中优先级）。
* [ ] **A50-3**：静默打印与 PDF 虚拟打印回调支持（低优先级）。

---

# A52 稳定性安全网（✅ 全部闭账）

* **A52-1 ~ A52-4** ✅ 编译参数防御与非法内联拦截（2026-08 完成）。
* **A52-5** ✅ `--publish` over-release 编译期与运行时安全网（2026-09-14 闭账，`734e7e62`）。
* **A52-8** ✅ `host_oom` 汇入 `zan_rt_fatal` 统一异常漏斗（2026-09-14 闭账，`4cadce08`）。

---

# A56-A83 历史修复记录（全部完成，一行摘要）

* **A56 ~ A77** ✅ 泛型嵌套推导、协程闭包重入、IDE 语法高亮与跨平台编译链路修复（2026-08-23 ~ 08-31 全部完成）。
* **A78-3** ✅ 泛型实例化超过 64 个崩溃问题根治（2026-09-11 闭账）。
* **A70** ✅ `Thread.Start` 实例方法组委托传递支持（2026-09-11 闭账）。
* **A81** ✅ ra2 模板空安全收紧兼容修复（2026-09-04 闭账）。
* **A83** ✅ 自动化测试差异处理：ra2 因架构重构移出，相关基准已重新平衡。

---
# A58 · 稳定性收口与运行时加固（✅ 全部完成）

* **第 1 批·封死静默产错码**：布尔/指针强类型检查、多赋值析构顺序、枚举范围检查全覆盖（2026-08-28 全部完成）。
* **第 2 批·验证基建**：构建 `--arc-guard` 专用内存防御工具链，前端 Fuzz 测试体系上线（2026-08-29 全部完成）。
* **第 3 批·企业级运行时嵌入**：内存崩溃诊断栈重构、安全字符串切片边界指针标记（2026-08-30 全部完成）。
* **第 4 批·标准库架构解耦**：GUI 与核心 runtime 完全隔离，依赖树单向收敛（2026-08-31 全部完成）。

---
# 专项审计与近期修复

## 真实遗留项挂账（待协调 / 需外部生态或设计决定）

* [x] **A352 · full 门禁首轮 triage：6 个网络 leakcheck 孪生确定性红（既有，非本轮回归）→ 已修 5/6（2026-09-24）** 根因=stdlib 服务端组件的**静态保持根 + 永生协程帧**在退出泄漏探测里恒可达：`MqttBroker.inst` 静态单例（Worker 路由经 `Global()` 惰性建）、`Worker.Mqtt/Ws/Sse` 的 static 委托槽、`WorkerWs/WorkerSse.sharedBus` 静态总线，以及 `MqttBroker.EnsureKeepAliveInspector` 巡检协程（`keepAliveRunning` 有标志但**无人置 false**，其帧持有 `this`=broker，broker 的三个列表随之恒可达；巡检局部 `s` 还钉住被踢会话——LWT 孪生的 41/42/694 即 p4）。**修复定式（拆卸对称性）**：① 组件提供对称拆卸面——`WorkerMqtt/Ws/Sse.Uninstall()`（清 static 委托槽 + 停静态总线 + `MqttBroker.TeardownGlobal()`：Shutdown 置 `keepAliveRunning=false`、停总线、清表、null 掉 inst）；② `Shutdown()` 置停**一切**内部协程（帧持有 this，不置停就永生）；③ 巡检循环用 100ms 观察步长 ×10 计满才扫表（周期仍 1s，但停机后帧最迟 100ms 可见标志退出——1s 的 Delay 会让测试的泵等待来不及）；④ **测试必须把 Uninstall 放在泵等待之前**（停掉的帧要靠后续 tick 看到标志才能完成，q2 曾放在泵后仍红）；⑤ WebDavClient 七个每调用方法补 `client.Close()`（此前每次调用新建的 HttpClient 套接字永不再关）。结果：`mqtt_lwt_retain`(24→0)/`mqtt_qos2_bus`(11→0)/`sse_stream`(4→0)/`ws_loopback`(4→0)/`ws_protocol_gate`(4→0) 全绿；webdav 残余转 A355。
* [x] **A355 · 编译器 async 降级/ARC 缺陷：HttpClient 空体响应的尾部 StringBuilder 过保持（webdav_conformance 残余，A352 分出）** `webdav_conformance` 仍漏 5×`HttpClient.zan:1271` rsb（5 个空体调用：MKCOL/PUT/DELETE/COPY/MOVE；PROPFIND/GET 有体不漏）。**最小复现**（`_scratch/lk_rsb/probe7.zan`，已删，重建仅 20 行）：`Socket.Initialize()` → `HttpServer("127.0.0.1", P)`，`OnRequest(Handle)`（返回 `HttpResponse.WithStatus(204, "No Content", "")`），`Task.Spawn(server.Start())`，延迟 50ms 后 `HttpClient c = new HttpClient("127.0.0.1", P); HttpResponse r = await c.SendAsync("DELETE", "/204", ""); c.Close(); r.Dispose();`，`server.Stop()` + `for (20) await Task.Delay(10)` → `--check-leaks` 恒报漏 1×1271。**触发条件=空响应体**：204/304/`Content-Length: 0`（走 noBody 或 ReadBody 均可）都漏；带体（如 `Text("x-body")`）不漏；与调用顺序无关。**已排除（stdlib 副本变体矩阵）**：`rsb.Append(bodyOut)`（空串 Append）删掉仍漏；`AbsorbCookies` 删掉仍漏；`rsb.ToString()` 换成直接 `return head` 仍漏——分配后所有 Zan 语句无关，rsb 只要"分配+Append(head)+return"就漏。**IR 审计**：merge3402 处 `zan_rt_alloc` → fl30，最终完成块 rt.cont3574 的释放链含 fl30——静态看无缺失；但运行时该对象 rc 未归零。旧 zanc（wt2、_scratch/zanc-pre-async@9590cbb6）同漏——长期存在，非近期回归。语言级形状复现（probe4/probe8：纯 async StringBuilder 局部、resume 段内分配）均干净，说明需要真实多 await 轮廓。**下一步**：给编译器发射的 `zan_rt_retain/release/alloc`（irgen.c:2614+ 内联实现）加 site 感知 trace（retain/release 时读 obj-8 的 site 字段），对 site-1271 事件序排序即可看到多出的 +1 来自哪条路径；属编译器异步帧生命周期/ARC 释放放置问题，修复后 webdav_conformance 应转绿。**修复（2026-09-24，同日闭账）**：真凶不是 async——是 **ARC 链式 owned 接收者临时泄漏**：`StringBuilder Append/AppendLine/ToString` 内建分支（irgen_call.c）直接求值接收者表达式、从不释放 owned (+1) 接收者；`Append` 声明为 void，语句级丢弃兜底（irgen_stmt.c EXPR_STMT owned 释放）也不触发。HttpClient.zan:1271 的“rsb 过保持”是**站点名错标**——空体响应走 `HttpResponse.BuildHeaders()` 的 `return this.BuildHeadersSb().ToString();` 链式临时（有体走 `Build()` 局部变量版所以干净），泄漏对象是 BuildHeadersSb 的 sb；1271 站点名是同形状 StringBuilder 分配点按形状别名（`-g` 才按 file:line 键控，arc_leak_site_label 同理）。**修法**：SB 内建分支复用集合内建的 `emit_intrinsic_own_recv/emit_intrinsic_drop_recv` 定式（EH 暂存接收者→求值实参→内建完成后 drop），只在真正走三个返回点时挂载；其余类型（普通类/List/Dict/string）实测无此洞。新探针设施：`--check-leaks` 构建下 env `ZAN_ARC_TRACE=1` 打印 alloc/retain/release/release-dyn 事件（obj/site/rc/调用点返回地址），本次即靠它把“单个多余 retain”钉死。测试 `tests/conformance/sb_chained_recv.zan`（四种消费形态：return 位/丢弃语句/实参位/局部位），probe7+webdav+五个 A352 孪生复跑全绿。
* [x] **A354 · server-mvc 等四模板 `DataScope.RoleAll/...` 静态字段幽灵错误 → 已修（2026-09-25，GenRoute 根因闭环；game-platformer 与 gui-wechat 部分另见 A356/A354 原诊断）** 真凶不在模板代码：**GenRoute（stdlib/System/Compiler/GenRoute.zan）的表单类绑定对 static 字段也生成 `sc.RoleAll = __c.InInt(...)` 形式的实例写**——凡 action 带类参数（如 `Members(DataScope sc)`）且该类有静态字段，合成源码必炸；而合成源码的诊断 file/line 被错标到无关文件（Login.zan:1499 出现在 148 行文件里），掩盖现场。证据链：最小化到单文件 + `ZAN_GEN_REPLY` dump 出合成 __AttrRoutes 中 8 条静态字段写（3 方法 × 8 字段 = 24 条幽灵错误，与计数吻合）。**修法**：GenRoute 表单绑定循环按 genmeta 字段元数据的 `"static"` 布尔（genmeta.c:852 已导出）跳过静态字段（静态字段本就不属于请求形态）；回归用例加进 tests/conformance/web_typed_binding.zan（表单类加 static int made，断言请求写不进去、文档参数数不含它），web_typed_binding/web_list_protocol/web_api_docs/web_menu_attrs 四用例全绿。server-mvc 编译绿 + e2e 121/121（2026-09-25）；server-collab/server-legend/server-licensing 属同根因，编译应随本修复转绿（待 templates_build 复核）。
* [ ] **A358 · 模板出生即红残留（A354 闭账 server-mvc 四模板后余项）** game-platformer 调 `GameEngine.Init/Run/IsKeyDown/IsKeyPressed/Clear/DrawRect/DrawText` 与 `Convert.ToSingle`——全仓 `git log -S "class GameEngine"`（所有 ref）零命中：引擎代码从未落库（9/21 会话的引擎改造未提交）；`Convert.ToSingle` 亦非内建亦无 stdlib 类（自然修法=按 A356 P1 迁 Arcade2D，或删模板）。gui-wechat：`Form` 无 `Close` 成员。**连带问题**：templates_build 只挂 full 层（standard 跑不到），红了没人看；包依赖模板的发现路径（模板自带 zan.proj 挡住向上找 monorepo packages/）。处置需 owner 决定。
* [ ] **A357 · checker 漏洞：未实现接口的类可隐式转接口参数（编译期不报错，运行时炸）** 最小复现（20 行，_scratch 已清，重建即可）：`interface IPing { int Ping(); } class Imp : IPing { … } class NotImp { int Ping() { return 2; } }`，`static int Take(IPing f)` 分别传 `Imp`/`NotImp`——**编译通过**，运行时 `f.Ping()` 报 `runtime error: interface dispatch has no implementation`（2026-09-25 实测，build/zanc 207 文件模板同机复现）。现实触发：7626f096（DAO 拆分）写了 `new CategoryDao(this.Db())`——`this.Db()` 返回 `DbContext`（不实现 `IDbConnection`），checker 未拒，e2e 在 /rss.xml 处进程炸（OrmSelect.zan GetProvider/QueryAsync 两处 dispatch 无实现；因 A354 阻塞编译、e2e 长期没跑而未被发现）。**应为编译期错误**：assignability 判定在目标是接口类型时必须验证源类（或其基类链）实现了该接口；class→class 同样值得复核。另注意 GenRoute 的 db_acc_head 改写依赖 `obj.__Conn()` 存在性，接口实现校验补上后此类错全部前移到编译期。
* [ ] **A356 · 两轨基座：渲染引擎能力增强（工具+游戏共享）+ 游戏引擎按大类重设计（owner 方向已定，设计稿 docs/ENGINE_REDESIGN.md）** owner 裁定（2026-09-24）：放置类=工具+皮肤留在 Gui 轨（缺的动画能力由渲染引擎补），游戏引擎推倒按大类（传奇/红警/帝国/魔兽/卡牌/独立游戏）建通用基座，不逐模板打补丁。原条目内容（诊断不变，分层方案已被设计稿取代）：染供给错位是性能病根，按大类建基座而非逐模板复用（2026-09-24）** **诊断（代码证据）**：① zan_gui GL vtable `blit_image = NULL`（gui_gl_backend.c:1663）——贴图精灵在 GPU 路径未实现，每贴图走 CPU 光栅+帧缓冲上传，且一帧混 image/blur/影子即 CPU-GPU 同步（gui_gl_backend.c:527 注释自认）；② 批处理只覆盖 UI 工具图元：rect/circle/radial 顶点批按 mode 聚合、文本有字形图集——传奇/RTS/卡牌的主语（成百上千图集精灵、图块）恰是栈里最弱的原语；③ CanvasPrims 每帧程序化重画光晕（FillRadial 每像素径向衰减），SDL 时代 bake 纹理一次每帧贴图，canvas 化丢了烘焙层；④ Kit.SpriteBatch 是假批（计数器封装，逐图元 FFI 调 CDraw）。GuiHost 主循环本身健康（事件排空/定步积分/连续出帧）——病在渲染供给不在循环。这解释了现状：放置/工具类（legend/wuwei）在 Gui.Widget 上如鱼得水，真游戏逆水行舟。**基座分层**：R0 渲染基座=给 zan_gui 补纹理化精灵批（图集注册 + `zan_gui_sprite_batch(atlas, packed_quads[], n)` 数组式一层一次 FFI，GL 加 textured-quad kind 与几何批共存，CPU 兜底同步实现）——原语优先级反转，贴图四边形一等公民；R1 烘焙与资产=程序化效果一次烘进图集+图集打包+帧表动画接批；S 模拟基座=定步确定性(Foundation 有)+实体 int id 池化(ARC 零计数 churn、天然可序列化=回放/锁步)+空间哈希+A*/流场上提(Board 有 A*)+RTS 指令/编队/迷雾(全新)；K 大类 Kit=Arpg(传奇，9.8k 行做底)/RTS kit(新建)/Cards/Board(已有)/Idle=Gui 融合正式化（游戏表面作为 Gui 部件+脏区重绘+按需动画，不进 60fps——放置游戏就是工具）；N 联网=RTS 锁步(依赖确定性 sim)+传奇服务器权威(Arpg Net/Server 有雏形)；G 门禁=帧预算 gate 扩到游戏模板+精灵批真指标。**分期**：P0=R0+R1+SpriteBatch 变真批（验收：万精灵 60fps 帧预算 gate）；P1=game-platformer 迁 Arcade2D+新批（消 A354）+id 化池；P2=RTS kit 新建+Arpg 对 legend 公共层下沉评估；P3=锁步联网。连带：README 宣称的 Game.Zgm 模块不存在（文档失真）。
* [x] **A359 · Zan.Web 声明式列表基座 ListPage 落地：一屏声明产出搜索区/工具条/条件收集/保筛选翻页链接（2026-09-25 闭账）** 对齐 OneAdmin（TableConfig 驱动）与 PHP 版（index_search_fmt 一份声明同喂 UI 与 SQL）的习惯，列表页不再手写搜索表单与 SQL 两份口径。结构：`ListPage.Of().Text/Select/Time/Pick(...).TbarAdd/TbarBatch/TbarDelete(...)` → `Collect(this)`（只收声明名，空值跳过）→ DAO 侧 `ApplyConds(__DbQ_<T>, List<ListCond>)` 拼参数化 WHERE（列名来自声明常量、值全占位符，多列 LIKE `|` 分组 OR）；SQL 留数据层。批端点零容忍：`Ids()` 收 CSV 主键（≤500），任一行越权（超管/自身停用）整批拒绝。Pick 远程搜索单选：选项端点 `?kw=`/`?id=N` 同服务筛选与表单。三屏迁移（Users/Articles/Logs）+ e2e 121/121 + 实机核对搜索/批量确认层/选择器/回显。实机揪出并修掉两个 JS 缺陷：pick `apply` 对 `data` 为真数组时 `JSON.parse` 必炸（改兼容数组/对象/字符串）；带值回显只渲染下拉不填搜索框（改直接填值不开浮层），layout 资产版本 v=12→13。教训入 zan-development skill（GenDb facade 定式 + publish 资产嵌坑）。
* [x] **A360 · Zan.Web 声明式表单基座 FormPage 落地：一份字段声明同驱动渲染与服务端校验，通用 CRUD 屏零视图文件（2026-09-25 闭账）** 与 A359 ListPage 合成完整配置化 CRUD：列表（搜索/工具条/条件/翻页）+ 表单（新增/编辑/校验）各一份声明。结构：`FormPage.Of(saveUrl).Text/Pass/Area/Select/SelectList/Hint(...)` + 修饰（`Req/Max/Min/Val/Ph/Span/Rows/OnlyNew/OnlyEdit/Blank`，作用于最近声明）；`Form()` 加载实体后 `fp.Field(name).Val(...)` 绑回显值 → `fp.Render(d)` → `AdminController.FormDialog(d)` 走共享壳 `views/Admin/_FormDialog.html`（FragmentOf，按实体各写 Form.html 的时代结束）；`Save()` 头部 `fp.Validate(this)` 从同一声明校验 required/min/max。条件可见性：`OnlyNew`（初始密码）/`OnlyEdit`（编辑提示）按主键判定，渲染与校验同步——编辑时初始密码既不渲染也不校验；pass 恒不回显，值回填经 Esc。Users/Articles 两屏迁移（各删一个手写 Form.html），业务规则（唯一性/scope/密码哈希）仍归动作。验证：e2e 121/121；实机核对新增渲染（req 星标/空白选项/条件字段）、两条声明校验文案、编辑回显（值/选中/密码隐藏/提示出现）、Articles 宽布局（span/rows/默认选中/作者预填）与对话框保存全链路（toast+列表刷新）。踩坑沉淀：e2e 与手动沙箱共用 _scratch/mvc_e2e，e2e 收尾会重写 config——手动起服务前需重新生成配置，否则 auth secret 缺失表现为"未配置会话密钥"假故障（入 skill）。

* [ ] **A91**：CSS `cursor` 属性在 Retained 模式下无消费点（待 GUI 事件总线统一注入鼠标光标切换）。
* [ ] **A263**：ECharts 6.1 高级属性长效对照清单（长效非阻塞，按业务需求逐步扩充，详见 `docs/ECHARTS_SPEC.md`）。
* [ ] **A305 / A306**：legend 迷你传奇模板在某些特定驱动下的偶发导航重绘白屏（已用保守双缓冲规避，待进一步根查底层显卡驱动 SwapChain 行为）。

---

## 近期专项闭账记录（2026-09-09 ~ 2026-09-22 全部完成）

* **A342 · nullable→字符串双通道 verifier 崩溃** ✅ `Convert.ToString(T?)` 与插值 `"{x}"` 的自建分支链把 `zan.nullable.<payload>` 结构按值递进 `__zan_itoa64`（拼接/打印路径有解包、这两条没有），LLVM verifier 拒绝模块（"Call parameter type does not match"）；修复=两处统一改走 `emit_to_cstr_u` 的 nullable 分支（some 格式化载荷、none 空串，与 `+` 拼接同一实现，none 臂分配新鲜空串保证 owned 精确），`.ToString()` 直调维持 checker 拦截诊断。`nullable_tostring` conformance 锁定 some/none × int?/double?/ulong?（提交 `9e0daec5`，2026-09-23）。
* **A343 · 多 worker 驱动程序化配置死管道** ✅ `zan_async_set_workers/io_shards/sync_fast`（rt_timer.c）自落地起无任何消费点：Zan 侧 `System.Threading.AsyncRuntime` 类从未存在（rt_co.h 注释承诺与实现相反），驱动只读 `ZAN_CO_WORKERS/ZAN_IO_SHARDS/ZAN_IO_SYNCFAST` 环境变量，且 worker 数在 `zan_co_sched_init` 固化快照、Main 内设置天然晚到。修复=stdlib 新增 `System/Threading/AsyncRuntime.zan`（DllImport crt 直连 setter/getter）、`zan_co_sched_run` 与 `co_pool_start_background` 两个池启动点重解析 `co_worker_count`（cfg 优先、env 回退、CPU 兜底；io_shards/syncfast 惰性读点本就在 Main 之后）。A/B 实证：`SetWorkers(1)` + `ZAN_CO_WORKERS=8` 环境下 8×50000 无同步计数精确 400000（对照 8 workers 丢失至 15~22 万）（提交 `9e0daec5`，2026-09-23）。
* **测试门禁三缺口（随 A342/A343 同批）** ✅ ctest 分层正则无 `unit_` 前缀致 `unit_json_oom/unit_json_trailing/unit_rpc_framing` 永远只挂 full 标签（smoke/standard 形同虚设）——正则补 `unit_` 入 smoke+standard；Windows CI 构建集（仅 zanc+zanrt_sync_selfhost）与测试花名册不对称——构建行补三纯 C 单测目标；`-E` 过滤器清除已无注册项的 `dm_database`；ZANC_CLI.md 撤除已不存在的 `--async-workers/--mt` 旗标行，改为按目标自动链入 `zanrt_io_mt` + `AsyncRuntime`→env→CPU 的解析顺序说明（提交 `9e0daec5`，2026-09-23）。
* **A344 · 嵌套类型提升丢宿主关系，限定调用被 checker 拒绝** ✅ `Outer.Inner.Value()` 在 standard 门禁报 `'Outer' has no member 'Inner'`，而 A/B（worktree @ `7050fb2c`）实证 2026-08-08 编译运行全绿——回归而非空想用例。根因：parser `zan_parser_flatten_nested_types` 把嵌套类型整体搬到单元级、注册靠简单名，旧版 checker 对非标量接收者的成员 miss 只返回 type_error 不发诊断，irgen 限定名路径静默兜底；typo 守卫收紧（本身正确，fully_qualified_unresolved 依赖）后合法路径一起被拒。修复：hoist 时在 AST 盖 `type_decl.nested_host` 宿主节点戳（delegate 与 method_decl 共用 union 不盖戳），binder 全部注册完成后按节点指针 `symbol_add_member` 挂回宿主 members[]——全部消费方按 kind/名过滤，字段布局不受影响。smoke 294 项仅余并行会话已知两红，standard 921 项该用例转绿（提交本批，2026-09-23）。
* **A345 · 后台池泵等待条件含 live_count → Task.Wait/Result 永久挂死** ✅ cs_b15_task 120s 超时：`Task<T>.Run` 刻意留帧不收割等 Result 读取，`zan_co_sched_run` 后台分支却等 `live_count==0` 才返回——泵自己等不到自己要收割的帧（`831577d60` 引入；A/B worktree @ `7050fb2c` 全绿证回归）。修复=等待条件收敛为 pending/io/timer/running 四个可调度计数，live-but-done 帧不是工作（提交本批，2026-09-23）。
* **A346 · WsSharedBus 三缺陷同修（ws_cluster_bus 红）** ✅ ① 集群连接计数用 `totalConns` 列但建表从未 `ColumnInt` 声明——`zan_find_column` 落空，Increment/GetInt 静默得 0；② 轮询自过滤只比 pid，同进程多实例互发全被当自消息丢弃——身份改 (pid, 实例 nonce)，新增 srcBus 列；③ StartPolling 是 async 方法，Zan 丢弃 async 调用的体首次泵才启动，开场 head 快照晚于调用点发布→消息被当旧序号跳过——拆同步快照 + 内部 PollLoop（async 启动语义教训已沉淀 skill）（提交本批，2026-09-23）。
* **A347 · 网络孪生测试手搓 calloc 串缓冲层全红 + genform_click 注册撞名** ✅ firebird_wire/sqlserver_tds 的 Fake 层用 `DllImport(crt) calloc` 返回的 string 当收发缓冲——外来串无长度元数据，下标守卫按 0 界拒绝，读写全崩；sqlserver_tds 的假服务协程崩在守卫上，客户端死等表现为 120s 超时（Timeout 假象）。两测试照真驱动 recvExact 的 `byte[]` 形态重写缓冲层，分别 127/0、83/0 全绿。genform_click_test 另有 GLOB 注册撞名：期望输出实为 genform_click.out，glob 机械按 `<名>.out` 找不到——照 arc_net_publish 先例在 glob 循环跳过，保留按真实文件名的手工登记（提交本批，2026-09-23）。
* **A348 · childwindow_shape 断言与记录器缺陷（Gui 会话提交时 ctest 未能运行）** ✅ 探针实证 stdlib 修复本身按设计工作（排版点=offX(15)+Scale(5)边距(7)=22 设备像素、top(48)+offY(15)+7=70），但测试①拿设备像素 bx 比未乘边距的 ShapeOffX（100% DPI 碰巧相等、150% 必红）；②MarkerBox 静态记录器被并发存活的另一宿主重绘回写（父泵顺带泵到已显示子宿主），plain 阶段读到 shape 的值。修法=记录器按相位分槽（tag 索引静态数组）+ 断言计入边距与 DPI，相邻 upload/tree golden 三案回归绿（提交本批，2026-09-23）。
* **A349 · APK manifest 字符串池补丁拒绝/静默截断长串** ✅ `axml_patch` 三层缺陷：① UTF-8 池条目带 **双前缀**（u16 单元数 + u8 字节数，≥128 时各自扩为 16 位形式 `0x80|hi,lo`），旧读侧见高位即 `return -1`（写侧还只写单前缀，与 aapt2 规范不符）；② zan.proj 的 androidPackage/androidLabel 落 128 字节缓冲，超长被 `snprintf` **静默截断成错的包名**（比拒绝更糟）；③ 替换超 ~1197 字节时写侧循环条件静默截断。修法=读侧照 ResStringPool 规范解双前缀 + UTF-16 条目 ≥0x8000 的 32 位长度形式、写侧 `pool_put_len` 规范双前缀 + 容量超限显式报错、proj 缓冲扩 256 且超限/权限名超 127/权限超 16 条全部显式报错。验证=独立探针（`#include apk.c`）对真实模板 UTF-16 池与 python 合成的规范 UTF-8 池各打 200 字符包名 + 多字节标签 + 权限追加，python 逐串复核未触碰串字节不变；proj 超长报错路径 e2e 落地。e2e 成功路径被 A350 挡住，A350 修复后 e2e 全链路闭环——并在闭环时抓出**第四层**截断：packaging 调用点还有 `char pkg[128], lbl[128]` 本地缓冲把 200 字符包名剪成 127，已扩 256 + 超限显式报错；签名 APK 解包逐串复核 200 字符包名/多字节标签/两权限字节精确在位（2026-09-23）。
* **A350 · 用户类型与泛型类型参数同名击穿泛型方法调用（`class T` vs `Binding<T>`）** ✅ 初报为"android 交叉编译 stdlib Gui 类型检查红"，深挖后**比挂账严重且完全不同**：与任何用户代码无关 target——只要用户声明了与泛型类类型参数同名的类型（`class T`），该泛型类的**成员方法在桌面/安卓一切调用点全部报错**（`no overload of 'Binding.Set' matches argument type(s)`），PropSpec/Input/Checkbox/Button 全中，GUI 打包全灭。根因双层：① binder 把类型参数按**简单名**注册进单元作用域（`register_type_param_list` 的 `scope_find` 命中即跳过），用户类 T 抢走该名后泛型 T 全局失明；② 绑定期签名解析在类作用域内是**健康**的（`bind_members` 类局部 scope 无条件注册 TP），但产物只落了返回类型（`msym->type`），**参数类型没落**——irgen 侧 `method_param_type` 等对签名类型引用做裸 `resolve_type`，调用点作用域下一解析就命中用户类，重载打分把唯一匹配的重载判死。修法=irgen 取方法参数类型时**优先按 decl 命中方法符号上绑定期落下的 SYM_PARAM 子符号**（返回类型早已走此路），打分与发射同源。定位手法：报错形状矛盾（conformance 绿/探针红）时 diff 通过与失败用例的最小形状差——类名 Program 与 T，一击命中。回归锁 binding_tp_shadowed_by_user_class（Set 写穿回模型 + 用户类 T 照常可用）。修复后 A349 的 e2e emit-apk 全链路随之闭环（2026-09-23）。
* **A351 · 对象初始化器写 Binding 字段多发一次 retain（full 门禁档 leakcheck 孪生红）** ✅ full 发布门禁首轮扫出 `leakcheck_objinit_ctor_field_overwrite` 红：`new Card { title = "x" }` 后再普通赋值，退出时恒剩 1 个 Binding 盒。探针二分定案（p1 纯赋值绿 / p6 仅初始化器红 / p7 两次普通赋值绿）：**对象初始化器路径** `emit_binding_value` 交出 +1 后，store 调用仍把原始 RHS 递给 `emit_rc_store_field`——字面量被判借用再 retain 一次，盒以 rc=2 落字段，出口级联只放一次。A341 重构留下的半截线：`fval_owned` 标志算了**从没用上**。修法=沿用普通赋值路径的 owned dummy 标记惯用法（`owned_rhs_marker`），初始化器两个 store 分支同补。IR 级实证：`retain %bindobj → store → release old` 序列在修复前直接可见。验证=5 探针 + objinit/binding 窄档 ctest 28/28（worktree zanc，主树 zanc 被并行会话 in-flight main.c 锁着不能重建）。A/B 定性：wt_pre(485f0127) 同红——既有缺陷非本轮回归（2026-09-23）。
* **server-mvc 冗余第三方前端库清理与 A317 闭账** ✅ 彻底移除 `vue.global.prod` / `naive-ui` / `zan-charts` / `zan-grid` / `zan-layer` 等未用/冗余前端脚本与对应 SPA 壳，回归轻量标准服务端 MVC 架构；关联第三方库缺陷 A317 闭账（2026-09-22）。
* **LSP 深度完善** ✅ 文档优先类型补全、增量 didChange、诊断工作线程与 using 命名空间补全全通（2026-09-09）。
* **ZanIDE 启动稳定性** ✅ 修复启动中间产物竞态段错误，确立 IDE 崩溃日志自愈标准（2026-09-09）。
* **跨平台运行时对象** ✅ win-arm64 / OpenHarmony / Android 原生对象重出与缺失符号补齐（2026-09-09 ~ 09-20）。
* **设计器交互与属性面** ✅ 撤销重做快照修复、全组件调色板对齐、Tabs 标签条方向与 BuiltinPropRows 通用直通（2026-09-10）。
* **GUI 审美与布局安全** ✅ ZAN_GUI_OVERLAP 重叠探测器与 ZAN_GUI_LAYOUTLINT 机械布局闸门落地（2026-09-10）。
* **音频运行时解耦** ✅ 彻底移除 SDL3，自研 `zan_audio` 原生轻量音频运行时全平台落地（2026-09-09）。
* **H5 / WebAssembly** ✅ wasm32 文件 IO 与异常控制流全通，最小 WebAssembly 运行闭环（2026-09-10）。
* **游戏引擎与生态** ✅ `Game.Idle` 放置库、Tween 缓动补间、高级物理碰撞射线、分级音频总线全面落地（2026-09-10 ~ 09-20）。
* **密码与网络吞吐** ✅ 加解密 Wrapper 零拷贝落地（吞吐跃升至 EVP 级别），发布种子明文移除（2026-09-10 ~ 09-11）。
* **服务端框架与模板** ✅ 7 个服务端模板抽测复绿，server-game 与 server-mvc 架构强化（2026-09-11 ~ 09-12）。
* **P8 组件精简与声明化** ✅ 流式 % 尺寸修复、Binding 初始化降级、Extra 冗余代码全删、`.zform` 全面退役（2026-09-13）。
* **图表与渲染引擎 Sweep** ✅ 日历图表调色板 Alpha 修复、柱状图垂直堆叠基线修复、347 例图表 Demo 审查全绿（2026-09-13 ~ 09-15，`21044e83`）。
* **ZanDb 核心引擎四轮攻坚** ✅ 扫描层零拷贝 + 段 CRC 校验 + 块级聚合内核 + 覆盖索引 GROUP BY + 跨度整读（2026-09-13 ~ 09-14 全部闭账）。
* **地图组件修复** ✅ 地图区域接缝闭点、共享边几何拓扑一致化、geo lines 车道与悬停扰动消除（2026-09-14）。
* **矩阵坐标系与复杂图表** ✅ ECharts 6 matrix 坐标系车道、直角坐标热力图车道与大数据平滑渲染（2026-09-15）。
* **跨进程并发总线** ✅ WebSocket/SSE 多 Worker 跨进程广播与频道路由，支持无锁 SharedTable 总线与全网连接计数（2026-09-20，`302b7a30`）。
* **现代化语法特性** ✅ 编译器支持现代 `defer` 确定性资源释放（LIFO 展开）、switch 重复标签阻断与穷尽性检查、闭包 this 环静态检测（2026-09-20）。
* **应用更新与防篡改生态** ✅ 发布通用更新包 `Zan.AppUpdate`（Zip 解压缩/断点续传/MD5 自愈校验）并补齐生态文档（2026-09-21）。

---
# A320-A341 审计落地与发布体积对账总表（✅ 全部闭账）

## 1. 发布体积肥边六项治理对账

| 肥边编号 | 解耦项目 | 治理手段与提交 | 状态 |
|:---|:---|:---|:---|
| **肥边①** | DataGrid 迷你图列 | 运行时弱引用注册，独立按需引入 | ✅ 已闭账 |
| **肥边②** | DataTable 导出 Xlsx | 解耦压缩与 XML 生成，独立组件化 | ✅ 已闭账 |
| **肥边③** | Worker 协议分片 | 按需拉取协议解析，消除全局依赖 | ✅ 已闭账 |
| **肥边④** | HttpFramer TLS 注入 | TLS 加密流动态注入，避免无加密服务冗余 | ✅ 已闭账 |
| **肥边⑤** | 自进程原语下沉 | AppPath/Process 原语下沉最小基础库 | ✅ 已闭账 |
| **肥边⑥** | HttpContext.WsUpgrade | 迁至独立扩展 `WebWs.Upgrade`（提交 `12925efe`） | ✅ 已闭账 |

> **体积基线实证结论**：
> * **A328 PE 链接期回收**：A/B 实证测得 lld-link GC 正常工作，剥离存活符号收益仅 0.1%，决定维持现状不返工。
> * **A330 stdlib 懒化第一期**：拍板维持默认全功能，不引入繁琐 opt-in，普查宣告闭账。

---

## 2. A320~A341 近期关键攻坚项落地总表

* **A323** ✅ 编译器五缺陷批修（2026-09-17）：最小探针闭环修复。
* **A327-②** ✅ Windows 后台池丢唤醒漏洞根治（提交 `d94e58a3` 闭账）。
* **A327-③** ✅ POSIX 多线程协程调度器平台适配（提交 `21044e83` 闭账）。
* **A327-13** ✅ 协作中心三阶段（13a/13b/13c 假文件退役、真附件、任务卡片）全面落地（提交 `807283f4` 等闭账）。
* **A327-14** ✅ 工作流中心自托管化与 10 个 FlowApi 闭环（提交 `886b0f69` 闭账）。
* **A333** ✅ WebView2 多平台语义一致化（CSS 注入/DevTools/静默安装）（提交 `a2a9501e`、`f5fe69b4` 闭账）。
* **A340** ✅ 审计散件全部落地：
  - ① postfix `!` null 包容操作符（提交 `eb52aaa4` 落地闭账）。
  - ② 假语义修复三则（`316fab05` 落地闭账）。
  - ③ BCL 统一决策归档，冗余数据并入 A15-5。
* **A341** ✅ `struct` 值内 `rc` 引用计数生命周期彻底修复（聚合 struct 复制/传参/返回 ARC 管理落地，提交 `f9c8d841` 闭账）。

---
