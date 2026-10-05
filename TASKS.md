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

- [x] **B-ID90（P2·工具链）残余仅 macos 双 dylib（Mac 独占，无本地路径）**——已闭
  （10-04）。终章：bot 提交 4875bb84（drivers.yml run 195）刷新 macos-arm64+x64
  libzan_gui.dylib 双件（bdd0a6a1 音频等入面）；win dll 束随后被 run 196/198 bot
  连续回写（a2fefbb4/94e41c1c/6f9321e6/f2494002/88cd618d/577505e8）。本批更早清偿：
  win-x64 dll 与 linux-x64 .a 换血（3922859b）；linux-arm64 .a 首建（fe8da32e，
  noble arm64 dev 包用户态解包免 sudo）；交叉 rt 十四件字节裁定现役（零提交）。
  CI 复活三连修（同日 deda5d72/f574839d/7c5a5843/1bda212c）：linux 腿补
  libfreetype-dev/libfontconfig1-dev（桌面字体分支 unity 编译需 FT/Fc 头）、
  msys2 装 unzip（MSYS2 镜像漂移剥内置，WebView2 解包炸）、win Lua 段逐文件
  -o luaobj/（VS18 clang 多输入 -c 产物 glob 不可见，lld-link 吃到字面 '*.obj'）、
  SDEST 未定义引用清理（0bf3950a 9-10 改名残留，set -u 必炸，被 unzip 缺失遮住）。
  黑腿根因：私有仓 Actions 额度耗尽（2000 分钟/月，macos 10× 计费一次烧完）——
  转公开根治，run 194 起六腿复活。
- [x] **B-ID91（P1·工具链）linux GUI 带文字交叉链接失败（x64/arm64 同）**——已闭
  （10-04 60d7580b）。探针 _scratch/link-probe/{probe,probe-notext}.zan 定谳矩阵：
  无文字+--publish 可链（10-02 516360bc「musl 可链化」验证的正是此形态）；带文字
  （DrawText 一行）publish 与否都挂，未定义恒为 FT_* 七符号 + Fc* 十三符号
  （fontconfig）。时间线：FT/Fc 引用自 7-12 bcf01de4（字体引擎）入面，带文字形态
  自那时即断——此后无人交叉发布过带字 linux GUI，静默至今。根因：fontconfig 桌面
  分支（gui_runtime_font.c #else）无静态供给，zanc linux 链接段亦无 libfreetype.a
  处理（唯一处理在 wasm32 段 main.c:8414）。修复（按平台分支保留拍板，不并驱动档）：
  zig cc musl 逐文件产 libfreetype.a（INSTALL.ANY 39 文件表）+ libexpat.a（手写
  expat_config.h）+ libfontconfig.a（手写 config.h+fcobjshash.h gperf 替代、
  fcalias 空桩）× x64/arm64 六档入 toolchain/linux-{musl,arm64}；main.c linux 段
  接线（存在则链，镜像 wasm32）。顺修两处连带发现：① CMake staging 漏
  zanrt_io_mt.o——linux x64/arm64 external_async_executor=true 链接需它，fresh
  checkout 必挂 zan_co_ready/zan_co_sched_init 未定义（主树 build/ 靠陈旧手工副本
  侥幸绿，干净 worktree E2E 当场暴露）→ 入 foreach EXISTS 守卫；② checker 六档
  入册（ZIG_BUNDLED·external-tree 派生）。验证：干净 worktree（并发会话 21 文件
  在途）zanc 四探针矩阵全绿——text+pub x64 3.19MB / text 非pub x64 4.53MB /
  notext+pub x64 1.98MB（回归无劣化）/ text+pub arm64 aarch64 ELF 3.07MB；
  WSL 实跑 text 探针 DISPLAY= 输出 probe-linked EXIT=0。
- [x] **B-ID84 残项（P3批·卫生汇总）**——libwebp 1.4.0→1.6.0 整包换血闭账（2026-10-04，
  a1fd1965 源码+金样：20/20 样张 RGBA CRC 新旧一致；三处本地改造按 README 重放：文件相对
  include 291 处、quant_levels_dec clip_8b_ql 改名（1.6.0 与 dsp/dec.c unity 冲突复现）、
  lossless.h enc 头剔除；x64+aarch64 unity 编译净）。驱动全档刷新：macos dylib+win dll 束
  CI bot 回写（fc808204/13bf2761/d58a5c00）；android×2+ohos×2 本机重刷（NDK/DevEco，
  nm 面=纯 webp 上游增删：删 VP8LClear/VP8LPredictor{0,1}_C，增 IsValidColorspace/
  WebPValidateDecoderConfig；每目标 clang 编译+.so 链接+.a 归档步全过；win 侧 webp 解码
  实跑探针通过）。linux-x64/arm64 .a 本机已烤出 1.6.0 档（3380 面）但因并发会话四档
  在途（M，1.4.0 时代内容）按规则 11 不越权覆盖，待其落地后按本提交同法重刷。顺修
  build_gui_ohos.sh REPO 机器绝对路径硬编码→脚本位推导（曾把主树在途 gui_runtime.c
  烤进产物，已还原重烤）。
  其余子项均已闭账/裁决：Encoding.GetByteCount 文档已在位、ByteBuffer.ToBytes
  尾随 NUL 金样契约保留；SDK ModExp 无盲化（P3 已注）、access_token GET query（协议固有）；
  runtime/common 539d72f1、加密 e4da88ba、格式包 c45d8efe、stdlib d16977d5、zanc 卫生
  4e31a537、Json \u0000 统一、GenJson NaN 守卫、rt_mem g_fls/pthread-key、Zan.Xml &#39;。
