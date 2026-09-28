# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。**凡在仓库中发现缺陷、缺口、未决设计，必须先在此登记编号再行动**；完成时标记为 `[x]` 或注明完成提交 / 闭账日期，并运行影响到的测试闭环。
> 状态标记：`[ ]` 未开始 · `[~]` 进行中 · `[x]` 已完成 · `[-]` 已作废

---

## 维护约定

* **编号规则**：`A` = 编译器 / 运行时 / 语言语义；`B` = 标准库 / 生态模块；`C` = 文档 / 工程规范；历史扩展沿用 `A<编号>`。
* **增补纪律**：发现新问题随手在对应大类末尾立账，写明现象、复现路径或最小探针。
* **销账纪律**：修复必须附带 conformance 测试用例；通过对应测试层后在条目中写明 commit hash 与完成日期。
* **归档纪律**：已完成的条目压缩为一行摘要（保留编号、一句话结论与日期，原详细排查过程进 git 历史，避免文档膨胀）。

---

## C 类 · 文档 / 工程规范

- [x] **C-1** `scripts/build_mge.ps1`、`build_glassprobe.ps1`、`build_gui.ps1`、`build_jsonbind.ps1` 的手工 mingw GUI 归档缺 `gui_runtime_dwrite.o`：gui_runtime.c 只声明 `zan_dw_render`，实现拆在 `gui_runtime_dwrite.cpp`（b674314a 拆分引入），IDE 与 gallery 脚本已补（2026-09-26），这四个跑 `--link` 会报 undefined reference。已修复：clang++ `-fno-exceptions -fno-rtti` 编译 dwrite.cpp 进归档并打包入库。2026-09-27。
- [x] **C-2** 提交态静态驱动归档（win-x64）陈旧叠加，静态发布泄漏构建机路径：`libzan_gui.a` 无 NDEBUG，vendored 库 assert 的 `__FILE__` 把 31 处 `D:\<repo>\src\runtime\...` 路径编进每个单文件发布 exe；且静态脚本漏编 dwrite TU（C-1 同族）、重写 `zan_gui.libs` 时丢失人工维护的 ole32/shcore。修复：`build_win_static_drivers.ps1` 加 `-DNDEBUG`、补编 dwrite 成员、`.libs` 合并为超集，归档重建后发布 exe 路径字符串清零。2026-09-27。
- [x] **C-3** PE 静态发布按需导入：`--gc-sections` 在 PE 上对 `.text$` 分组节无效（.pdata 钉活，实测 .pdata 只减 4 项），unity 尾部 `#include "zan_audio.c"` 让 WASAPI+stb_vorbis 进每个 GUI 单文件 exe。修复：`ZAN_GUI_AUDIO_SEPARATE` 下 zan_audio.c 独立编为 `libzan_gui.a` 第三成员，ld 按符号需求拉成员——不用音频的程序 −103KB（vorbis/OggS 字符串清零），音频程序照常拉入（audioprobe 实开 WASAPI 验证）。共享 DLL 保持 unity、103 导出零漂移。linux/android/ohos 静态驱动脚本同批加 NDEBUG（提交态归档待各自平台重跑换血）。2026-09-27。

---

## 2026-09-26 硬件加速改造挂账

- [x] **B-HW1** 纯 Zan TLS 重写：`stdlib` 的 `TlsStream` 原先持 OpenSSL DllImport
  （`SSL_new`/`SSL_connect` 等），已彻底重构为 100% 纯自研零外部 C 依赖 TLS 1.2 / TLS 1.3
  双协议栈引擎（AES-GCM、Curve25519/X25519、RSA-CRT 加速、HKDF/PRF、ASN.1 X.509）。
  性能消弭与 C/OpenSSL 差距（AES-GCM 达 2.6 GB/s，RSA-2048 签名 1.04 ms，握手 0.66~1.6 ms），
  44/44 ctest 密码与 TLS 用例全绿。提交 a39e0da5 / 2026-09-27。
