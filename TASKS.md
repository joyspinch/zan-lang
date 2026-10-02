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
> 2026-10-01 平账：**B-ID45 闭账随本批提交**——七池建连失败保持 null 契约（Acquire 记退避），失败 episode 首条在 Acquire 失败分支补诊断（NoteOpenFailed 之前问 `OpenFailing()`，天然每 episode 一条、抛与返 null 两形态同 cover）：六驱动池带连接对象 lastError 真因（MySql"无握手/认证错"、TDengine HTTP 状态等），DbPool 经 lastOpenError 字段保留 connector 抛出的异常消息（构造器须显式初始化 ""，Zan 字段无默认值——探针实测 null.Length 触发 runtime null 探针）；RedisPool 三条失败路径经 NoteOpenEpisode 补 host:port 归因。AdminController 审计兜底 catch 补 Log.Warn。死端口探针实证 6 次借出每池恰 1 条、零刷屏；Zan.Data/Zan.Mvc 整树合编 0 error。条目移出。

> 2026-10-01 平账：**B-ID31 拍板闭账（全面清扫=逐处裁决，结论零改传播）**——catch 全集 grep 133 命中逐处连上下文裁决：10 处误报（CefFingerprint/WebView2/ApiDocs 生成的 JS `catch(e){}` 字符串、CodeEditor Intelli 代码片段模板）+1 处 doc 注释，实为 **122 处真实 Zan catch 站点，全部合法，零处需要改传播**。合法形态五类各有 why：①传播/包裹/重抛（Sdk 三家 networkError 包装、DownloadJob SetFail+Finish 保界面不悬、AppController 清理错误收集完 finally 后统一重抛、CoderController 清理 staged+重抛、GenRoute 生成模板 ApiError→HTTP 换冲+回滚不吞原始异常+cleanup 条件重抛）；②合法错误值契约（/proc+/sys 探针缺席即常态、bool/null/Failure 契约、LicenseClient 状态面、池借出 null 契约、JsonValue.ParseOrStr）；③进程/循环边界记日志继续（HttpServer/Worker/WebApp 请求边界→500+指标、ClusterBus/Metrics/JobHost 后台循环、Health/Schema 探测、LoginController 登出不死因 bump）；④don't-mask 清理（AppController.DiscardLease 头注释即不变式、GenRoute rollback 空 catch、Metrics 回滚失败先摘池租约）；⑤设计内护栏（App 每帧 zan__guard_call、Log/UiErrorLog 终端错误汇无处可报）。审计快照四个"代表性真吞点"逐一辟谣：UiErrorLog:73 终端汇、IconSvgData:163 图标包探针、GenRoute rollback 生成代码范本、Power /sys 探针循环；GenScene/GenForm Parse 返 null 亦系 doc 明写的错误值契约（C 端报通用错误）。教训：**"61 catch 仅 6 重抛"是 grep 口径不是吞点口径**——吞点裁决必须逐处读上下文，探针/契约/边界天然占多数。诊断缺口剥离挂 B-ID45。条目移出。

> 2026-10-01 平账：**B-ID24 拍板闭账（对齐 C#）**——十进制字面量 [2^31,2^32] 直写 int 槽位由二补码回绕改为 CS0031 式窄化报错（checker `uint32_bit_pattern` 豁免加十进制基数排除）；hex/bin/oct 字面量定型期即 int 的 ARGB 回绕、`x & 4294967295` 掩码惯用法（二元表达式通道）原样保留，显式 cast 是回绕通道。存量迁移 13 文件直写点转同值 hex（Sha1/Sha256 轮常量表、Firebird PROTOCOL_V13、FNV 种子 Focus/DataTable.Query/DataTable.Sort/Router、Charts 白色色值等 ~40 处）；掩码/比较/三元/带 cast 的 80+ 处按设计不动。钉语义：diag_decimal_literal_narrowing（新错）+ conformance_int_literal_range 重写（hex 全语境回绕+掩码+cast 通道），四红存量用例（download_job_model/hardware_intrinsics/native_memory_sha256/protocol_audit_harden）转绿，四游戏模板与 crypto_reference 编译绿，pkg_sweep 全量净账（Zan.Toml 红为并行会话未跟踪在途新包，非本批）。条目移出。

> 2026-10-01 平账：**B-ID37 闭账随本批提交**——server 四模板全部再生为 Zan.Mvc 包消费形态（删平行框架副本/sys Model·Dao/包已接管屏，控制器改包基类+包服务，视图双键并存，rbac 归零=路由类公共 static 助手私有化）：collab c2056371（e2e_collab 113×3、e2e_im 31、e2e_realtime 25）、legend 947aebb2+2078d51e（e2e_legend 135、e2e 138、sec_probe 22）、licensing f4deaebc（activate 契约/admin 302/仪表盘与 codes 渲染）、iot b70b7b93（health/登录发 token/Bearer 全端点/kick/stats，mqtt+http 双 worker）。平台连带修复：GenRoute 路由键去重先声明者胜（包默认屏不再顶掉应用同键路由，100525a0，conformance web_route_duplicate_key 钉行为+collab 113 回归）。发布契约实测：iot fresh-dir --publish 331 文件全装运（pq 三件+sqlite3+zan_gui），发布产物净目录实机 boot（mqtt 1883+http 8080、SQLite 自动建库）、/health 200、/admin/stats 未认证 302、bootstrap 登录发 token 后 Bearer 打通 me/stats/clients。排查记录：曾现 "driver 'sqlite3' was not bundled" 系床目录残留 e2e 服务器进程（legend_r9.exe）持自身导入 DLL 的拒绝写句柄，zanc 普通条目拷贝失败静默（装运循环无失败输出），聚合警告误指"清单缺失"——杀进程即全绿，非编译器正确性缺陷，诊断口径缺口挂 B-ID52。余账拆条：collab e2e_realtime 线上帧 ~1/3 偶发丢失挂 B-ID50；pq.bundle @driver/ssl+crypto 在 win-x64 无对应 bundle 挂 B-ID51。另记：台账 B-ID48 现被两条占用（OCR 撤案存档/定时器期限钟），B-ID49 已被闭账提交 61ac40e3 占用——历史条目不改号，新条目自 B-ID50 起。

> 2026-10-02 平账：**B-ID52 闭账随本批提交**——zanc 发布装运（--publish）驱动拷贝诊断加固：重构 `zan_copy_file_ex`，在拷贝失败时详细捕获 errno 与 Windows GetLastError() 系统错误代码；普通驱动条目与 `@driver/` 依赖条目拷贝失败时均输出具体 warning（含源/目标路径与系统错误码）；聚合警告精准区分"目录中没有运行时库"与"目标文件被占用或写权限不足导致拷贝失败"，消灭运行中残留进程持 DLL 锁导致误导用户补清单的伪报错。测试验证：已通过 SqliteConnection 实体实机构建并对目标 DLL 实施独占锁定探针验证，准确捕获 winerr 32（ERROR_SHARING_VIOLATION）并输出准确诊断。条目移出。

