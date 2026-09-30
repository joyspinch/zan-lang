# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：`[ ]` 未完成 · `[~]` 进行中 · `[-]` 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 `[x]` 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 2026-09-29 平账：全部已闭账条目移出台账（C-1..3、B-HW1..4、A-HW4/5、B-ID1..16、B-NET1..4、B-NET4a..j 通讯协议加固各条）及"最近验证"流水；截至提交 `b380d2b7`，通讯协议安全审计批次 A–F 全部闭账（含 B-NET1/2/3 按 fail-closed 边界闭账：macOS/iOS 无 SecTrust 桥接恒拒绝、CRL/OCSP/nameConstraints 撤销未知 fail-closed、Ed25519/P-384/完整 UTS-46 严格拒绝）。
>
> 2026-09-30 平账：全部已闭账条目移出台账（B-ID17、B-ID18、B-ID20、B-ID36、B-ID22、B-ID23、B-ID25、B-ID26、B-ID27、B-ID28、B-ID29、B-ID34、B-ID35、B-ID32、B-ID33、A-MEM1、A-MEM2、A-MEM3，共 18 条）——闭账结论与验证证据由对应提交正文承担（6bf6e85f、27ffbf7a、7e1c6e20、050a0052 等）。
>
> 2026-10-01 平账：B-ID37 定案改写后随迁移批挂账，B-ID21 闭账条目移出（733ea3a5，实现/单例金样/经验沉淀正文承担）。
>
> 2026-10-01 平账：templates/examples 编译健康普查（冻结床 38 项）收尾——修复 5 项随本批提交（crypto_reference 快照改名空间避 Zan.Security 撞声明、wuwei 菜单字段避 Gui.Widget.Menu 类遮蔽〔作废，见下注〕、legend 限定名+SceneFn、Zan.Game 八处多参回调换具名委托），闭账条目移出；新增 B-ID38/B-ID39 两条编译器名字解析缺口（探针在册；处置见下注）；server 四模板红并归 B-ID37 迁移批。
>
> 2026-10-01 平账（更正）：**B-ID38 根治**随本批提交——nsresolve 同名歧义判定按泛型元数过滤（`count_simple_matching`：带类型实参的引用只与同元数声明冲突，`Action<int>` 在 `using System; using Gui;` 下恰一元可匹配即解析到 System.Linq.Action<T>，不再误报 ambiguous；裸名仍按名计数，两个同名零元 `Action` 双导入照报歧义，对齐 C# CS0104），conformance_ns_ambiguity_arity_filter 钉语义、四游戏模板冻结床回归绿，条目移出。**B-ID39 证伪撤案**：ZANC_DUMP_GEN 插桩实证 HEAD wuwei 显式传入 App.html 即 428 文件全绿，「字段被同名导入类遮蔽」机制不存在（checker 裸名本就字段优先）——普查的 wuwei 红是漏传设计稿的方法学红：zanc 不自动发现入口旁 .html，生成字段缺席后裸名 Menu 经"全库唯一简单名"兜底命中 Gui.Widget.Menu 类型，报误导性 "'Menu' has no member 'visible'" 且独占输出（undeclared 群在 checker 静默，要等 irgen 才冒）；上注 wuwei 修复作废、MenuRoot 改名回退原状，条目移出。
>
> 2026-10-01 平账：**B-ID24 拍板闭账（对齐 C#）**——十进制字面量 [2^31,2^32] 直写 int 槽位由二补码回绕改为 CS0031 式窄化报错（checker `uint32_bit_pattern` 豁免加十进制基数排除）；hex/bin/oct 字面量定型期即 int 的 ARGB 回绕、`x & 4294967295` 掩码惯用法（二元表达式通道）原样保留，显式 cast 是回绕通道。存量迁移 13 文件直写点转同值 hex（Sha1/Sha256 轮常量表、Firebird PROTOCOL_V13、FNV 种子 Focus/DataTable.Query/DataTable.Sort/Router、Charts 白色色值等 ~40 处）；掩码/比较/三元/带 cast 的 80+ 处按设计不动。钉语义：diag_decimal_literal_narrowing（新错）+ conformance_int_literal_range 重写（hex 全语境回绕+掩码+cast 通道），四红存量用例（download_job_model/hardware_intrinsics/native_memory_sha256/protocol_audit_harden）转绿，四游戏模板与 crypto_reference 编译绿，pkg_sweep 全量净账（Zan.Toml 红为并行会话未跟踪在途新包，非本批）。条目移出。

## 未完成 · IDE / 编译器

