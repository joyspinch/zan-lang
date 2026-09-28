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

- [~] **A-MEM1** 大型发布已落地保守生成前裁剪：声明先行、Main/初始化/构造/委托/虚表/反射/库导出按固定点保活；非发布与 `--emit-ir` 保留用户体以免吞掉降层诊断，发布仅裁剪 stdlib 体，未用声明留一块 `unreachable` 以满足 LLVM。冻结 OnePlus 402 输入重测：IR 定义 18,628→18,488、指令 4,022,035→3,545,750，峰值 Commit 1,530→1,414 MB（仍由 IRGen 主导，未彻底闭账）。`dead_method_pre_ir` 行为+IR 回归、smoke 313/313、standard 可执行集 999 项中仅并行 `zandb_p3` 偶发红且串行通过；阶段一 `12d7e911` 仅建立 generated-object 向量，明确无内存收益声明。独立 LLVM 生命周期探针在同一 context 串行生成/写出/销毁 8 个高负载 module（每片 128 函数×256 算术指令）通过，PrivateUsage 首片后不随 module 数线性增长；因此当前主要阻碍不是 module dispose 泄漏，而是 Zan IRGen 普遍持有 module-local LLVM handles、internal linkage 和全局辅助/类型状态，尚无可安全拆分的闭合函数族；继续保留单模块 fallback，后续需重构 ABI/声明重建后再评估分片。问题边界扩大到所有大型项目后，补做四处项目无关的规模化热点优化（2026-09-28）：输入去重 O(K²)→FNV-1a 开放寻址（含 POSIX dev/ino 索引）；binder 成员冲突检查 O(M²)→按类型成员名索引，索引异常或同名候选超过 64 时回退原全量扫描并保持诊断顺序；tuple 类型缓存线性查找→规范化签名哈希；IRGen `body_has_live_use` O(W²)→LLVM 函数指针索引，未知父函数保守视为存活。6000 成员、401 输入、七类诊断和 publish 定向探针通过。smoke 313 项两例并行负载抖动均隔离通过；standard 1003 项并行运行出现 62 个负载/环境红，代表性串行复跑 6/7 通过，唯一稳定红为需要交互桌面的 `win_tray_screen_smoke`，未发现本轮编译器回归。

## 已闭账 · 通讯协议加固（2026-09-28）

- [x] **B-NET4a** TLS 跨 record 握手重组、ServerHello 扩展边界、记录类型/版本 fail-closed 与认证前预算；用例 `security_tls_handshake_bounds`。
- [x] **B-NET4b** Content-Length/chunk-size 乘加溢出、chunk 数据/长度行严格 CRLF；用例 `http_chunk_len_overflow`、`http_client_keepalive`、`security_forwarder_wire`。
- [x] **B-NET4c** MVC/WS/WSS 升级响应、帧掩码/RSV/控制帧/Close/UTF-8 门禁与请求目标注入；用例 `security_web_ws_upgrade`、`security_ws_client_protocol`、`security_ws_handshake`、`security_ws_worker`、`ws_protocol_gate`、`ws_loopback`。
- [x] **B-NET4d** TLS 公钥 pin 不受 `disableVerify` 绕过、证书链与主机名策略错误分离；用例 `tls_auth_chain`。
- [x] **B-NET4f** TLS 接收缓冲区范围 fail-closed、X25519 低阶/全零共享密钥拒绝、TLS 1.2 ClientKeyExchange 尾随字节拒绝；用例 `security_tls_receive_bounds`、`tls_auth_chain`。
- [x] **B-NET4e** HttpClient Connection token 按逗号/OWS/大小写解析，Proxy TLS 上游握手使用配置 timeout；用例 `http_client_keepalive`、`security_forwarder_wire`、`http_forwarder_keepalive`。
- [x] **B-NET4g** HTTP/Proxy 报文边界与 IPv6 链路（2026-09-28）：`HttpResponse.Parse` 严格状态行（HTTP/1.0|1.1 + OWS + 恰三位 1xx-5xx 码 + 第 4 位非数字）与头部逐行校验（token 名、冒号前无空白、值禁控制字符 HTAB 除外），TE+CL 冲突与头块未终止拒收，全部以 statusCode=0 fail-closed（不返回 null，12 个调用点语义不变）；`HttpClient.BuildRequestHead` 统一 method token/host/path 校验（守卫在连接后发送前抛 HttpRequestException），两处下载旁路统一走 `BuildDownloadRequest`（Range 行由构建器插入）；`HttpServer`/`HttpForwarder` setter 钳界（timeout 1ms-24h、chunk/head 1KiB-1MiB、连接 1-1e6、idle 1-4096/24h），异常配置不再全拒绝或无界分配；`HttpForwarder.ParseUpstream` bracket-aware 严格解析（`[IPv6]:port` 支持、junk 端口/未闭合括号/裸 IPv6 每请求 502），响应状态行/头部严格验证（StatusOf/IsResponseHeadValid），trailer 禁止响应 Connection/Proxy-Connection 动态提名字段；IPv6 三处根因：`TcpListener` 按主机含 ':' 选 CreateTcp6、代理 `ConnectUpstream`/`ServeConnect` 改 `TcpClient.ConnectAsync` 按解析地址族逐条建连、`ExternalTarget.ValidIPv6` 修正前导/尾随 `::` 压缩误杀。用例 `security_forwarder_status`（21 项：4 畸形状态行/3 畸形头部/TE+CL/未终止→502 不透传、Connection 提名 trailer 拒转发、bracketed IPv6 端到端 + Host 头方括号还原、junk/裸 IPv6 upstream→502、极端 setter 后可用、method 注入守卫、下载路径守卫）、`http_parser_hardening`（26 项：10 条畸形 wire statusCode=0）。边界：`ValidIPv6` 不接受内嵌 IPv4 尾段与 %zone（严格拒绝），纯 AAAA 上游可通。