> 2026-10-02 平账：**B-ID51 闭账随本批提交**——PostgreSQL win-x64 pq 驱动装运与 OpenSSL 依赖消除伪告警：实测证明 Windows 预编译 `libpq.dll` 的 PE 静态导入表硬性包含 `libcrypto-3-x64.dll` 与 `libssl-3-x64.dll`（若缺失会导致目标机启动报找不到依赖 DLL）；由于此前纯 Zan Tls 重构剥离了 stdlib 的 OpenSSL 原生驱动，win-x64 pq.bundle 引用的 `@driver/crypto` 与 `@driver/ssl` 成为空悬引用。修复方案：将对应版本的 `libcrypto-3-x64.dll` 与 `libssl-3-x64.dll` 收敛内置进 `packages/Zan.Data/.../drivers/win-x64/` 驱动实体目录，更新 win-x64 `pq.bundle` 直接声明打包此二件 DLL。实机验证：使用 `PgConnection` 实体进行 `--publish` 构建，192 个文件完整装运，消灭 2 条 `@driver` 缺失告警，产物目录具备完整的 libpq/libcrypto/libssl/libiconv/libintl 5 件套，实机运行正常握手建连退出。条目移出。

> 2026-10-02 平账：**B-ID46/47/48 闭账归档移出**——B-ID46（NativeMemory 缺 double↔u64 位型 reinterpret 原语，以 AsI64/AsF64 bitcast 内建闭账）；B-ID47（wasm32 async _setjmp 链接桩与交叉运行时对象重编闭账）；B-ID48（Windows 定时器期限钟微秒化与 timeBeginPeriod 停车粒度配对闭账）；B-ID48（OCR 斜排文本支持，因 Zan.ML/Zan.OCR 两包整体移除暂缓撤案，历史存档待后续恢复）。条目全部移出未完成区。

> 2026-10-02 平账：**B-ID53 闭账随本批提交**——Vector128/Vector256 的 ExtractMostSignificantBits/MoveMask 内建在非 x86 目标无条件发 `llvm.x86.sse2.pmovmskb.128`/`llvm.x86.avx2.pmovmskb`，AArch64/RISCV 后端 codegen 直接 `LLVM ERROR: Cannot select` 崩溃（ohos-arm64 交叉 task_yield 实爆；stdlib MemoryExtensions/StringExtensions/NativeMemory 的向量搜索路径全中）。修法：emit_target_is_x86 判 triple（空 triple=宿主，编译期 `__aarch64__` 兜底）——x86 保留 intrinsic 快路，其余目标发可移植 IR（`icmp sgt <N x i8> zeroinitializer` 取符号位 + 逐 lane extractelement/zext/shl/or 聚合，AArch64 CMTST/wasm/RISCV 均可选中）；`Sse2.MoveMask` 属 x86 专有 API 面不改语义。验证：vprobe（v128=1/v256=0x10001/全 1=-1 与 pmovmskb 语义一致）原生实跑 + linux-x64/macos-arm64/win-arm64 交叉全过、ohos-arm64 IR 零 pmovmskb、task_yield 五目标链接绿。同面残留已收（同日 9e55a9f1）：Vector128.Shuffle/Average、Vector256.Average 同法改双路——x86 保 ssse3.pshufb.128/sse2.pavg.b/avx2.pavg.b，其余目标发可移植 IR（Shuffle=逐 lane extractelement+动态 gather+select 实现置零语义；Average=(a&b)+((a^b)>>1)+((a^b)&1) 无进位恒等式，逐 lane 和 ≤255 不跨字节进位）。验证：原生探针语义钉死（Shuffle 含高位置零与 &0xF 回卷、Average 含 255+255/255+254 舍入边界）+ ohos-arm64/macos-arm64/win-arm64/linux-x64 四目标链接全过 + ohos IR 零 x86 intrinsic。坑沉淀：LLVM C API 的 LLVMBuildLShr/And 不自动 splat 标量常量到向量操作数，须 LLVMConstVector 显式铺（LLVM verification "Both operands to a binary operator are not of the same type"）。

> 2026-10-02 平账：**B-ID50 闭账随本批提交**——server-collab 实时链路（`e2e_realtime` / `e2e_collab` / `e2e_im`）偶发丢帧与时序窗口根治：
> 1. 水位竞态窗口根除：CollabEventRelay 与 MessageRelay 在服务对外接收连接前由 `main.zan` 启动期显式执行 `InitWatermark(qdb)`，锁定存量已落库的最大 id 作为历史基线；彻底杜绝过去首拍执行与外部新事件落库交织时，首拍被延迟调度导致启动瞬间的新事件被误当作历史静默跳过的竞态漏洞；
> 2. 跨进程孤儿 worker 残留清理：Windows 下 `stop_server` 在杀主进程树后追加基于可执行文件名的 `/IM` 强杀保底，杜绝孤儿 worker 攥住 8090 端口并向后续测试进程注入脏响应；
> 3. 自动化测试探针与代理绕过加固：`e2e_realtime.py`、`e2e_collab.py`、`e2e_im.py` 增补本地环回 `NO_PROXY` 隔离，重连接收帧采用 deadline 轮询窗口（消除测试侧对单拍抵达时刻的刚性假设）。
> 验证证据：`e2e_realtime` 实机连续 50 轮高强度压测 100% 满分通过（50/50 runs, 25/25 checks 全绿，零 transport anomaly）；`e2e_collab` 113 项全绿；`e2e_im` 31 项全绿。条目移出未完成区。
>
> 2026-10-02 平账：**B-ID55 闭账随本批提交**——stdlib Stopwatch.NowTicks 与 Frequency 消除 NativeMemory.Alloc(8)/Free 堆分配：
> 1. C 运行时 `rt_sync.c` / `rt_sync.h` 增补 `zan_stopwatch_ticks`（Windows 栈局部 `LARGE_INTEGER` 直读 QPC、POSIX 走 `zan_monotonic_ns`）、`zan_stopwatch_frequency` 及同义别名 `zan_monotonic_ticks` / `zan_monotonic_frequency`；
> 2. `Stopwatch.zan` 静态缓存 `cachedFrequency`，`NowTicks()` 与 `Frequency()` 全面接入 C 原生直读，彻底消除每拍读钟调用对 CRT 堆锁的竞争与多核串行化；
> 3. `src/compiler/irgen_emit.c` 增补 `zan_stopwatch_` 符号前缀绑定规则，与 `toolchain/win-x64/zanrt_sync.o` 重新编译同步。
> 验证证据：`stopwatch_timer`、`crossplat_stdlib`、`ffi_widths`、`bytebuffer` 四族共 24 项全套孪生测试（conformance/determinism/leakcheck/arcguard）100% 全部通过。条目移出未完成区。

