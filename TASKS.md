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
- [ ] **B-ID3** rename/references 是整词文本匹配，非 binder 支撑：跨作用域会
  误伤同名符号。需把 intellisense 符号表接入 rename/references 做作用域感知。
- [ ] **B-ID4** semanticTokens / inlay hints 未实现。
- [ ] **B-ID5** intellisense 每次补全 ~2MB malloc、全量重建符号表；大文档补全
  卡顿，需增量更新与复用。
- [ ] **B-ID6** zan-lsp 诊断在 worker 线程但请求处理单线程串行：前端跑诊断时
  跳转/补全排队，需请求级并发或 $/cancelRequest。
- [ ] **B-ID7** 设计器三条构建路径（IDE 内预览 / 直接运行 / --publish）行为
  不一致；控件事件缺 sender 参数，事件处理无法区分来源控件。
- [ ] **B-ID8** 六个控件的属性表为空，属性面板只覆盖常用控件子集。
- [ ] **B-ID9** IDE 代码编辑器没有折叠 UI——LSP foldingRange 已就绪，前端未接。
- [ ] **B-ID10** git 面板只有 add/commit/push/pull，缺 branch/merge/stash。
- [ ] **B-ID11** IDE 无 UI e2e 验证闭环（截图锚定 + 探针断言未自动化），回归
  靠手测。
- [ ] **B-ID12** IDE「新建项目」模板无 capabilities= 维度（GUI/网络/加密等
  预勾选缺失）。
