# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：`[ ]` 未完成 · `[~]` 进行中 · `[-]` 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 `[x]` 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出，在下方索引留一行（编号+提交号）；10-03 起不再保留长叙事——此前的叙事全文在 git 历史与本清单 blob 历史及对应提交正文。

## 平账索引

- 2026-09-29 通讯协议安全审计批 A–F 全闭：C-1..3、B-HW1..4、A-HW4/5、B-ID1..16、B-NET1..4（含 4a..j；fail-closed 边界：macOS/iOS 无 SecTrust 桥接恒拒、CRL/OCSP/nameConstraints 未知拒、Ed25519/P-384/完整 UTS-46 严拒）；截至 b380d2b7。
- 2026-09-30 静态审计 18 条闭账移出：B-ID17/18/20/22/23/25..29/32..36、A-MEM1..3（6bf6e85f、27ffbf7a、7e1c6e20、050a0052）；B-ID21 闭账（733ea3a5）。
- 2026-10-01 B-ID37 server 四模板迁移批闭账：collab c2056371、legend 947aebb2+2078d51e、licensing f4deaebc、iot b70b7b93、GenRoute 路由键去重 100525a0（fresh-dir --publish 实机 boot）；残项剥离 B-ID50/51/52；编号占用注记（B-ID48 曾双占、B-ID49 被 61ac40e3 占用）自本批起新条目改自 B-ID50 编起。
- 2026-10-01 templates/examples 冻结床普查（38 项）：5 项随批修；B-ID38 根治（nsresolve 歧义判定按泛型元数过滤，conformance_ns_ambiguity_arity_filter）、B-ID39 证伪撤案（ZANC_DUMP_GEN 实证「字段被同名类遮蔽」机制不存在，普查红系漏传设计稿，wuwei 修复作废回退）。
- 2026-10-01 B-ID45 七池建连失败诊断闭账（OpenFailing/lastError/lastOpenError，死端口探针实证）；B-ID31 catch 全集逐处裁决闭账（122 真实站点全合法，「61 catch 仅 6 重抛」系 grep 口径）；B-ID24 十进制字面量 [2^31,2^32] 直写 int 槽改 CS0031 式窄化报错（diag_decimal_literal_narrowing + conformance_int_literal_range，存量 ~40 处直写点转 hex、掩码/cast 通道保留）。
- 2026-10-02 B-ID52 发布装运拷贝诊断加固（winerr 32 独占锁实证）；B-ID51 pq win-x64 libcrypto/libssl 收敛内置（192 文件装运实机握手）；B-ID46/47/48 闭账归档（AsI64/AsF64 bitcast、wasm _setjmp 桩+交叉对象、定时器期限钟微秒化；OCR 斜排撤案存档）；B-ID53 向量 MoveMask/ExtractMostSignificantBits 非 x86 可移植 IR（Shuffle/Average 同法 9e55a9f1）；B-ID50 collab 实时丢帧根治（水位竞态 InitWatermark/孤儿 worker 强杀/NO_PROXY，e2e_realtime 50/50）。
- 2026-10-02 B-ID55 Stopwatch 直读 RT 时钟去堆分配（zan_stopwatch_ticks，24 项孪生）；B-ID58 字符串 + 拼接丢内嵌 NUL（emit_cstr_len_of 长度感知，tests/conformance/string_concat_nul）；官方语料互操作收官（bson-corpus + msgpack-test-suite 入库，钉修 bool 严格/binary subtype/写侧 NUL 串、DateTime 公元 0 年 FloorDiv、msgpack 64 位 ts 掩码，b4116ce3、1fcaeee6、8d04737a）；B-ID57 Json -0.0 负零位型。
- 2026-10-02 B-ID44/B-ID54 高并发主线闭账（M:1+mt 双引擎默认即开、spawn 缓存线隔离 steal_ok -85%、让渡路径开销归因反转、FINEXC 最小帧 5264B→168B）；长期路线拍板留 CPS，Loom 不采纳（续体切换 126-172ns/帧 ~168B/spawn 400-465ns，三硬指标实测零收益）。
- 2026-10-02 B-ID56 WhenAll/WhenAny 完全事件化（joinmap+完成钩子，e8614c8c+01ab2c49+fd5e40e9）；B-ID19 归因勘误闭账（真因=try 内 return 溢出槽跨 finally 挂起重读未初始化栈格，修复 46a7e83a 早已落库，903294fd）。
- 2026-10-02 --fast-alloc native 链路修复（lld PE --wrap 改写导入槽致加载即死→rt_mem 对象改走捆绑 GNU ld，2c796e99）+ 翻 native 默认（--no-fast-alloc 显式退出，0eb5047a）+ rt_mem remote 分片 8 条带（00571743）。
- 2026-10-02 B-ID59 闭账（ios/win 交叉 rt 对象全部本机重编，3fe0eb8d）+ 交叉对象陈旧全面定性（04e68896）。
- 2026-10-03 深度审计批 B-ID60..84 闭账（8 领域约 62 万行重审，6 路修复代理+主会话并行 29 提交，25879056 收册）：编译器 ba32a189（枚举折叠/插值 NUL/链接截断）；stdlib eb64f58a、f885c5a5、c8cbe3b8、a738a0d0；runtime bdd0a6a1、9a45a015、1745aa39、14f4f1f8、624c706d、539d72f1；包安全 80a83dd2、daa61dc4、9ff778eb、529e53fd、278609f3；解析器 7779081c、49b2b5c9、2824550d、c45d8efe；Mvc/Web e20c1e68、b037adbf、d59a6e2a；Linq/加密/工具链 b4ee0a62、38f00e2f、9c36cdfe、e4da88ba、d16977d5、4e31a537、2b80ce8e。审计期新发现 B-ID85/86/87/88/89 与各批残项缩条重挂。
- 2026-10-03 审计期新发现与残项闭账：B-ID85 语句 lambda 重载（0d5486e7）、B-ID87 stdlib 内联 ReadAllText reach（d3e4418a）、B-ID86 GUI 驱动平台 builder（c2de7a95）、B-ID82 对象矩阵（04e68896/c2de7a95，win-arm64 载荷 CI 盲区本机补齐）、B-ID74 残项（c936eb03）、B-ID78 残项（bd1c3508）、B-ID80 残项（d6548ae3+0d4cb1fb）、B-ID81 全子项（a/c/e/h a738a0d0、d 38f00e2f、f 624c706d、g 9c36cdfe、i fb11642e、残项(b) win-arm64 载荷 5524ba94+台账守卫 a48fa5b8；残项(c) ohos-arm64 需 OHOS SDK，归 B-ID86 残项另行立项）、B-ID83 残项（10e1a610）。
- 2026-10-03 B-ID88 linux 多工 join 假停滞闭账（io 分片静态数组零初始化 fd=0 守卫短路，io_shards_prime -1 根治，1f69ce5a）；B-ID89 win-arm64 PE 消费链接「lld 空白」证伪闭账（幻影符号 zan_gui_init——DLL 导出表 103 项从未含它，真导出符号四路链接+zanc E2E 全通，7f31a251）。
- 2026-10-03 B-ID79 残项拍板不修闭账：CSV 写出保持数据原样（用户裁决「不能改 CSV 的习惯」——不前置撇号、不改字节，中和与否属调用方决策）；类文档安全提示补写侧明示（本提交）。同批：drivers.yml 触发面改 src/runtime/** 全树通配，堵死 unity 内联面（libwebp/stb_*/gui_* 等）变更不触发驱动重建的 B-ID86 同款缺口，兼作 CI 活性金丝雀（6c25d871）。
- 2026-10-03 B-ID86 残项（ohos-arm64 GUI 驱动）闭账——挂账前提「需 OHOS SDK、GitHub runner 无法产出」对本机已失效：DevEco Studio 自带完整 OpenHarmony native SDK（llvm/clang + sysroot 153 库含 native_window/libEGL/libGLESv3）。ohos-arm64 驱动首建（libzan_gui.so + static/libzan_gui.a，AArch64 ELF、597 导出、NDEBUG 零路径泄漏）；ohos-x64 陈旧产物（9/12 vs 9/30 源）同配方换血；build_gui_ohos.sh 重写为自包含配方（freetype 2.14.3 按 INSTALL.ANY 逐文件自源构建——unity 拼接撞宏；DevEco clang.exe 不认 MSYS 路径）。E2E：zanc --target ohos-arm64 --emit-lib 探针出 AArch64 libmain.so，FT_* 未定义=0（freetype 全静态入档），未定义面=libc/dl/egl*/gl* 设备端加载期解析。连带清偿 6bab35f6 预告的 android 债：双 ABI 静态归档换血（build_gui_android_static.sh，NDK 27.2 本机）。