> 2026-10-02 平账：**B-ID58 闭账随本批提交**（原误记 B-ID56，与本批已挂账的 WhenAll/WhenAny 完全事件化重号，改号顺延）——字符串 `+` 拼接丢内嵌 NUL 字节：`emit_str_concat_n`（n 路拼接）逐叶取长度走 `emit_cstr_len_of`，非字面量操作数回落裸 `strlen`——管理串带 U+0000（`Encoding.GetString` 过二进制字节、`Utf8FromCodePoint(0)`）时长度被量成 NUL 前缀，memcpy 字节数同步缩水，`"a" + nul + "b"` 静默产出 `"ab"`（探针实测 len 2、字节 61 62）；成对版 `emit_str_concat` 与 `__zan_str_ocmp` 早已长度感知，唯独此路漏网，与 A279（UrlDecode `%00` 缩短 append）同类。修法：`emit_cstr_len_of` 的 strlen 回落改 `emit_string_length`（缓存 ARC 头长度 / byte[] 元素数 / 裸 extern char* 才 strlen），字面量编译期常量快路保留。验证：_scratch 探针（拼接后 `61 00 62`）+ 新固定用例 tests/conformance/string_concat_nul.zan（双叶/Utf8FromCodePoint/四叶链三形态）conformance+leakcheck+arcguard 孪生 3/3 绿；BSON 值模型 NUL 键拒绝路径（`CStr` 扫描抛 BsonException）由此可达。

> 2026-10-02 平账：**官方语料互操作深化收官**——MongoDB bson-corpus（oid/boolean/int32/int64/string/datetime/document/array/binary/double 十类型）与 kawanet msgpack-test-suite（nil/bool/binary/正负整数/浮点/bignum/ASCII+多字节+emoji 字符串/数组/map/嵌套/timestamp 全语义组）真实字节向量入库为 tests/conformance/bson_corpus 与 msgpack_corpus（配 .out，canonical 解码断言 + 值语义编码命中语料规范字节 + decodeErrors 必抛）。语料钉出并修复三处（分别随 b4116ce3/1fcaeee6/8d04737a 入库）：① BSON 读侧收紧——bool 值严格 0x00/0x01（corpus 0x02/0xFF 为 decodeError，旧 `U8()!=0` 全收）、binary 过时 subtype 0x02 校验内嵌长度（3 个错误向量）；写侧放开串值内嵌 NUL（corpus "Embedded nulls" 合法——长度前缀消歧，键 cstring 才禁 NUL）。② stdlib DateTime civil-from-days 的 `era = z / 146097` 用截断除法，z 为负（proleptic 公元 0 年，套件 `[-62167219200,0]` 向量）时 doe 变负、日历崩成 `1-2--29`——改 FloorDiv 后 `0000-01-01T00:00:00Z` 正确（Hinnant 原算法要求 floor；构造器与 CivilFromDays 两处，DaysFromCivil 早已正确）。③ MsgPackReader 64 位 timestamp 的 sec 掩码误写 2^35-1（34359738367），nanos 最低位为 1 时 sec 多吃一位、日期整体漂移（旧 msgpack_values 向量恰好 nanos 最低位为 0 而漏网）——纠正为 2^34-1（17179869183）。钉死范围与值模型的边界（int64 语料保型只钉解码、u64 超 long 收敛 double、任意 ext 值模型外、invalid UTF-8 尽力收敛）均在测试头注释里写明。连带暴露 **B-ID57**（System.Json `Parse("-0.0")` 丢负零符号，见未完成区），测试内改 `NewDouble(-0.0)` 直构钉 BSON 位型。验证：_scratch 探针（bool/binary 新校验、NUL 串往返 25 字节精确比对、负毫秒 1960-12-24T12:15:30.499Z、Y10K 10000-01-01、公元 0 年、9999-12-31T23:59:59.999Z 极值 timestamp、u64 高位收敛 double）+ 窄 ctest 24/24（bson_corpus/bson_values/msgpack_corpus/msgpack_values/cbor_values/datetime_civil × conformance/determinism/leakcheck/arcguard 四形态）。

> 2026-10-02 平账：**B-ID57 闭账随本批提交**——System.Json `Parse("-0.0")` 保留 IEEE 754 负零符号位型：`JsonValue.DoubleOf` 与 `JsonDoc.DoubleInRange` 在遇到负号标记 `neg == true` 且数值为 0.0 时，原先裸返回 `0.0 - v` 在部分浮点算术下得到 `+0.0`，修正为显式返回 `-0.0`；使 `JsonValue.Parse("{\"d\":-0.0}")` 以及 `JsonDoc.Parse` 均能精确保留负零位型（`NativeMemory.AsI64` 为 `-0x8000000000000000` / `0x8000000000000000`）；`tests/conformance/bson_corpus.zan` 的 `dbl-negzero-enc` 移除了 `NewDouble(-0.0)` 临时直构，换回标准 `JsonValue.Parse("{\"d\":-0.0}")` 入口；`tests/conformance/json_number_precision.zan` 增补负零往返断言（JsonValue 与 JsonDoc 两路）。验证：`json_number_precision` 与 `bson_corpus` 全套 8 项孪生测试（conformance/determinism/leakcheck/arcguard）100% 全部通过。条目移出未完成区。

