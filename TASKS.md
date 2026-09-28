# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：`[ ]` 未完成 · `[~]` 进行中 · `[x]` 已闭账 · `[-]` 已作废。
>
> 维护原则：未完成项保留复现路径和下一步；已完成项只保留编号、结论、提交或测试证据，详细过程由 git 历史承担。

## 已闭账 · C 类工程规范

- [x] **C-1** mingw GUI 归档补齐 `gui_runtime_dwrite.o`；四个手工 GUI 构建脚本已修复（2026-09-27）。
- [x] **C-2** win-x64 静态驱动归档启用 `NDEBUG`、补齐 dwrite 并保留完整 `.libs`；发布 exe 不再泄漏构建机路径（2026-09-27）。
- [x] **C-3** PE GUI 音频驱动拆分为按需归档成员；无音频程序不再拉入 WASAPI/Vorbis，音频程序回归通过（2026-09-27）。

## 已闭账 · 硬件加速与基础密码学

- [x] **B-HW1** `TlsStream` 改为自研 Zan TLS 1.2/1.3 记录与握手栈；AES-GCM、X25519、HKDF、RSA-CRT、X.509 用例通过。提交 `a39e0da5`。
- [x] **B-HW2** ARM64 SHA、AES、GHASH、CRC32C、SM3 内核落地；KAT、qemu-aarch64、软件回退和 standard 验证通过（2026-09-26）。
- [x] **B-HW3** SM4 AES-NI/VAES/ARM64 硬件内核与常数时间回退落地；官方 KAT 和双路径验证通过（2026-09-27）。
- [x] **A-HW4** `byte[]`→`string` 视图长度改用数组元素数，修复 NUL 截断；用例 `string_view_length`（2026-09-26）。
- [x] **A-HW5** `Md5.Hash` 空消息递归修复；用例 `hw_crypto_pixel`（2026-09-26）。
- [x] **B-HW4** linux GUI 静态归档脚本及 CI 产物回填落地（2026-09-27）。

## 已闭账 · IDE / LSP / DAP

- [x] **B-ID1** DAP pause、hit-count、logpoint 真实现；`dap_integration_test` 通过（提交 `589e6183`）。
- [x] **B-ID2** LSP formatting、rangeFormatting、highlight、folding、prepareRename、诊断范围补齐；集成回归通过（2026-09-27）。
- [x] **B-ID3** 局部符号 rename/references/documentHighlight 改为作用域感知；成员级 rename 仍是后续边界（提交 `c83ea8f0`）。
- [x] **B-ID4** semanticTokens 与 inlay hints 端到端实现；LSP 集成回归通过（2026-09-27）。
- [x] **B-ID5** intellisense 按 `(uri, version)` 复用引擎，关闭文档失效缓存；连续请求不再全量重建（提交 `c83ea8f0`）。
- [x] **B-ID8** Alert/AlertBox 控件、legacy kind 归一化和设计器回归补齐；设计器相关用例通过（提交 `050702f4`）。
- [x] **B-ID10** git 面板分支、merge、stash/pop/list 接入后台作业通道（提交 `19a7cc2b`）。
- [x] **B-ID11** 无 UI IDE e2e 工具链清账；真实驱动、命中区导出、截图锚定和组件静态检查已入册。
- [x] **B-ID12** 模板 `caps=` 能力维度贯通 manifest、向导和磁盘模板（提交 `e10e064d`）。
- [x] **B-ID13** 删除不完备逃逸分析，修复 IDE `--publish` 启动崩溃；`IDE_RUNNING_OK` 与相关回归通过（2026-09-27）。
- [x] **B-ID15** standard 三条既有红完成定责：gallery 路径和 wasm ABI 已修；Windows 桌面测试保留为环境限制；偶发并行红复跑确认非回归（2026-09-27）。
- [x] **B-ID16** 异步异常路由双重释放修复；`async_catch_ownership_route` 修复后 240/240 通过。

## 未完成 · IDE / 编译器

- [ ] **B-ID6** zan-lsp 请求处理仍单线程串行；诊断期间跳转/补全排队。需请求级并发或 `$/cancelRequest`。
- [ ] **B-ID7** 设计器预览、直接运行、`--publish` 三路径行为仍不一致；事件缺 sender，需评估 API 破坏性变更后统一。
- [ ] **B-ID9** IDE 代码编辑器尚无折叠 UI；LSP `foldingRange` 已就绪，前端未接。
- [ ] **B-ID14** IDE 全量输入加 `-g` 仍有两类问题：31 份设计触发 codegen 崩溃；`-g --publish` 可能触发 GNU ld `IMAGE_REL_AMD64_REL32`。复现清单在 `_scratch/ide_input_list.txt`，不得用换形输入绕过。

## 未完成 · 编译内存

- [~] **A-MEM1** 大型发布已落地保守生成前裁剪：声明先行、Main/初始化/构造/委托/虚表/反射/库导出按固定点保活；非发布与 `--emit-ir` 保留用户体以免吞掉降层诊断，发布仅裁剪 stdlib 体，未用声明留一块 `unreachable` 以满足 LLVM。冻结 OnePlus 402 输入重测：IR 定义 18,628→18,488、指令 4,022,035→3,545,750，峰值 Commit 1,530→1,414 MB（仍由 IRGen 主导，未彻底闭账）。`dead_method_pre_ir` 行为+IR 回归、smoke 313/313、standard 可执行集 999 项中仅并行 `zandb_p3` 偶发红且串行通过；后续仍需更强的 stdlib 压力与后端分片评估。

## 未完成 · 通讯与 TLS

- [ ] **B-NET1** Windows 已接入收到的 DER 链与主机名的 Crypt32 链构建/SSL 策略验证；离线缓存吊销未知或错误失败关闭，显式 `AddTrustedCert` 走独立签名链。`security_tls_windows_policy` 已覆盖不受信任、错误主机、畸形输入和显式 CA 正例，但缺少带缓存 CRL 的系统受信任正例、受禁根、过期和 API 不可用回归；缓存缺失可能拒绝有效公网站点。macOS 仍无 SecTrust 桥接，默认无显式 CA 时失败关闭。
- [ ] **B-NET2** 自研 X.509 尚无完整 RFC 5280 路径构建、CRL/OCSP、pathLenConstraint 和名称约束求值；重复扩展与未实现的 `nameConstraints` 已失败关闭。需补中间 CA、撤销/未知状态、路径长度和离线/网络超时策略用例。
- [ ] **B-NET3** 对端证书和握手目前主要支持 RSA/SHA-256；ECDSA/EdDSA、IPv6 `iPAddress` SAN、完整 IDNA 规范化尚未覆盖，不能靠关闭验证解决互通。
- [x] **B-NET4** TLS 认证前握手累计大小、重复/乱序 `EncryptedExtensions`、畸形扩展向量、ClientHello 压缩方法和重复扩展已限制；Windows Crypt32 函数指针漏参导致的调用崩溃已修。用例 `security_tls_handshake_bounds`、`security_tls_windows_policy`，提交 `a6fbc562`。

## 最近验证

- `ctest --test-dir build --output-on-failure -R 'conformance_(security_|tls_|http_|jwt_rs256|ws_)'`：通讯与密码学定向集 **37/37** 通过。
- 完整 `standard` / `full` 档未在本轮完整跑完，不在此清单中宣称通过。