- [x] **B-ID92（P1·arm64 GUI 代码生成崩溃）闭账（2026-10-04）**——根因与 B-ID84 的
  webp 无关（当初归因 irgen 链系误判）：`Crc32C_Compute`（Gui 宿主路径经 stdlib 拉入）
  中 `Sse42.Crc32` 被无条件发射成 `llvm.x86.sse42.crc32.*` intrinsic，任何非 x86 目标
  （android/ohos-arm64）SelectionDAG 无法 promote 直接 fatal。定位法：--emit-ir（尾
  截断是 emit-ir 另一独立小缺陷，与崩点无关）→ llc 修 extern_weak/attributes 后复现
  → 二分到 `@Crc32C_Compute`。修法：emit_sse42_call 按目标分流——x86 仍走硬件
  intrinsic，其余架构落 `__zan_crc32c_step{1,2,4,8}` 表驱动软件链（复用
  crc32c_table_global，位精确；ARM 上该分支运行时本被 Cpu.HasSse42→0 守死，纯为
  模块合法化）。验证：裸 GuiHost 探针+3D demo android-arm64/ohos-arm64 链接通过；
  CRC32C("123456789")=0xE3069283 标准向量 x86 硬件与 ARM 软件路径（qemu-aarch64 实跑）
  五值全等；x86 新旧 zanc 逐值一致；crc32_hw conformance 金样过。两条残项已闭：
  wasm32 段归 B-ID93、emit_aes_call 段归 B-ID94（均见下）。--emit-ir 240MB+ 模块
  尾截断 exit=0 仍开放（独立小缺陷，另批）。
- [x] **B-ID93（P2·wasm32 cpuid 内联 asm 非法）闭账（2026-10-04）**——B-ID92 残项：
  `__zan_cpu_feature` 非 ARM 分支无条件发射 x86 cpuid 内联 asm（`{ax}` 约束），
  wasm32/riscv 后端 SelectionDAG 无法分配输出寄存器，编译期 fatal。修法：
  irgen_expr.c cpu_feature_fn 三分支化——x86/amd64 三连走原 cpuid asm，ARM 保持
  既有读寄存器路径，**其余架构直接 `ret i32 0`**（wasm/riscv 无 cpuid 概念，全部
  特性位恒不支持，与 ARM 守门语义一致）。验证：crc32_hw conformance 探针
  wasm32 编译通过（原 `couldn't allocate output register for constraint '{ax}'`
  fatal 消失）；IR 中 `@__zan_cpu_feature` 退化为 `ret i32 0`，模块零内联 asm。
- [x] **B-ID94（P2·Aes.* aesni intrinsic 目标失配 + InverseMixColumns 静默漏接）闭账
  （2026-10-04）**——两个叠加缺陷：① emit_aes_call 无目标分流，ARM 上六方法全部
  发射 `llvm.x86.aesni.*` 直接 fatal（B-ID92 同类）；② x86 路径 InverseMixColumns
  分支长度常量写错（`len==18`，实长 17），从未匹配过——调用静默漏到 DllImport
  外部路径，链接期 `undefined reference to 'InverseMixColumns'`（无人调用过故从未
  暴露）。修法：aarch64 分流进 emit_aes_arm，按 ARM AES 语义代数重构（**ARM
  AESE/AESD 是先 XOR 轮密钥再 SubBytes/ShiftRows，与教科书相反**，单条 AESE 永远
  出不了 `SR(SB(a))^k`）：AESENC=aesmc(aese(a,0)⊕aesimc(k))、AESENCLAST=aese(a,0)⊕k、
  AESDEC=aesimc(aesd(a,0)⊕aesmc(k))、AESDECLAST=aesd(a,0)⊕k、IMC=aesimc(a)（五式
  Python 对 x86 硬件金样+200 组随机数验证后才落码）；KeygenAssist ARM 无对应，
  返回 false 走干净未解析诊断；crosscomp.c 与 irgen_emit.c aarch64 特性表补
  `+aes`（llvm.aarch64.crypto.* 选择的前提）；len 18→17 双处修正（ARM 新码 +
  x86 旧码）。验证：b94 六方法金样 x86 硬件与 qemu-aarch64 逐字节全等（imc2 自
  检=Involution 成立）；win-arm64/linux-arm64 链接通过；t3/t4 单方法探针 x86 过。