> 2026-10-02 平账：**B-ID44、B-ID54 闭账归档移出**——根据全仓审计与提交历史（edeca1fc、9ac4d4b4、39147246、ccfcba42、99428c5b、9fb9aaf7、bd40f4a4 等），高并发主线与 mt 多核退化各分段均已全面落地并验证：
> 1. B-ID44：M:1 与 mt 多 worker 双引擎实证默认即开、Task.Yield 让渡原语、回边协作抢占（zan_co_poll 门控+dispatch 打点）、忙碌路径到期定时器泵、微秒单调钟切片（zan_co_precise_us）、调度公平 61:1 饥饿修复（批量取注入+环尾自重排）、FINEXC 嵌套定长帧体积优化（最小帧 5264B→168B）、全交叉平台运行时对象刷新与验证全绿。
> 2. B-ID54：① 让渡路径多核开销归因反转（门控前移消灭逐回边跨模块调用与 TLS 读，多核开销归入硬件噪声）；② spawn 提交侧多核退化根治（worker 环 head/tail 分缓存线隔离伪共享，steal_ok -85%、park -79%）；③ WhenAll/WhenAny 汇合让渡化（先 Task.Yield 8 圈再 Delay 指数退避，小扇出汇合延迟 -66~88%）。
> 相关结论、基准探针（_scratch/async-bench/*）与孪生测试已全部入账，条目移出未完成台账。

> 2026-10-02 平账补记：**B-ID44 ③长期路线拍板落定——留 CPS（用户确认）**——Loom 式惰性拷贝续体不采纳，高并发主线最终形态即上述已落地现状：堆上 CPS 帧（ARC、按实际 try/finally 深度定长）+ M:1 内联与 mt 多 worker 双驱动同构（native 64-bit 默认 mt）+ 回边 zan_co_poll 1/256 时钟门控 + 2ms 微秒量子（ZAN_CO_QUANTUM_MS 可调）+ WhenAll/WhenAny 汇合让渡化；有栈 fiber 仅服务 Task.Spawn 同步代码兼容路径，不进 async 主线。依据（全部实测）：续体切换 126-172ns（单帧自重排/双帧 FIFO 交替），与 Go goroutine、Loom mount/unmount 的生产量级持平；最小帧 5264B→~168B（31×）、10 万活任务 530MB→17MB——Loom 的"百字节帧"卖点已实质达成；spawn 入队 130-175ns/全程 400-465ns（旧账 3.1-3.6µs 系 WhenAll Delay 轮询被期限钟 tick 量化的测量方法学污染，非真实成本）；k=32 全让渡墙钟 2.26× vs 零让渡硬件底 2.24× 同形——调度器本体已贴硬件极限。Loom 不采纳的理由：需精确栈指针图 + 恢复时栈重建 + ARC 帧语义重推，工程量数倍，而切换/帧体积/spawn 三个硬指标实测零收益，"去染色"是生态已付的沉没成本。唯一后续优化=B-ID56（WhenAll/WhenAny 完全事件化，挂账中，收益上限小扇出汇合 ~1-2µs，接近 spawn 本身的物理成本）。

> 2026-10-02 平账：**B-ID56 闭账随本批提交（e8614c8c + 01ab2c49 + fd5e40e9）**——WhenAll/WhenAny 完全事件化，替换 B-ID44⑥的让渡混合轮询。挂账时的 aw 槽 CAS 设计被侦察否决：spawn 帧的 awaiter/awaiter_step 槽在 emit_detach_async_call 就被预填 `self+reap_fn`（spawn/reap 生命周期自用），借槽必撞。落地设计=joinmap+完成钩子：运行时全局开放寻址 map（frame→pair，与 live 注册表同一把自旋锁），Task.JoinNew/JoinBind/JoinCancel/JoinWait 四内建（irgen_call.c 直发 + irgen_expr.c await 挂起形，值走 ASYNC_FRAME_RESULT 槽两路汇于 resume-k）；**主通知点 zan_join_complete 挂在 emit_async_complete**（DONE 发布后每帧恰过一次——keep_result 的 Task.Run 结果帧完成后不被回收只置 done，untrack 永不触发，第一版只挂 untrack 时 WhenAll(Run 帧批) 等到调度器无定时器可排、静默排水 rc=0，wa_n1s 探针 Spawn n=1 过/Run n=1 死钉出）；untrack（zan_co_live_del）留作兜底。joiner 以 Delay 形挂起（挂起路径禁 self-ready，运行时持 (frame,step)），fire 经 g_ready_hook 于锁外（mt 与 M:1 双驱动均置）。bind=live+未 done+未重复三查；删除按精确指针+值匹配（地址复用不误伤）；JOINMAP_TOMB 探测前置守卫；join_any_done(空对)=1。无 join 程序 `!g_joinmap_cap` 免锁快路径。stdlib TaskJoin.zan 事件快路径 + WhenAllPoll/WhenAnyPoll 回退（无钩子/非协程时 r==2 自动退）。收益实测：8×Delay(5ms) 扇出 5.8-6.0ms 真挂起（原 6-26µs 轮询节奏项清零，join 分量 ≈49ns/任务=untrack 完成路径摊销）；probe 11 场景×3 连跑全绿（含 WhenAny 400 轮迟完成浸泡）；wasm32 M:1 事件 join node 实测同过；ctest when_all|async_mt|async_when 16/16（连带修 async_mt 硬编码端口 46138→44500——Windows 排除端口范围轮换覆盖致 rc=10061）。交叉 .o 六目标重建随库（macos/ios 按约还原）。

> 2026-10-02 平账补记：**B-ID19 归因勘误+独立复证（修复早已随 46a7e83a 当晨落库，台账回填）**——真实触发面不是"裸 await 语句"：try 内 return 的返回值此前溢出到 $resume 入口 alloca，finally 含 await 挂起恢复时入口块重执行出未初始化栈格，裸 await/接住结果/循环 try-return-finally 三形态全坏（挂账时"裸语句是触发面/接住结果即安全"的观察是形状巧合）。46a7e83a 改走堆 frame RETSPILL 槽修愈并留常设用例 async_return_in_awaiting_finally；本会话以疤痕编译器（e91f9ce7，修复前一提交）考古独立复证：四形探针 E/F/G/H 全返假 false、HEAD 十九形态探针全绿、finally 族孪生 8/8；同日 a94129fa（完成握手 acquire/release）是 DONE/RESULT 陈旧读同症状的第二层修复。skill zan-dev-standards 坑⑱已同步勘误三副本；packages/Zan.Mvc Schema.zan 内"编译器挂账"注释已过时（包文件归原会话维护，留其顺手清）。

> 2026-10-02 平账：**--fast-alloc native 链路修复并实测入账**——native 的 zanrt_mem 对象与 cmake 接线早已随 zanc 就位，但 Windows 上一开 `--fast-alloc` 就加载即死：lld 20.1.8 的 PE `--wrap`（GNU 仿真）把导入槽改写成 `__imp___wrap_malloc` 别名，wrap 从未生效、进程在 main 之前死（msys exit 127、零输出）——旗标实质不可用，这正是 B-ID54 残余"CRT 堆锁"在默认构建上始终真实存在的原因。修法（main.c）：链接器选择处 rt_mem_obj 存在时改用捆绑 GNU ld 2.36.1（同对象同 CRT 全绿；lld 保留给大对象常规链接），Apple native 在对象解析处豁免（ld64 无 --wrap，镜像 8792 共享库守卫），新增 `ZAN_LINK_ECHO=1` 回显完整链接命令。验证：fa 探针 `zan_mem_slabs` 8→14 证明 wrap 生效、5 连跑稳定；string_concat_nul/task_yield/async_return_in_awaiting_finally 带旗标对 golden 全 MATCH；mtscale 实测 win-x64：k=1 spawn_us 1002→693（-31%，三轮中位数，纯 spawn 提交路径收益）、join_us 不变（汇合已由 B-ID56 事件化主导）、fanout k=4 5→3-4µs/批；**k=32 交错复测出反向信号**（B-ID56 事件化后 WhenAll 睡眠噪声源已除）：fa spawn_us 中位数 8294µs vs def 7251µs，6/7 对连败（+14%）——单 producer/32 consumer 形状下所有跨线程 free 原子 exchange 到同一 owner 的 remote 头部（单点竞争），UCRT 堆（Win10+ 自带每线程缓存）反而更快。**回归当场修掉：remote 分片 8 条 64B 条带**（rt_mem.c：freer 用自己 cache 指针亲和选条带、无 cache 线程轮转回退，drain 遍历条带），复测 k=32 fa 中位数 7495 vs def 7632（回归消失，3/6 对互胜，1 个 10725 离群为批睡眠形状）、k=1 仍全胜（948 vs 1212，-22%，安静机时 -31%）；rt_mem_remote_test 首次在 Windows 手动构建跑过（cmake 守护 `NOT WIN32` 一直挡着，FlsAlloc 路径首获覆盖）；toolchain/linux-musl/zanrt_mem.o 随源重编（zig 0.15.1 同配方），交叉 string_concat_nul WSL 实跑对 golden MATCH。分配器默认仍 opt-in——翻默认依据已齐（两形状均不劣，k=1 显著优），同日用户放行，翻默认落地见下条。坑沉淀 zan-compiler-internals 三副本（lld PE --wrap 与导入符号不兼容）。

> 2026-10-02 平账：**--fast-alloc 翻 native 默认（用户"继续"放行）**——main.c 三处：旗标解析改三态 `fast_alloc_opt`（0=默认 / 1=--fast-alloc / -1=--no-fast-alloc），对象解析处 `fast_alloc = fast_alloc_opt >= 0` 即 Windows/Linux 原生默认链 zanrt_mem；"无对象"告警只在显式 `--fast-alloc` 时发（默认态对象缺失静默回退 CRT，Apple 原生每链必告警的问题一并消掉）；`--no-fast-alloc` 显式退出（排查堆问题/块交还外部 CRT），与 `--fast-alloc` 同现后写生效（同 --obfuscate-strings 对惯例）。交叉策略零改动：musl 自动、Android 永不、其余交叉不涉及。验证（不带任何旗标即新路径）：ZAN_LINK_ECHO 确认默认链接含 zanrt_mem.obj、`--no-fast-alloc` 不含、两种顺序的双旗标各自胜出；string_concat_nul/task_yield 对 golden 全 MATCH；mtscale 交错实测 k=1 spawn_us 中位 800µs vs CRT 1088µs（-26%，延续既有收益）、k=32 默认 3/3 对全胜（8584 vs 9571 中位）——默认≡原 fa 形状在两压力形状均成立。

> 2026-10-02 平账：**B-ID59 闭账随本批提交（用户质疑驱动破案：ios/win 全部本机修掉）**——挂账时"本机无合规重建环境"的判断错了两处。ios 五对象（3e284401，提交时区 +0800=本机产物）的屏障只是 zig 的 ios os-tag 不自动接 libc 头，而 zig 自带 Darwin 头集 any-macos-any——`-I <zig>/lib/libc/include/any-macos-any` 手动塞入即通；win 系对象指纹是 github-actions[bot]（-0700），确认 CI drivers.yml MSYS2 产物、本机从未装过该环境，但 zig -windows-gnu 产出 ABI 等价对象。修复：ios 六对象（含 gui 统一 zig 风味）与 win 双架构各五对象全部重编（timer nm zan_join=5）；验证 ios-arm64 WhenAll 探针真链接通过（修复前 undefined _zan_join_new 实爆）、win-x64 链接后本机实跑 ok（事件汇合真实执行）、win-arm64 链接 coff-arm64 干净。配方入册：build_cross_rt.cmd iOS 段（头 -I + 五对象）、check_toolchain_stale.py（ios 条目转 runtime 组 + rebuild_cmd ios 分支，--verify 字节自洽 6/6；win 保持 report-only——CI MSYS2 字节风味与 zig 不同，不做本地字节假设）、do_rebuild 补 ios 块。zig 0.15.1 已从 Downloads zip 恢复至 D:\tools 标准位（本会话中途 D:\tools 被挪用致工具一度失踪）。教训沉淀 zan-compiler-internals 三副本：zig os-tag 头接线规则（ios 手动喂 any-macos-any、win 可 zig -windows-gnu 本地造）；"本机没这个环境"结论前先做对象指纹考古（llvm-readobj/nm producer 串 + git 提交时区），别把 CI 产物当成本机能力边界。

> 2026-10-02 深度审计挂账：用户质疑"是否没有潜在问题"驱动全仓重审——8 领域
> 并行审计（运行时 C 内存/并发×2、编译器 C、stdlib 全量、格式解析包、
> Web/文件安全、SDK/加密、跨平台+性能），覆盖约 62 万行；主会话抽样复核
> 15/15 全实锤（含 2 个编译运行探针：枚举折叠、插值 NUL）。此前"台账清零"
> 仅对已审计过的面成立；本轮证明解析器写侧、POSIX 路径、包消费面、设计器
> HTML 原语、许可证客户端此前从未入册。新挂账 B-ID60..B-ID84。标注约定：
> 【实锤】=主会话读码/探针亲证；【代理】=审计代理读码确认（抽样校准 15/15）；
> 【疑似】=需探针定案。

## 未完成

- [ ] **B-ID60【实锤·探针】（P1）zanc 枚举初始化器只认纯整字面量，静默折叠错误值**——
  `enum Color { Red = -5, Green }` 实测 `(int)Color.Red`=0、Green=1（应 -5/-4）；
  `enum Mul { X = 5, Y = X * 2 }` 实测 Y=6（应 10，回落运行计数器）。根因：三处消费点
  （irgen_expr.c:7078 成员折叠、irgen_call.c:404 反射表、irgen_reflect.c:324）只认
  `AST_INT_LITERAL`，而 parser 不折叠负号/表达式（parser.c:1889 产生 UNARY）；checker
  不校验枚举值形态。附带 `(int)int_val+1` 在 INT_MAX 有符号溢出 UB。修法：checker 期用
  既有 const_int_expr（irgen_expr_core.c:283）统一折叠回写 AST_INT_LITERAL + int 域宽校验；
  非常量报错。探针形态：`_scratch/audit_enum_neg.zan`（已删，本条含全部触发形态）。
- [ ] **B-ID61【实锤·探针】（P1）zanc 字符串插值 `$"{...}"` 丢内嵌 NUL + 长度头虚报带出堆垃圾**——
  B-ID58 修 `+`/concat_n 路的同族漏网：emit_expr_string_interp（irgen_expr.c:6572-6585）用
  strcpy/strcat 逐段拼（首 NUL 截断），而 total_len 走 emit_string_length（NUL 计入）→
  头长>实写字节，Substring/GetBytes 读到 malloc 未清零垃圾。探针实测 `$"v={含NUL};"` →
  `76 3d 61 3b 00 00`（'b' 丢失 + 2 字节垃圾）。修法：照搬 emit_str_concat_n 的
  memcpy 逐叶拷贝。
- [ ] **B-ID62【实锤】（P1）stdlib Directory POSIX 目录枚举恒返回空**——
  Directory.zan:124 `readdir` 误声明为返回 `string`：extern 裸 char* 的 Length=strlen(偏移0)，
  struct dirent 首字段 d_ino/d_reclen 必含 0x00 → Length≤17 < nameOff(19/21) →
  posixName(:379) 循环体零执行恒返回 ""。GetFiles/GetDirectories/GetPaths 在
  linux/macos/ios/ohos/android/wasm32 全部静默空列表；DeleteRecursive 因此什么都不删。
  编译器注释（irgen_call.c:1946-1950）正好拿 struct dirent* 当不可靠字符串典型。
  修法：readdir 返 nint + 按 d_reclen/d_name 偏移取字节复制（≤256）。
- [ ] **B-ID63【实锤】（P1）stdlib Directory.DeleteRecursive 跟随符号链接删除越出目标树**——
  posixList(:415)/DeleteRecursive(:581) 用 opendir 探目录性（follow symlink），指向树外
  目录的 symlink 被递归进入逐个 unlink；Windows junction 同型。与 B-ID62 联动：62 修复
  （枚举恢复工作）即激活本洞，必须同修。修法：lstat/fstatat(AT_SYMLINK_NOFOLLOW) 判定，
  symlink 一律 unlink 本身不递归。
- [ ] **B-ID64【实锤】（P1）stdlib Gate.Wait(timeout) 看门狗对已 Close 句柄发信号 = UAF 写**——
  Gate.zan:54 Spawn(Watchdog(handle))，:64 延迟后 zan_gate_signal(裸 long 句柄)；Wait 被
  Signal 提前唤醒后调用方 Close()（:76 zan_gate_free），看门狗到点仍写已释放 reactor 对象。
  文档只论证"盈余信号无副作用"，未论证"已释放句柄无副作用"。修法：Close 递增代际、
  runtime signal 前校验，或超时内建进 gate 生命周期。
- [ ] **B-ID65【代理·实锤级】（P0）Zan.AppUpdate zip-slip 任意文件写**——
  UpdateFileEntry.NormalizePath(:32) 只换分隔符剥前导斜杠、不滤 `..` 段；
  AppUpdater.ApplyPackage(:137-146) `destFile = targetAppDir + "/" + rel` 直写；
  TamperDetector.RestoreFromZip 同型。恶意/被劫持更新包 `../../x` 条目即可写应用目录外
  （配合 B-ID66 无签名=更新即 RCE）。Zan.IO 的 Tar.SafeName 安全原语已存在被绕开。
  修法：复用 SafeName 判定，`..` 段 fail-closed。
- [ ] **B-ID66【代理】（P1）Zan.AppUpdate 更新链完整性三缺口**——
  (a) manifest+zip 零签名：MD5-only 只是完整性非真实性（expectedMd5 来自未签名本地清单，
  通道攻击者同时替换两者即可）；stdlib Ed25519/Rsa 已可用未用。(b) 覆盖写非原子：
  ApplyPackage 直写活文件，断电即截断可执行、连自愈都起不来；应临时名+原子 rename
  （CoderController.WriteSources 有范本）。(c) backupOld 只建目录不拷贝，回滚不可能。
- [ ] **B-ID67【实锤】（P0）Zan.Mvc Admin Wiki Markdown 渲染存储型 XSS**——
  WikiController.Html(:456) 头注释承诺"先整行转义再叠加标记"，实际仅代码围栏分支转义
  （:479）；标题/列表/引用/有序/段落 7 个 Inline 调用点（:488/:494/:500/:511/:522/:535/:539）
  全部直传原始行，Inline 注释自认"输入应先经过 Esc"（:566）。持 wiki 编辑权者保存
  `<img onerror=...>` 即对每个查看者执行（含 AI 整理内容同一出口）；HttpOnly 挡 cookie
  外带但脚本可同源调管理 API。修法：7 处调用先 Esc（与代码块一致）。
- [ ] **B-ID68【实锤】（P0）Zan.Commercial 许可证状态零认证，"signed"模式整体可伪造**——
  LicenseClient.zan:124-145 状态文件 ~/.zan-license/**/license.json 明文 JSON 无 HMAC/签名；
  Check()(:207) `mode=="signed"` 仅判本地明文字符串即放行（手写 JSON=终身授权）；
  online 宽限（last_seen/grace_seconds 可改）与 device_fp 比对同样落在可编辑明文上。
  全文件无任何验签调用。修法：状态文件 HMAC（派生自本机不可移植因子）或 signed 模式
  内嵌证书 + Rsa/Ed25519 验签取 expiresAt。