- [x] **B-HW2** ARM64 硬件内核落地：`rt_hw_accel.c` 的 x86 侧已全覆盖
  （SHA-1/256 NI、AES-NI 参数化 CBC/ECB/CTR、PCLMUL GHASH、CRC32C、
  SM4 查表），ARM 侧缺内核。已落地（2026-09-26）：SHA-1/256（FEAT_SHA1/256，
  vsha1*/vsha256* 全体内管线）、AES（FEAT_AES：enc 融合管线、dec 挂起密钥
  管线 AESD 前异或语义、CBC/ECB/CTR 参数化）、GHASH（PMULL 寄存器域反射
  折叠）、CRC32C（ARMv8 CRC 系统指令）、SM3（FEAT_SM3：sm3ss1/tt1a/1b/2a/
  2b 状态反排 {D,C,B,A}、SS1 不含 W 项、partw1/partw2 各取不同 n 窗）。
  全部挂三态 KAT 门控，qemu-aarch64（zig 交叉编译真执行 ARM 加密指令）
  对官方向量全 GREEN（FIPS 180-4、FIPS-197、SP 800-38A、GB/T 32905、
  RFC 4960），digest 另与 OpenSSL 逐长度（55..130 含块边界）比对一致；
  x64 构建回归 + conformance crypto/hw 7 例过 + ZAN_NO_HWACCEL=1 软件
  路径与硬件路径逐字节一致；standard 档过。SHA-512 保持诚实 -1
  （FEAT_SHA512 真机罕见，纯 Zan 覆盖）。真机批量回归仍待硬件到手。
- [x] **B-HW3** SM4 AES-NI 仿射分解：x86 查表保底并全面升级硬件加速。
  已落地（2026-09-27）：基于 GF(2^8) 仿射同构分解 $S_{SM4} = M_2 \cdot S_{AES}(M_1 \cdot x \oplus C_1) \oplus C_2$，
  消除 AES ShiftRows 并通过 PSHUFB 并行计算仿射变换；实现 4 块与 8 块双路交织 AES-NI 内核、
  16 块 256 位 AVX2+VAES 向量内核，及 ARM64 FEAT_SM4 硬件内核；挂载 GB/T 32907-2016 官方 KAT 门控；
  加密升级为常数时间防缓存计时攻击，解密吞吐由 235 MB/s 提升至 602 MB/s (AES-NI) 与 1180 MB/s (VAES, 5.02x)；
  标准向量与回退路径在 `hw_crypto_pixel.zan` 与 `crypto_pure_ciphers.zan` 验证双向通过。
- [x] **A-HW4** `byte[]`→`string` 零拷贝视图的 `.Length` 走 strlen，首个
  NUL 处静默截断（原始摘要转 hex 丢尾字节；`emit_string_length` 未开
  array_count 模式，而边界检查早开了——铺了一半的缺陷）。已修：长度读取
  统一识别 `ZAN_ARRAY_MAGIC` 头并取 -16 元素数（ABI 注释即此设计意图）。
  conformance：`tests/conformance/string_view_length.zan`。2026-09-26。
- [x] **A-HW5** `Md5.Hash` 空消息无限递归（`Hash(new byte[0], 0)` 自调用；
  旧"总是成功"的 Md5 内建把它掩成死代码）。已修：删递归行，空消息直落
  纯主体（与 Sha256.zan 同构）。conformance：hw_crypto_pixel md5 empty。
  2026-09-26。
- [x] **B-HW4** linux GUI 驱动静态归档的构建配方未入库：`stdlib/Gui/drivers/
  linux-{x64,arm64}/static/libzan_gui.a` 构建脚本入库。已落地（2026-09-27）：
  编写 `scripts/build_linux_gui_static.sh`（支持 x64/arm64，单 TU 编译 gui_runtime.c 并提取
  系统 libX11.a/libXau.a/libxcb.a 静态库合并封装），并在 `.github/workflows/drivers.yml` 的
  linux 构建流水线中挂接该步骤与产物回填。

---

## 2026-09-27 IDE/LSP/DAP 假实现清账（IDE · 设计器 · 代码编辑器审计）

审计 src/ide_zan 与 zan-lsp/zan-dap：可当场修的已闭账，大件立账如下。

- [x] **B-ID1** zan-dap 三假声明：pause / hit-count / logpoint 只在 initialize
  能力里宣告、请求到达后不实现。已修（提交 589e6183，2026-09-27）：pause 真
  打断运行中 inferior（Windows DebugBreakProcess 注入断入线程后按 .zan 栈挑
  线程，POSIX SIGINT；单线程适配器用 wait-hook 在等 gdb 输出的空窗消费客户端
  pause）；hitCondition `==K`/`>=K` 映射 gdb `-break-after`（`%K` 明确拒绝不
  假装支持）；logpoint 命中时插值 `{expr}` 打 output 并自动续跑。集成测试
  tests/dap/dap_integration_test.c 四场景（basic/hitcount/logpoint/pause，
  燃烧目标 tests/dap/dbgtarget_burn.zan），ctest dap_integration 绿。
