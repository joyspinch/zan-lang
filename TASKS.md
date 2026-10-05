# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：`[ ]` 未完成 · `[~]` 进行中 · `[-]` 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 `[x]` 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出，在下方索引留一行（编号+提交号）；10-03 起不再保留长叙事——此前的叙事全文在 git 历史与本清单 blob 历史及对应提交正文。

## 平账索引

- B-ID98（P1·irgen）lock/try 的 rethrow 传播路径补跑 finallys 栈下方条目（监视器不释放→死锁、外层 finally 被跳过）——0ab8ac0e，探针 `_scratch/probes/probe_rethrow`（rethrow-lock entered + 外层 finally 计数 1）。
- B-ID99（P2·irgen）goto 后向跳转补 finally/EH 解装/局部释放清理链 + 前向跨边界跳转诊断 + 重复标号诊断——0ab8ac0e，探针 probe_goto（同深度前向/带 owning 局部的后向）+ 负向两例（跨边界报错、重复标号报错）。已知限制：goto 出 try（C# 合法）目前保守报错而非落地链降级，仓库内零 goto 不受影响，完整降级见 B-ID112。
- B-ID100（P2·irgen）foreach 中途回返的集合临时/枚举器泄漏（集合临时注册为 owning 局部，复用 locals+EH 释放机制）——0ab8ac0e，probe_foreach_ret `--check-leaks` 零泄漏。
- B-ID101（P2·irgen）元组字面量/解构的 rc 元素所有权归一（按「返回结构体」+1/字段契约，decon 按来源所有权移交/保留，多余字段释放）——0ab8ac0e，probe_tuple `--check-leaks` 零泄漏（含 `_` 丢弃与调用返回元组）。
- B-ID102（P2·irgen）MoveMask 便携版 SGT→SLT 反相（Vector.Equals 全等通道掩码在非 x86 恒为 0）——0ab8ac0e。

## 未完成

- [ ] **B-ID103（P2·binder）validate_interface_contracts 先于 register_type_params 执行**，接口契约验证看不到本类型刚注册的类型参数，泛型实现接口时可能误报/漏报。位置 binder.c ~1917。修法：调序 + 探针（泛型类实现含自引用参数的接口）。
- [ ] **B-ID104（P2·checker）struct 循环检测 DFS 无 visited 集**，菱形依赖 DAG 上指数级重访，深结构编译卡死。位置 checker.c ~4402。修法：三色标记 + 探针（菱形结构链）。
- [ ] **B-ID105（P2·门控）ReciprocalSqrt / Aes.Encrypt/Decrypt 等 SIMD 内建在 wasm32/riscv 落空**（无门控回退，错译或误算）。MoveMask 已随 B-ID102 修。位置 irgen_expr.c ~2436/~3128。修法：镜像 x86 门控模式 + 非 x86 回退实现，wasm32 交叉编译探针。
- [ ] **B-ID106（P2·irgen）GetValueOrDefault 缓冲长度 16 应为 17**（NUL 截断边界）+ 查询表达式 float 槽装载未走 load_collection_slot_value。位置 irgen_expr_core.c ~2138 / irgen_expr.c ~8154。
- [ ] **B-ID107（P2·parser）批量**：lexer_peek_n 条件栈回滚缺守卫（~2318，镜像 lexer.c:1603）；defer/else-if 链缺 stmt_depth 防护；skip_angle_group 对 `>>` 的处理（~3119）；数组 rank>16 未 clamp；clone_ast_subst 浅拷贝别名（~5494）；union str_val 未判空。逐项最小探针。
- [ ] **B-ID108（P2·rt_io）首次初始化竞态**：init 检查无专用互斥，双线程首用可能双重初始化；且 pthread_mutex_init 对已持有互斥重初始化（~1511）。修法：专用 init 锁 + once。
- [ ] **B-ID109（P2·rt_sync）分离式 spawn 僵尸进程**：无 double-fork，detached 子进程变 zombie 常驻。位置 rt_sync.c ~3298。修法：grandchild 收养。
- [ ] **B-ID110（P2·rt_timer）joinmap 墓碑无收缩**，长时高吞吐定时器下表慢性膨胀。位置 rt_timer.c ~1400，镜像 zan_co_live_add 的收缩策略。
- [ ] **B-ID111（P2·rt_crash）sigaltstack 未安装**，栈溢出时信号处理器自身无栈可用，崩溃报告失效。位置 rt_crash.h ~1499。
- [ ] **B-ID112（P2·irgen）goto 出 try 的落地链降级**（B-ID99 的保守诊断改为完整降级）：goto 站点快照 finallys/armed/catch/locals，标号定义时发射清理链。外部用户代码目前会误报。
- [ ] **B-ID113（P3·编译器杂项批）**：async EH 搁浅临时（irgen_async.c ~1748）；irgen_shard.c ~238 固定 32768 表自旋；irgen.c site 表退出泄漏（site_syms/site_coll/…）；checked 上下文无符号下溢（irgen_expr.c ~4095）；ScanNotByte 常量判定（~874）；Prefetch 元素宽（~2475/2805）；插值 char/T? 洞 strlen（~6680/6695）；NaN punning memcpy（~7046）；binder 泛型元数校验；nsresolve prune 泄漏+重复诊断；genrun.c ~877 调试打印；main.c 引号/截断/malloc 检查；package.c % 白名单+realloc 检查；apk.c 错误路径释放；embedres.c 守卫次序；incremental.c 最小加固。
- [ ] **B-ID114（P3·运行时杂项批）**：fork 前 argv 置空（rt_sync.c ~3250）；capture 循环 EINTR（~3548）；reactor fd CLOEXEC；线程池创建失败记账（rt_io.c ~5769）；shutdown 竞态（~5904）；select 后端 rto_wait_ms 桩；rt_file.c ~723 gen 回绕退役；rt_timer POSIX hard-mode 打印（~466）；rt_crash.h tm_yday 时区回绕（~1239）。