- [ ] **B-ID69【代理·实锤级】（P1）Zan.Desktop TaskScheduler Windows 命令注入**——
  TaskScheduler.zan:382 Quote() 不转义内嵌 `"`，:146-184 name/trigger/time 未验字符集直拼
  schtasks 命令；执行通道 Process.zan:156/257 走 `cmd.exe /c`（类文档自认 shell 语义）。
  name=`x" & calc.exe & rem` 即 RCE；Quote 的存在诱调用方误信已消毒。连带：预测名临时
  文件（zan_cap_<pid>_<tick>.tmp / zan_crontab_<pid>.tmp，cmd `>` 重定向跟随 symlink）。
  修法：name 白名单字符集、trigger/time 枚举校验、临时名加随机 token+CreateNew。
- [ ] **B-ID70【代理】（P1）Zan.Sdk.Wechat 验签面四缺口**——
  (a) WechatPayV3 响应验签默认 fail-open：WechatPayConfig.zan:48 RequireResponseSignature
  默认 false，缺头/缺平台密钥时静默放行（可注入平台证书扩大成回调伪造链）。
  (b) 通知验签无时间戳新鲜度（WechatPayV3Client.zan:134-140），截获"支付成功"通知可任意
  时刻重放。 (c) CheckSignature.zan:16 DefaultToken="weixin" 公开常量回落，token 忘配即
  签名形同虚设。 (d) 三处签名比较非常数时间（WXBizMsgCrypt:70/CheckSignature:21/
  WechatPayV2:94）+ WXBizMsgCrypt:148 `20+msgLen` i32 上溢绕过边界检查（需合法对端前置）。
  修法：(a) 默认 true fail-closed；(b) 内置 |now-ts|≤300s；(c) 空 token 拒绝；(d) 换
  fixed-time 比较（Jwt.fixedEquals 范本）+减法比较。