- [ ] **B-ID37** server-collab 模板再生存量工程：模板是**拆包前的平行框架副本**——231 个类内 50+ 与包同名（AppController/MetricsStore/Db/Cfg/Fmt/Cache/Mailer/Perm/Routes…），且与包**共用命名空间名**（模板 `namespace ZanWeb.Dao.Sys` vs 包同 ns 的 SysDepartmentDao；`ZanWeb.Framework.Services` 等亦然）。2026-09-30 冻结床定案（`git archive HEAD packages stdlib` + build/zanc + zanrt_*.obj 组成隔离测试床，规避并行会话在途 Zan.Data 编辑）：HEAD 工具链编译模板 57 错、**双向对称互撞**——同名 ns 合并后裸名解析命中另一侧副本：包侧裸名命中模板副本（`MetricsStore has no member FlushAtExit`、`SettingKeys has no member SiteLanguage`——模板副本落后于包的新成员；`SysUserDao` 构造重载不匹配），模板侧裸名命中包副本（基类 `ZanWeb_Web_AppController` 不匹配、`User`/`SysUser` 互转失败）。**逐文件补 using 无稳定解**（using 导入的是合并后命名空间，两份同名类同框，只是换边撞）——增量修复路线已实验证伪。影响面：仓库构建不引用该模板；发布契约绿（dist 工具链拷出编译 332 文件过，dist 包发现窄、老代际不踩）；红仅限 HEAD 自编工具链形态。方向：模板对齐包消费形态再生（删平行框架副本、改用包服务，参照 Zan.Web 迁移范式）或模板整体改名空间隔离；正落在框架拆包迁移主航道，宜与 Zan.Web 迁移同批处置（并行会话迁移中，避免对撞）。2026-10-01 普查扩及同族，server 四模板红并归本条迁移批处置面：server-legend（模板自带 `Controller/Admin/Monitor/Monitor.zan` 平行副本的 `MetricsExportDoc` 与 Zan.Mvc 包同类同名同框，报 ambiguous——本条"平行副本撞名"的单向变体）、server-iot / server-licensing（包内 AdminController.zan 引 `ZanWeb_Routes.DataIndex` / `SysUserDao` 构造重载与模板路由表/DAO 形态不匹配——框架↔模板代际错位，修法同属模板再生）。
## 未完成 · 语义决策（审计批遗留，待拍板）

- [ ] **B-ID44** 高并发主线（2026-10-01 用户拍板：高并发处理能力是卖点）——现状盘点与升级路线。现状（实证）：双引擎已有——默认 M:1 单线程驱动（协程 CPS 堆帧 + zan_co_ready FIFO 就绪队列；另有 rt_sched 有栈 fiber 路径服务 Task.Spawn/Run，128KB 固定栈 mmap 惰性物理页 + PROT_NONE guard 页 + 256 池）；`--async-workers` 链接 zanrt_io_mt 多 worker 驱动（worker 池 + 私有队列 + work stealing + 共享 IOCP，ZAN_CO_WORKERS 默认=逻辑核数，CPU 密集协程跨核扩展）。本轮已补：Task.Yield 原语（requeue 自身到就绪队尾，M:1 与 mt 两驱动同构可用）+ ZAN_CO_STACK env 旋钮（fiber 栈 64KB~16MB 可调）。**缺口**：①两驱动均无抢占/时间片——M:1 下单个 CPU 密集协程饿死全部（含 IO 回调），mt 模式稀释为 1/N 但 worker 内仍独占；缓解现状=真线程（Thread+BlockingQueue）/process-per-core 双实例，提案=循环回边安全点轮询（JVM safepoint 式，可移植无信号）或 Go 式信号抢占。②--async-workers 转.server 目标默认的成熟度/回归面评估未做。③长期路线拍板：Loom 式惰性拷贝续体（去染色+百字节级帧，需精确栈指针图）vs 全栈式 CPS（永久染色+零 fiber）；ARC 两者兼容（frame=引用计数对象，确定性回收）。测量床：tests/conformance/task_yield.zan、async_return_in_awaiting_finally.zan。

- [ ] **B-ID31** 运行时错误软着陆 vs fail-fast 默认：全仓审计批（2026-09-30）识别——部分运行时错误路径现为软着陆（记录后继续），提案是改走 fail-fast 默认（Go 的 unrecovered panic 语义）。批2b 核实相关守卫在代码里已存在，改不改默认属产品语义拍板项，审计批未动。批2 相关结论以 git 历史为准（a94129fa..29ddaae2 系列）。**决策依据（2026-09-30 盘点）**：运行时 C 层大多已 fail-fast 双态（`zan_rt_fatal` 漏斗 handler 返回也 abort，rt_timer.c:533；调度器滞留协程退出先报后停 rt_sched.c:502；GQCS 丢包有 pre-park drain 守卫）；设计内软着陆一处——GUI 每帧护栏 `zan__guard_call`（帧内硬故障隔离 + FaultCount 计数，产品语义正确，非缺陷）。真正的"记录后继续"集中在 **stdlib 异常面**：61 处 catch 仅 6 处重抛，代表性真吞点 UiErrorLog.zan:73（"无处可报；忽略"）、IconSvgData.zan:163（返空串）、GenRoute.zan 两处 rollback 空 catch、Power.zan 空 catch——改 fail-fast 的实际工程量 = 逐个裁决这 ~55 处 catch 的语义（哪些该传播、哪些是合法的错误值转换），运行时 C 层几乎不用动。

## 未完成 · 编译内存