## 未完成 · 通讯与 TLS

- [ ] **B-NET1** Windows 已接入收到的 DER 链与主机名的 Crypt32 链构建/SSL 策略验证；离线缓存吊销未知或错误失败关闭，显式 `AddTrustedCert` 走独立签名链。`security_tls_windows_policy` 已覆盖不受信任、错误主机、畸形输入和显式 CA 正例，但缺少带缓存 CRL 的系统受信任正例、受禁根、过期和 API 不可用回归；缓存缺失可能拒绝有效公网站点。macOS 仍无 SecTrust 桥接，默认无显式 CA 时失败关闭。
- [ ] **B-NET2** 自研 X.509 尚无完整 RFC 5280 路径构建、CRL/OCSP、pathLenConstraint 和名称约束求值；重复扩展与未实现的 `nameConstraints` 已失败关闭。需补中间 CA、撤销/未知状态、路径长度和离线/网络超时策略用例。
- [ ] **B-NET3** 对端证书和握手目前主要支持 RSA/SHA-256；ECDSA/EdDSA、IPv6 `iPAddress` SAN、完整 IDNA 规范化尚未覆盖，不能靠关闭验证解决互通。
- [x] **B-NET4** TLS 认证前握手累计大小、重复/乱序 `EncryptedExtensions`、畸形扩展向量、ClientHello 压缩方法和重复扩展已限制；Windows Crypt32 函数指针漏参导致的调用崩溃已修。用例 `security_tls_handshake_bounds`、`security_tls_windows_policy`，提交 `a6fbc562`。

## 最近验证

- `standard` 档 1003 项：**1002 通过**；唯一失败 `conformance_win_tray_screen_smoke`（托盘/屏幕交互冒烟）单独复跑 4 个变体全过，属并行负载下的环境敏感毛刺，不在通讯改动面上。
- 新增/受影响面直编直跑全绿且 `.out` 金样吻合：`security_forwarder_status` 21/21、`http_parser_hardening` 26/26、`security_forwarder_wire` 15/15、`http_forwarder_framing/keepalive/stream/tunnel`、`http_client_keepalive/binary/redirect/timeout`、`security_http_client`、`ipv6`；ctest 中 `security_forwarder_wire`、`http_forwarder_framing`、`proxy_binary_body`、`security_tls_*`、`x509_certificate` 均通过。
- 首轮 standard 因会话后台任务被终止连坐（无关测试成批 0xc000026b 瞬死）的 7 项（mqtt_lwt_retain、game_idle、sdk_jd_modules、chart_axis_scale_extent、arpg_database、dap_integration、package_install_mvc）单独复跑全部通过，非代码回归。