- [ ] **B-ID71【实锤】（P1）runtime zan_audio.c 两处内存不安全**——
  (a) WAV 解析 chunk 长度 (int) 截断（zan_audio.c:321-332）：len≥2^31 时
  `pos += 8 + (int)((len+1)&~1u)` 变负 → pos≈-2GiB 继续循环 memcmp 野读；fmt 分支
  `pos+8+(int)len<=n` 对负 len 恒真 → fmt 指针出界读 ≤16 字节。≥20 字节恶意 WAV 即触发。
  (b) WASAPI 混音声道数未钳上限（:435-441）：devch 只钳下限，>8 声道设备（空间音频/
  虚拟声卡）memset/累加写穿 4096×8 静态 acc；姊妹函数 mix_s16/mix_f32 都有钳制，唯独
  此路径漏。修法：(a) 全程 u32/int64 比较+拒 len≥2^31；(b) 补同款三段钳制。
- [ ] **B-ID72【实锤】（P2）runtime rt_sched.c 是编不过的孤儿文件仍在 staleness 入册**——
  :35 未闭合 `/*` 吞掉 :40 `#define ZAN_CO_STACK_DEFAULT` 与 co_stack_size()（终止符在
  :61）；:189 引用的 ZAN_CO_STACK 全仓无定义——任何非 ZAN_IO_STACKLESS_ONLY 形态编译
  即炸；check_toolchain_stale.py:36 仍列为 RT_IO 输入。修法：补 `*/` 并统一宏名（文档
  ZANC_CLI.md 用 ZAN_CO_STACK），或删文件并同步删入册条目。
- [ ] **B-ID73【代理·疑似】（P2）zan_co_sched_run 入口 TOCTOU 双 owner**——
  rt_io.c:5377 普通读检查 live/fg 后 :5421 无条件 InterlockedExchange 置位：两线程同窗口
  通过检查 → 各建一套 worker 落同一 g_wk[i]，环协议被破（丢任务/协程不恢复）。root-await
  形态（Thread.Start 内 Task.Wait）是文档支持入口。修法：入口 InterlockedCompareExchange
  准入，输者走等待分支。