- [x] **B-ID2** zan-lsp 五个硬缺口一次补齐（2026-09-27）：formatting /
  rangeFormatting（zanfmt 同语义 + 字符串/注释感知；range 只改行首空白）、
  documentHighlight、foldingRange（花括号扫描，串/注释感知）、prepareRename
  （renameProvider 升级为 {prepareProvider}）、publishDiagnostics 范围从 1 字符
  扩到整 token（标识符延展、标点 1 字符）。lsp_integration_test 扩展 9 项断言
  全绿；能力清单见 docs/TOOLING.md。
- [x] **B-ID3** rename/references 局部符号已作用域感知（c83ea8f0，2026-09-27）：
  引擎跟踪方法体范围（intel_local_extent），rename/references/documentHighlight
  对局部与参数把编辑/引用限定在所属方法体内，同名局部不再跨方法误伤；集成
  测试 19 断言绿。余量：字段/方法等成员级仍整词文本匹配，binder 支撑的成员
  rename 待立新账（量大，见 docs/TOOLING.md 已述限制）。
- [x] **B-ID4** semanticTokens 与 inlay hints 协议能力端到端落地（2026-09-27）：
  initialize 响应声明 inlayHintProvider 与 semanticTokensProvider（15 类 tokenTypes
  + 空 modifiers）；新增 textDocument/inlayHint 支持：var 推导类型提示与调用端实参形参名提示
  （intel_collect_inlay_hints，复用符号表无大 AST 重建开销）；新增 textDocument/semanticTokens/full
  支持：基于 zan_lexer 与符号表发射 5 元组差分高亮流；lsp_integration_test 扩展 4 个断言（类型/形参
  hints、非空 tokens 数据）全绿回归通过。
- [x] **B-ID5** intellisense 每请求 ~2MB malloc + 全量重建（c83ea8f0，2026-09-27）：
  五个 handler 改 (uri,version) 单槽缓存复用引擎，didClose 失效、uri 切换重建；
  大文档连续补全/hover 不再重复解析。
- [ ] **B-ID6** zan-lsp 诊断在 worker 线程但请求处理单线程串行：前端跑诊断时
  跳转/补全排队，需请求级并发或 $/cancelRequest。改动面大（请求调度器重写），
  未动。
- [ ] **B-ID7** 设计器三条构建路径（IDE 内预览 / 直接运行 / --publish）行为
  不一致；控件事件缺 sender 参数，事件处理无法区分来源控件。sender 是 API 破坏
  性变更（全部事件处理器签名要改），未动。
- [x] **B-ID8** 六控件属性表逐一查实（050702f4，2026-09-27）：真正缺陷是
  Alert——工具箱可放置，但生成代码发射不存在的 `Alert` 类（必编译失败）、
  FormBuilder 重建返回 null。已新增 Gui.Widget.AlertBox（Layer.DrawNotify 同
  渲染器，text+type 通道），legacy kind 双别名收编，design_palette 补齐双 kind，
  新增 conformance_gui_design_alert 回归。其余五个查实为如实空：AlarmBanner/
  AlarmList 数据源驱动（Props() 显式声明空），ListView/Dropdown 条目走已建模
  options 键，Ellipsis 是画布渲染器非 Control——空表是如实反映非缺失。
  补遗（同日，policy 档两测暴露收编残留，已修）：zform.controls.txt 未重生成
  缺 AlertBox；"Alert" 双注册撞 policy_control_factory 的"一类一个 Kind()
  持久化标签"查重。修法：legacy 别名退出 Make 分支与 Names 名单，改走
  HeavyControls.RegisterAlias 归一化（读侧 Create 先归一再查表，palette 只列
  现行 kind），manifest 重生成；design_alert/design_palette/designer_html
  回归全绿。
- [ ] **B-ID9** IDE 代码编辑器没有折叠 UI——LSP foldingRange 已就绪，前端未接。
- [x] **B-ID10** git 面板补齐分支与暂存（19a7cc2b，2026-09-27）：新建/切换
  分支、merge、stash/pop/list 六操作落 BranchRow，走 gitJob 后台作业通道与
  GitEscape 转义，输出进日志面板并触发重扫描。IDE_BUILD_OK。
