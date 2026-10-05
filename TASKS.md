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
- B-ID103（P2·binder）泛型类实现泛型接口的契约校验失效（`class Repo<T> : IStore<T>` 里错签名零诊断通过）：类型参数注册提前到 pass 1.5 前（resolve_bases 能真正实例化 `IStore<T>` 而非退化为模板）+ 通配符仅在接口参数未被替换时生效（替换后的类型参数按结构比较）——探针矩阵 5 例（错返回/错参数/非泛型回归/两组合法实现）+ conformance interface_assignability|interface_dispatch|generic_constraint 17 例全过。
- B-ID104（P2·checker）struct 循环检测 DFS 补三色 visited 集（原仅有路径栈，菱形依赖 DAG 指数重访挂死编译）——探针 25 层菱形 2.5s 编译完（旧版需走 2^25 条子树）、400 层直链无诊断、自环+双环反例照常报错；conformance struct|layout 29 例全过。
- B-ID105（P2·门控）SIMD 内建非 x86 落空修复：Vector128.ReciprocalSqrt 门控 x86（RSQRTP）+ 非 x86 便携 1/sqrt；Sse2.MoveMask 门控 + 便携 pmovmskb；Aes.* 非 x86 非 ARM 落到清晰编译诊断（原先 wasm32/riscv 直接炸 LLVM ISel "Cannot select"）——native 语义逐字节不变（debug 探针 + vector128_simd conformance/determinism/leakcheck 三孪绿）、wasm32 交叉编译两探针出 .wasm、Aes wasm32 得干净报错。已知独立缺陷：Vector128.Create(float) 在 wasm32 的 f64→f32 常量 bitcast 不可选，另案处理。
- B-ID106（P2·irgen）槽字装载阶梯缺 float 分支的三个副本一次修齐：① GetValueOrDefault 推断 len==16→17（原死分支永不生效，全仓扫描确认仅此一处 off-by-one）；② 查询表达式 query_loop_load 换用 load_collection_slot_value；③ foreach 元素装载（计数路径补 float 分支 + 协议路径补擦除 i64 字转换守卫）——List<float> 索引读正确而 foreach 把 3.5f 读成 1069547520.0（sitofp 数值转换而非按位重解释）暴露的根因。探针：float/double/byte/string foreach 全对 + --check-leaks 零泄漏 + 查询 where/select 3.5/4.5 + GetValueOrDefault 推断 3.5/42；nullable_value_types|null_conditional_value 六孪绿。
- B-ID107（P2·parser）批量五项：lexer_peek_n 补条件栈字段级回滚（窥探跨 #if/#else 残留压栈）；defer 链块级入口 + TK_DEFER 递归点双防护；else-if 链 parse_if_stmt 递归点防护；skip_angle_group 计 `>>`=2/`>>>=3` 闭角（嵌套泛型局部函数探测跑飞）；new T[…] 尾随 rank>16 clamp——08bcd45f。探针：p_cond3 元组括号组内嵌 #if/#else（±D 双跑 1020/23 分支均正确）、p_defer_chain 6000 层恰 1 条深度诊断、p_elseif_chain 4096 处触发不崩、p_localfunc_gen 返回 7、p_rank 恰 1 条 rank 诊断。两项审计疑点核实为无需修：union str_val 全部读点 kind 守卫在前且 lexer_make 对 token memset 清零（失败 expect 后 previous 为关键字/标点 token，str_val={NULL,0}；数字 token 原子消耗到不了名字读点）；clone_ast_subst 浅别名 probe_gspec 全对（2/1/99/2/hello），现实形状下共享安全。副产品：发现 B-ID115。
- B-ID108（P2·rt_io）首次初始化竞态：POSIX 三后端 zan_io_init 改专用 g_io_init_mx（静态初始化），分片互斥由 io_shard_mutexes_prime 在首个 init 一次性 prime（原 io_shard_open 持 sh->mx 时对它 re-init 是 UB，阻塞线程等状态被抹；首次 shard_lock 落在静态数组全零字节上仅 glibc/musl 凑效）；Windows IOCP 补 SRWLOCK 双检，g_rto_lock 改 one-shot——e2c5adab，十六件交叉 zanrt_io{,_mt}.o 全部重编（linux×3/macos×2/ios/ohos×2 经 WSL zig、android×2 经 NDK clang）；async_asocket_echo 新旧运行时各出 PING，musl ±CO_DRIVER 编译过，zanrt_io_addr_test 过。

## 未完成

- [ ] **B-ID115（P3·parser）具名元组元素变量声明解析失败**：`(int a, int b) t = G(10);` 在 `=` 处报 expected '='，匿名形式 `(int, int) t = G(10);` 与 `var (a,b) = ...` 均正常（探针 p_cond_nodir/p_cond2）。B-ID107 探针设计时发现，属能力缺口非回归，待定性后修。
- [ ] **B-ID109（P2·rt_sync）分离式 spawn 僵尸进程**：无 double-fork，detached 子进程变 zombie 常驻。位置 rt_sync.c ~3298。修法：grandchild 收养。：无 double-fork，detached 子进程变 zombie 常驻。位置 rt_sync.c ~3298。修法：grandchild 收养。
- [ ] **B-ID110（P2·rt_timer）joinmap 墓碑无收缩**，长时高吞吐定时器下表慢性膨胀。位置 rt_timer.c ~1400，镜像 zan_co_live_add 的收缩策略。
- [ ] **B-ID111（P2·rt_crash）sigaltstack 未安装**，栈溢出时信号处理器自身无栈可用，崩溃报告失效。位置 rt_crash.h ~1499。
- [ ] **B-ID112（P2·irgen）goto 出 try 的落地链降级**（B-ID99 的保守诊断改为完整降级）：goto 站点快照 finallys/armed/catch/locals，标号定义时发射清理链。外部用户代码目前会误报。
- [ ] **B-ID113（P3·编译器杂项批）**：async EH 搁浅临时（irgen_async.c ~1748）；irgen_shard.c ~238 固定 32768 表自旋；irgen.c site 表退出泄漏（site_syms/site_coll/…）；checked 上下文无符号下溢（irgen_expr.c ~4095）；ScanNotByte 常量判定（~874）；Prefetch 元素宽（~2475/2805）；插值 char/T? 洞 strlen（~6680/6695）；NaN punning memcpy（~7046）；binder 泛型元数校验；nsresolve prune 泄漏+重复诊断；genrun.c ~877 调试打印；main.c 引号/截断/malloc 检查；package.c % 白名单+realloc 检查；apk.c 错误路径释放；embedres.c 守卫次序；incremental.c 最小加固。
- [ ] **B-ID114（P3·运行时杂项批）**：fork 前 argv 置空（rt_sync.c ~3250）；capture 循环 EINTR（~3548）；reactor fd CLOEXEC；线程池创建失败记账（rt_io.c ~5769）；shutdown 竞态（~5904）；select 后端 rto_wait_ms 桩；rt_file.c ~723 gen 回绕退役；rt_timer POSIX hard-mode 打印（~466）；rt_crash.h tm_yday 时区回绕（~1239）。