- [ ] **B-ID74【代理】（P2·性能）runtime 调度热点六项**——
  (a) POSIX mt 驱动整个 reactor 在一把全局互斥内 epoll_wait（rt_io.c:229-236,1561-1566），
  注册新 await 最长顶 20ms、其余 worker 全排队（Windows 有分片，POSIX 无）。
  (b) zan_timer_cancel_delay 每帧释放路径 O(n) 全堆扫描+无条件 Floyd heapify
  （rt_timer.c:847-873，w==len 一个没删也全跑；`if (w != g_heap_len)` 一行可省）。
  (c) WhenAll(N) 完成通知在全局自旋锁内 O(N²) 全对扫描（:1440-1453,1483-1496）。
  (d) g_co_activity/g_co_outstanding 全局单点缓存行 RMW（spawn k=32 残留争用点）。
  (e) join_pair_done 对 DONE 朴素 memcpy 读（:1436，与完成线程 release xchg 构成 C11
  竞争，ARM64 可陈旧读；一行 __atomic_load_n ACQUIRE）。
  (f) live/slab 自旋锁 TTAS 无退避无让渡（非 x86 满核空转风险）。
- [ ] **B-ID75【代理·抽实锤】（P2批）stdlib 正确性十四项**——
  (a) ByteBuffer.Str/CmpRangeRaw `start+n` i32 溢出跳过钳制 → 越界读（:281-289,424-435）。
  (b) JsonValue.Get 重复键语义随成员数翻转：≤8 线性扫首键胜、>8 keyIndex 覆盖写末键胜
  （:218-239，校验读 A 取值读 B 走私向量）。(c) PathSetValue 非对象根丢写/NPE（:292-316）。
  (d) NaN/Infinity 经 ToJson/GenJson 输出裸字面量=非法 JSON（JsonValue:481-482、GenJson:291）
  ——自家 Parse 认所以自测测不出，外界全拒。(e) WriteAllLines 用 fputs：内嵌 NUL 截断+
  "w" 文本模式 CRLF 翻译（File.zan:375-388），与 WriteAllText 二进制语义不一致。
  (f) ftell 声明 int：>2GiB 文件 GetSize 负值/Copy 静默空文件（File.zan:29-32,290-292），
  GetSize64 存在未接线。(g) Windows Mutex.Create 传 "" 而非 NULL：全部实例共享同一命名
  互斥体互相阻塞（Threading.zan:107-111）。(h) DateTime 用非可重入 localtime，多线程
  转换互踩静态缓冲（DateTime.zan:29-35,137），换 localtime_r/localtime_s。
  (i) SHA-1 位长 len*8 i32 溢出：≥256MiB 输入摘要错（Encoding.zan:640-706）。
  (j) sem_wait/sem_timedwait 无 EINTR 重试（疑似，Threading.zan:202-300）。
  (k) Random 墙钟种子 MMIX LCG 无"非加密"警示（Random.zan:20-68）——类名/API 与 .NET
  同形极易被误用于 token；RandomNumberGenerator 已验证真 CSPRNG。修法：文档强警示+
  考虑 Secure 工厂。(l) Convert.FromBase64String `(len*3)/4` i32 回绕：>715MB 输入得
  0 字节缓冲喂原生解码=堆越界写（Convert.zan:75）。(m) MemoryExtensions
  `startIndex+count` 溢出 → 误报未找到（:45-48）。(n) FromHexString 非法字符静默按 0 解、
  奇数长度丢尾（Convert.zan:145-149）——宽松度是否合意待拍板。
- [ ] **B-ID76【代理】（P2批）zanc 三项**——
  (a) 插值格式说明符数字累加 `digits*10` 有符号溢出 UB（irgen_expr.c:6318，超 10 位数字），
  且 'X' 分支无 digits<=0 守卫 → 生成负宽度 printf 格式。(b) system() 链接分支
  4096 字节命令静默截断（main.c:8676-8860，无 argv 分支的 link_cap_exceeded 机制）
  → 链接报错与真因脱节。(c) 同行块注释后的 #endif 被吞（lexer.c:360-381，`#ifdef X /*n*/ #endif`
  条件永不闭合，声明被静默跳过）。
- [ ] **B-ID77【代理】（P1批）Zan.Yaml 四项**——
  (a) hex 整数溢出钳制阈值错 4 倍（YamlParser.zan:786 用 2^61-1，应为 (2^63-1-15)/16）
  → 大数静默变负（0x1FFFFFFFFFFFFFFF0 → -16）。(b) 序列项 `"- "` 空串 rest[0] 越界
  （:258-263，同文件其余取首字符处都有 Length 前置）→ 致命报告/进程退出。
  (c) 块嵌套 ParseBlock↔ParseMap 互递归无深度上限（:161-237；flow 有 512、同批
  Toml/Xml/Bson/Cbor/MsgPack 全有）→ 深嵌套输入栈溢出进程死。
  (d) 块标量/flow 跨行 O(n²) 字符串累加（:384-408,432）→ 线性输入平方级 CPU。
- [ ] **B-ID78【代理】（P1批）四二进制包写侧 EnsureRoom i32 回绕 + 读侧定长截断**——
  (a) BsonWriter/CborWriter/MsgPackWriter/ProtoWriter 四处同段复制的 EnsureRoom：
  总量>1GB 翻倍到 2^31 回绕负数恒小于 → 死循环；单次 append 使 len+extra≥2^31 首判据
  回绕提前 return → NativeMemory.Copy 写穿小缓冲。读侧 d4139b46 已加固、写侧裸奔。
  (b) CborReader.TakeChunks 扩容循环同型回绕（:186-187）。(c) CborReader 定长 argument
  (int) 截断：>4GB 长度静默按低位解析（:111-139）。(d) ProtoReader.ReadBytes/Skip
  varint (int) 截断：声明 2^32 截成 0 → 空载荷继续按字段对齐静默错解（:174,204）。
  修法：(a) 容量运算升 64 位+写侧总量上限（对齐读侧 512MB）+超长分片；(c)(d) Arg/varint
  > int.Max 即抛。
- [ ] **B-ID79【代理】（P2批）其余格式包正确性/资源上限**——
  (a) Toml：非法进制数字静默解析为 0 不置溢出哨兵（TomlParser.zan:605，`port=0xGG`→0）；
  WriteTable 递归无深度上限（Toml.zan:74,83，读侧可造万层树→ToToml 栈溢出）；
  键重复静默后写胜+点键覆子表、查重键 U+0001 可与键内容碰撞、数字尾部垃圾静默截断
  （三项宽松度待拍板声明）。(b) Csv：行数/字段数无上限，1GB 逗号密集文本内存放大约
  30 倍 OOM（Csv.zan:54-127，d4139b46 容器上限未跟随）；公式注入（=`+`-@ 开头单元格
  不中和，Excel 执行）标安全误用。(c) Regex：星号循环 Split/Mark 每圈递归一层，
  预算限总步数不限深度（RegexProgram.zan:1475-1491,588）→ 长匹配可栈溢出（疑似）；
  回溯预算按起始位置重置，超大输入总功 O(n×2M)（缓解完备性）。(d) Xml 写侧
  WriteNode 递归无上限（XmlDocument.zan:81）。