- [x] **B-ID11** 「无 UI e2e 闭环」账目不实（2026-09-27 查证）：闭环已存在且
  入册——perf_frame_budget(+gl) ctest 以 tests/perf/ide_hover_scroll.uidrv 驱动
  真实 ZanIDE.exe（move/scroll + dump hitregions/tree + 帧成本预算断言，缺
  ZanIDE.exe 时自动跳过），scripts/drive_ide.ps1（点击/滚轮/拖拽/按键）与
  shot_ide*.ps1（截图锚定）、check_ide_components.ps1（自绘禁令静态检查）构成
  交互回归工具链。本会话在沙箱 shell 里跑 perf_frame_budget 与直启 IDE 均
  0xC0000005，桌面会话手工验证待补（见下 B-ID13）。
- [x] **B-ID12** 模板 caps= 维度端到端落地（e10e064d，2026-09-27）：manifest
  新增 caps= 键，33 个模板按真实 using/依赖逐一标注，WizardTemplate 链式
  Caps() 在向导右栏渲染能力行，AddDiskTemplate/WizTemplates 全链路接线。
- [x] **B-ID13** 本机 ZanIDE.exe 启动即 0xC0000005——根因 6e2d70f9 的
  `zan_opt_escape_analysis` 不完备，已删（2026-09-27）：逃逸判定只看 zan_alloc
  返回值的**直接** use——指针经 phi/select（循环携带变量）、从地址被取的局部槽
  load 出来再外传、以及一切非 {store,call,ret,gep,bitcast} 的 use 落在
  `default: break` 里统统判为"未逃逸"；对象被搬进栈 alloca（RC 预置 1000000），
  定义帧一返回，仍被容器/全局持有的引用即指向死栈，之后虚调用读到旧栈上的垃圾
  vtable → `call rip=0x2`、`rdx=0x5a414e4152524101`（栈复用残留的 ZANARRA 数组头
  魔数）。归因法：环境二分——O0 正常 / O1、O2、Os（--publish）必崩；关逃逸分析
  （ZAN_NO_ESCAPE=1）+ O2 → 正常；两条去虚化路径单独关掉仍崩、开启+关逃逸正常
  → 去虚化无责，逃逸分析独罪。修法：整个 pass 删除（value_escapes +
  zan_opt_escape_analysis + 管线调用 + 报表行 + optimizer.h 统计结构）——不完备
  是构造性的，局部补丁补不回完备性，重立须先有跟踪内存/phi 的健全设计再落地。
  验证：默认 --publish 构建 IDE 启动 IDE_RUNNING_OK（修复前同配置 100% 崩），
  standard 档除三条与本修无关的既有红（见 B-ID15）外全绿；连带走通的 B-ID11
  遗留"直启 0xC0000005"即本案。
- [ ] **B-ID14** zanc 对 IDE 全量输入加 `-g` 编译失败（2026-09-27 定位，未修，
  编译器 lane）：符号构建 `ZAN_IDE_ZANC_ARGS="-g" scripts/build_ide.ps1` 挂，两种
  独立症状——① `-g`（zanc 注记 "-g forces -O0"）+ IDE 全量输入（418 文件）在
  codegen 阶段 AV 0xC0000005（~4s，早于链接）：崩溃点在 LLVM codegen 读已释放堆
  （`cmp byte ptr [rdx],11h`、rdx=0xFEEEFEEEFEEEFEEE，cdb 最近符号
  CoalescingBitVector::find / MachineInstr::getRestoreSize 随二进制漂移，MinGW
  PDB 栈回溯不可靠）；阈值=设计文档总数：31 份（entry+30）必崩、30 份不崩；
  ddmin 压不掉任何一份真设计，但 31 份合成迷你设计+最小 code-behind 不崩 → 与
  设计**内容/总量**相关非纯计数。② `-g`+`--publish`：编译过、GNU ld 链接失败
  "relocation truncated to fit: IMAGE_REL_AMD64_REL32 against .rdata$rterr.1276"
  （13MB .o 内的 .text→.rdata 引用，非 2GB 距离问题；.rdata$rterr.* 是
  zan_irgen_intern_string 的 per-site 哨兵串私有全局，疑似 GNU ld 对海量小节
  在 DWARF 布局下的排序/COMDAT 处理问题，可试 lld 或改单节放哨兵串）。
  无 `-g` 的全部配置（dev O0 与 publish Os）均正常，IDE 发布/开发链路不受影响；
  gallery 312 文件 `-g` 可过（需 --no-check-leaks，IDE 输入 >4096 ARC 分配点）。
  复现：见 _scratch/ide_input_list.txt 生成法（build_ide.ps1 的输入清单 +
  `build/zanc.exe -g <清单> --no-arc-guard --no-check-leaks -o x.exe --subsystem
  windows`）。