## 未完成

- [ ] **B-ID90（P2·工具链）残余仅 macos 双 dylib（Mac 独占，无本地路径）**——本批清偿：
  win-x64 dll 与 linux-x64 .a 换血（3922859b，bdd0a6a1 音频入面）；linux-arm64 .a
  首建（fe8da32e，noble arm64 dev 包用户态解包免 sudo 取 X11 四静态档 +
  aarch64-linux-gnu-gcc，官方脚本同旗子；探针定验未定义面恰为 FT/Fc 二十符号、
  X11 零缺失，符号面与 x64 对齐差集全为 SSE2 变体）；交叉 rt 十四件重编字节裁定
  现役（零提交）。macos 双 dylib 需 ObjC+macOS SDK（gui_runtime_mac.m），zig cc
  不编 ObjC，本机物理不可产，等 Mac runner；drivers.yml 复活即随回写清偿，
  与 B-ID84 同门。
- [ ] **B-ID91（P1·工具链）linux GUI 交叉链接全量失败（x64/arm64 同）**——探针
  _scratch/link-probe/{probe,probe-notext}.zan：App.CreateDarkStage+Show 一行
  DrawText 与不带 DrawText 的对照**都**链接失败，未定义面恒为 FT_* 七符号 +
  Fc* 十三符号（fontconfig）——文字面从 App/canvas 核心必然可达，gc-sections
  剪不掉，任何 linux GUI 程序当前不可交叉链接。根因：驱动档不含 freetype 实现
  （build_linux_gui_static.sh 只并 X11 系），zanc linux 链接段亦无 libfreetype.a
  处理（唯一处理在 wasm32 段 main.c:8414，仓库入册的也只有
  toolchain/wasm32/libfreetype.a）。修复方向：zig cc musl 目标产 libfreetype.a
  （wasm 配方 build_cross_rt.cmd:98 前车，源树 D:/project/firefox/modules/
  freetype2）+ fontconfig（拉 expat，源树获取是缺口）→ 入 toolchain/linux-* 并
  在 linux 链接段接线，或按 ohos 前车并入驱动档；修后本探针转绿即闭。
- [ ] **B-ID84 残项（P3批·卫生汇总）**——仅余 runtime：libwebp 1.4.0→例行升级（整包换血，
  单独批次）。**开工前置（2026-10-03 观察）：驱动回写 CI 未证活**——bot 提交自 46477da5
  （10-01）后全腿归零（c2de7a95/c936/1f69 三次应触发零落地，win 腿疑 continue-on-error
  吞败，本机不可观测日志）；升级会让 gui_runtime.c unity 内联的 libwebp 变更同步污染
  五平台驱动（macos dylib 只能 Mac 造，B-ID82 同款陷阱）——先等一次 bot 提交落地证 CI 活，
  再动此批。其余子项均已闭账/裁决：Encoding.GetByteCount 文档已在位、ByteBuffer.ToBytes
  尾随 NUL 金样契约保留；SDK ModExp 无盲化（P3 已注）、access_token GET query（协议固有）；
  runtime/common 539d72f1、加密 e4da88ba、格式包 c45d8efe、stdlib d16977d5、zanc 卫生
  4e31a537、Json \u0000 统一、GenJson NaN 守卫、rt_mem g_fls/pthread-key、Zan.Xml &#39;。