- [ ] **B-ID80【代理】（P2批）Mvc/Web/Scripting 纵深加固**——
  (a) CSRF double-submit 实现存在（Csrf.Guard 含常时比较）但 Boot:112 默认只注册
  OriginGuard，未注册前者（现代浏览器 SameSite=Lax 已挡，老浏览器残余面）。
  (b) AccessLog 回显客户端 x-request-id 未清洗进控制台日志 → 日志注入伪造请求行
  （Boot.zan:353-365）。(c) DataController 表元数据拼 HTML 属性未转义（:111-126，
  当前有 Gen.Safe 白名单前置，直改库即逃逸）。(d) 全站无 CSP/X-Frame-Options/
  X-Content-Type-Options（管理台 clickjacking、上传 nosniff 纵深缺）。
  (e) WS 升级无 Origin 校验（WebWs.zan:36-42，CSWSH 残余面同 (a)）。
  (f) View.zan HtmlEscape 漏 '（当前无单引号属性模板，防纵深）。(g) Zan.Scripting
  Lua 沙箱：Sandbox() 未移 load 且 LoadBufferX mode=null 允许字节码 chunk →
  预编译字节码绕过 _ENV 防线（Lua.zan:159-170,534-543）；修法：load=nil 或限 mode="t"。
  (h) DesignerHtml 三处未转义 sink：Element tag 原样拼（DesignerHtml.zan:124,129）、
  属性值 rv 原样（:233）、属性名 key/KebabKey 未过滤（:490-517）——设计器文档
  （模板/市场来源）不可信即注入；修法：tag 走 IsNameChar 校验、键值全过 EscapeAttr、
  KebabKey 丢非字母数字。
- [ ] **B-ID81【代理·部分实测】（P2批）跨平台缺口九项**——
  (a) AppPath.Pid() 无 Apple 分支：macos/ios 上 Environment.ProcessId 恒 0
  （AppPath.zan:58-79，ProcessHost.Pid 有 getpid 范本）。(b) Zan.Gui win-arm64 驱动
  只有 libzan_gui.dll.a 导入库、无 static/dll/bundle payload（B-ID51 同型）。
  (c) Zan.Gui 只有 ohos-x64 驱动、ohos-arm64（真机）缺失。(d) toolchain/ios/arm64 缺
  zan_embed_api.o+zan_inflate.o（其余全目标都有，引用嵌入资源 API 的 iOS 程序链接响亮失败）。
  (e) OperatingSystem.Platform 对 ios/ohos/android 报 "linux"、IsOSPlatform 不识别三者
  （OperatingSystem.zan:74-97，编译器宏已定义）。(f) rt_sync_wasm shared-table 哈希漏
  0→1 重映射（:171-181 vs rt_sync.c:233-242，注释声称 identical）。
  (g) ProcessHost.DaemonizeTo 仅 LINUX，macOS 静默 false。(h) RandomNumberGenerator 无
  wasm32 random_get 分支，WASI 无 /dev/urandom 时静默 null（疑似）。
  (i) Zan.Desktop System.Management 族 macOS 全落空值（半文档化，包内口径不一致）。
- [ ] **B-ID82【实测】（P2·工具链）check_toolchain_stale --verify 实测 6 项真陈旧待重交**——
  toolchain/win-x64 与 win-arm64 的 zan_embed_api.o（落后 rt_timer.h）；Zan.Gui 驱动
  win-x64/zan_gui.dll、linux-arm64/static/libzan_gui.a、macos-arm64/macos-x64 两
  libzan_gui.dylib（落后 gui_runtime_x11.c/text/font/shims）。linux/android/macos 的
  rt 族对象日期过期但 --verify 字节一致=实为最新（B-ID59 机制在位）。需对应平台重建提交。
- [ ] **B-ID83【代理】（P2·性能）Zan.Data/Linq 热点**——
  (a) 四驱动每行每列无条件字符串物化+数值重复解析：SqliteConnection.zan:344-373 每格
  column_text 强制 int→text 再 text→int、PostgresConnection/MySqlConnection 同型、
  DbResult.GetInt/GetLong/GetDouble 每次重新 strtoll 无缓存；20 列×1 万行=20 万次串
  分配/查询。修法：惰性列物化/类型化直取（sqlite3_column_int64）。(b) Linq
  Distinct(eq) 比较器版 O(N²)（Enumerable.zan:376-382，哈希版已有）。(c)
  OrderBy(k1,k2) 两次全排序（:447-450，可合成单次双键比较省一半）。(d) MergeSort
  每次排序多一份全量输入拷贝。
- [ ] **B-ID84（P3批·卫生汇总，修时可批量）**——
  runtime：zan_inflate zan_embed_rawlen 缺 total≥4 下限、32 位目标 raw_len+1 回绕
  （:25-49）；rt_file.c:879 文件锁 gen 回绕重置为 1（应退役）；common/json.c:439 超长
  数字字面量静默截断变值+接受前导 +；libwebp 1.4.0→例行升级；Windows g_fls DWORD
  跨线程读/pthread key 失败不回收。zanc：verbatim/插值字符串 EOF 未终止无诊断
  （lexer.c:1335,1288）；浮点字面量超长误报 integer 措辞（:945）；arena 尺寸算术无溢出
  守卫（arena.c:41,67）；目录枚举序进发射序=跨机字节不可复现（package.c 十处
  FindFirstFileA/readdir 无排序，同机确定性无碍）；OOM 分支 strbuf 泄漏（lexer.c:1154）。
  stdlib：GenDbEmit 属性串插值未过 Esc（:272，开发期暴露）；GenForm 数值属性 AsInt 截断
  小数（:1449）；GenRoute Opt 默认值 Atoi 10 位回绕（:187）；EscapeAttr 漏 '（双引号
  包裹下无触达，防纵深）；ByteBuffer.ToBytes 多一尾随 NUL（:76-82）；Interop EnvVar
  1023 截断静默/Wide.Of(null) NPE/Com.Guid 畸形文本裸异常；JsonValue 与 JsonTape 对
  \u0000/孤立代理对行为分歧（:1082）；Encoding.IntToString 死代码/ParseDouble 位数无钳/
  GetByteCount 按 strlen。SDK：Sm2.Sign k 由调用方传入无 r/s==0 重试（Ecdsa 已 RFC 6979
  对照）；Curve25519 未拒全零共享密钥；Rsa OAEP/v1.5 PS 扫描变长时间（疑似，Bleichenbacher
  前提）+ModExp 无盲化；access_token GET query 传输与无刷新互斥（协议固有+多实例部署提示）。
  格式包：ProtoWriter.WriteBytesField 切片校验加法可回绕（:154，读侧已减法）；Proto 读侧
  字段号>2^29-1 宽松收；CBOR tag1 内层非数值伪造纪元零值；YAML 浮点指数循环最多 2^31
  次乘（秒级有界）。
