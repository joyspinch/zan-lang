# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：`[ ]` 未完成 · `[~]` 进行中 · `[-]` 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 `[x]` 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 2026-09-29 平账：全部已闭账条目移出台账（C-1..3、B-HW1..4、A-HW4/5、B-ID1..16、B-NET1..4、B-NET4a..j 通讯协议加固各条）及"最近验证"流水；截至提交 `b380d2b7`，通讯协议安全审计批次 A–F 全部闭账（含 B-NET1/2/3 按 fail-closed 边界闭账：macOS/iOS 无 SecTrust 桥接恒拒绝、CRL/OCSP/nameConstraints 撤销未知 fail-closed、Ed25519/P-384/完整 UTS-46 严格拒绝）。
>
> 2026-09-30 平账：全部已闭账条目移出台账（B-ID17、B-ID18、B-ID20、B-ID36、B-ID22、B-ID23、B-ID25、B-ID26、B-ID27、B-ID28、B-ID29、B-ID34、B-ID35、B-ID32、B-ID33、A-MEM1、A-MEM2、A-MEM3，共 18 条）——闭账结论与验证证据由对应提交正文承担（6bf6e85f、27ffbf7a、7e1c6e20、050a0052 等）。

## 未完成 · IDE / 编译器

- [ ] **B-ID37** server-collab 模板再生存量工程：模板是**拆包前的平行框架副本**——231 个类内 50+ 与包同名（AppController/MetricsStore/Db/Cfg/Fmt/Cache/Mailer/MetricsStore/Perm/Routes…），且与包共用命名空间（模板 `namespace ZanWeb.Dao.Sys` 的 DepartmentDao vs 包同 ns 的 SysDepartmentDao，类名错开但裸名解析互相污染）；`using ZanWeb.Dao.Sys;` 之类前缀 using 会经包发现把包内族（含 Blog DAO）硬拽进编译集，任何裸名撞面即炸。仓库内与拷出仓库两种形态编译均红（包发现随 zanc 二进制的包根走，跟 CWD 无关）。方向：模板对齐包消费形态再生（删平行框架副本、改用包服务，参照 Zan.Web 迁移范式）或模板整体改名空间隔离；撞名清单已盘（50+ 名单见 B-ID36 会话 sweep：AdminController/ApiController/AppController/AppServices/Ai 族/Auth 族/Cache 族/Cfg/Db/DataScope/Fmt/Mail 族/Metric 族/Perm/Presence/Routes/Schema/Settings/VerifyCode 等）。正落在框架拆包迁移主航道，宜与 Zan.Web 迁移同批处置。
- [ ] **B-ID21** 设计器预览交互式分发：预览画布目前是 mock 渲染，复合控件预览实例还被 `Disabled = preview` 禁用（Designer.Form.zan 2355-2440 一带），设计文档声明的 `on<Event>` 处理器名在预览中静默无效（code-behind 不在预览进程加载，真实接线走 GenForm SetHandler → ChildWindow.WireNode）。方向：预览模式下对 PvCompCtl 实例按 `f.events`（基类 Control 的 event→handler 绑定列表）BindEvent 到设计器分发反馈（状态条 flash "事件 → 处理器名"），mock 字段加预览态点击命中走同一反馈； sender 参数经 2e296631 的 `HandleSender`/`BindEventS` 通道可一并送达。验证须实机 IDE 截图锚定（无头环境验不了设计器交互），B-ID7 闭账时因无实机验证条件未落地。
- [ ] **B-ID24** 语义决策挂账：十进制字面量 [2^31, 2^32] 隐式写入 `int` 槽位按二补码回绕（`int x = 3000000000` 得 -1294967296，checker.c `uint32_bit_pattern` 豁免），与既有 ARGB Java 风格设计（hex [2^31,2^32] 有意类型化为 int 回绕，checker.c AST_INT_LITERAL 注释）同族；C# 则一律 CS0031 报错。2026-09-30 字面量审计批次修掉的是**无争议缺陷**（hex/bin/oct >ulong.Max 静默截断已改报错、unsuffixed >long.Max 已提升 ulong 防变值），此条属有意设计的存废问题，需拍板：维持 Java 风格（现状）或对齐 C# 改报错。若改报错须全仓扫十进制 2147483648..4294967295 的 int 槽位写入点。


## 未完成 · 语义决策（审计批遗留，待拍板）

- [ ] **B-ID31** 运行时错误软着陆 vs fail-fast 默认：全仓审计批（2026-09-30）识别——部分运行时错误路径现为软着陆（记录后继续），提案是改走 fail-fast 默认（Go 的 unrecovered panic 语义）。批2b 核实相关守卫在代码里已存在，改不改默认属产品语义拍板项，审计批未动。批2 相关结论以 git 历史为准（a94129fa..29ddaae2 系列）。**决策依据（2026-09-30 盘点）**：运行时 C 层大多已 fail-fast 双态（`zan_rt_fatal` 漏斗 handler 返回也 abort，rt_timer.c:533；调度器滞留协程退出先报后停 rt_sched.c:502；GQCS 丢包有 pre-park drain 守卫）；设计内软着陆一处——GUI 每帧护栏 `zan__guard_call`（帧内硬故障隔离 + FaultCount 计数，产品语义正确，非缺陷）。真正的"记录后继续"集中在 **stdlib 异常面**：61 处 catch 仅 6 处重抛，代表性真吞点 UiErrorLog.zan:73（"无处可报；忽略"）、IconSvgData.zan:163（返空串）、GenRoute.zan 两处 rollback 空 catch、Power.zan 空 catch——改 fail-fast 的实际工程量 = 逐个裁决这 ~55 处 catch 的语义（哪些该传播、哪些是合法的错误值转换），运行时 C 层几乎不用动。

## 未完成 · 编译内存