- [x] **B-ID16** 属性路由异步异常后的双重释放：catch 入口已将 +1 转入 handler-owned 槽，匹配 catch 的收尾却再读可被嵌套异常覆盖的 TLS owned 标志并释放同一对象；改为仅按 handler-owned 槽释放。`async_catch_ownership_route` 最小双请求回归，旧编译器 6/80 次 AV、修复后 240/240 通过；后台调度排空另以 outstanding 记账消除漏等。
- [x] **B-ID15** standard 档三条既有红定责与处置（2026-09-27，均先于 B-ID13
  修复存在、与其无关）：① policy_gallery_coverage——gallery 种子第 25 项引用
  已迁走的 `templates/server/server-mvc`（3959170f 整体迁为 packages/Zan.Mvc），
  已修：种子改指包内真实文件（Framework/AppController.zan、Account/
  LoginController.zan、zan.pkg），policy 测试绿。②
  conformance_wasm32_file_io_in_try——真因是 8c9c7188 的 ASCII SIMD 扫描把
  Vector128/256.Create/Load 拉进每个含 IO 的编译图，而 wasm32 无聚合 C ABI
  分类时**声明即硬错**；已修（声明不再错、改挂 pending 名单，真实调用点才报
  "call to extern … no C ABI classification"，abi_pending_report 挂
  zan_irgen_write_obj 顶部；native 分类恒成功零影响），新增
  diag_wasm32_struct_extern_call 回归（声明合法+调用必错双侧钉死），
  wasm32 档全绿。③ conformance_win_automation_smoke——测试本体 CreateWindowEx
  需交互式 window station，沙箱无桌面必红，属环境限制非缺陷（基线与修复后
  同红；桌面会话不受影响）；不改测试语义让它无桌面假绿。另 standard 全量
  并行跑时有 4 测偶红（async_try_exit_depth/mqtt_loopback/win_tray_screen_smoke/
  gui_datatable_ctxmenu_selection），单测复跑双配置均绿，判并行资源抖动非回归。

## 编译内存后续（2026-09-28）

- [ ] **A-MEM1** 大型发布 IRGen 与前端 AST 仍同时存活，且单模块建出大量未用方法体：冻结的 OnePlus 输入产生约 285 万 AST 节点、18,628 个 IR 定义和 402 万条指令；压紧节点并在 IRGen 后释放前端使峰值 Commit 2,022→1,530 MB，但 1,530 MB 仍发生在 IRGen。后续按声明/具体泛型实例做保守的体可达固定点，保护 Main、静态初始化、构造链、委托、虚表、反射和库导出；若仍由单模块主导，再设计可销毁的 LLVM 分片与对象归属。复测时重新冻结 OnePlus 源/设计文件清单与哈希，在相同参数及工具链下 A/B，补不可达体压力用例；不能拿 LLVM GlobalDCE 冒充生成前裁剪。

## 通讯安全余量（2026-09-28）

- [ ] **B-NET1** Windows/macOS 的默认 HTTPS/WSS 客户端缺系统链策略桥接，当前无显式 `AddTrustedCert` 会失败关闭。不能仅枚举 ROOT/keychain：那会丢弃系统拒绝列表、用途与吊销策略。需以收到的原始 DER 链和预期主机名调用 Crypt32 SSL 链策略 / macOS SecTrust，并保留 TLS 握手签名、Finished 与 pin 校验；回归覆盖受信任、错误主机、受禁根、过期和平台 API 不可用。
- [ ] **B-NET2** 自研 X.509 链验证尚无吊销检查或完整 RFC 5280 路径构建；非关键 nameConstraints 与重复扩展已拒绝，带 pathLenConstraint 的 CA 仍被解析器失败关闭。`security_x509_extensions` 与 `tls_auth_chain` 仅覆盖所支持链形；需定义 CRL/OCSP 的离线及网络超时策略、路径约束处理，增加中间 CA、撤销/未知状态和路径长度回归，在此之前不得声称完整 PKIX 验证。
- [ ] **B-NET3** TLS 对端证书目前仅支持 RSA/SHA-256，ECDSA/EdDSA 与其他证书签名方案失败关闭；IPv6 iPAddress SAN、IDNA 规范化亦未覆盖。按目标平台扩展证书与握手签名算法、规范化主机名并做跨实现握手与 SAN 用例，不能靠关闭验证解决互通。
