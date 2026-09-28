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
- [x] **B-NET4h** TLS 记录/握手剩余边界（2026-09-28）：Alert 明文与加密态均要求载荷恰 2 字节（3 字节 `[1,0,0]` 曾被当 close_notify 静默关闭）；ChangeCipherSpec 要求载荷恰 1 字节且值为 1、状态不匹配一律拒绝（TLS 1.2 重复 CCS、密钥未就绪、ServerHello 前 CCS 从静默吞掉改为 fail-closed）；TLS 1.3 `KeyUpdate` 显式拒绝断开（静默忽略会让对端滚动密钥后本端全部解密失败）；服务端证书支持 PEM 链束（叶子在前、中间证书随后，≤8 张且 DER 总长 ≤128 KiB，任何一环解析失败整体拒绝），TLS 1.2/1.3 Certificate 消息发完整链、单证书字节布局不变；`SystemTrustRoots` 抽出 `TrustFromCandidates`：存在但损坏/不可解析的候选路径只跳过不阻断后续候选，全部不可用才 fail-closed。用例 `security_tls_record_bounds`（42 项：明文/加密 Alert 尺寸、CCS 尺寸/值/状态/重复、明文与认证后 KeyUpdate 拒收、record 上限、trust 候选遍历 6 例、三级链 OpenSSL fixture 回环 1.3+1.2 全链验证/显式中间锚/无锚拒绝/错误主机拒绝）；`tls_native`、`tls_auth_chain`、`security_tls_handshake_bounds`、`mysql_tls`/`redis_tls`/`sqlserver_tls` 复跑通过。
- [x] **B-NET4i** X.509 路径构建、pathLenConstraint 求值与 IPv6 SAN（2026-09-28）：`X509Certificate.ParseForTls` 的 basicConstraints 不再把携带 pathLenConstraint 的证书整张拒绝，改为解析存储（非负 INTEGER，>255/负值/非最短编码仍按畸形拒收）并暴露 `HasPathLen()`/`PathLenConstraint()`；SAN `iPAddress` 两处解析器（ParseForTls/ParseDisplay）补齐 16 字节 IPv6 支持，与 IPv4 统一经 `IpSanText` 存规范文本（v6 为 RFC 5952 式压缩小写十六进制，前导零抑制、最长零串压缩）；`VerifyHost` IP 分支改双侧规范化比较（`CanonicalizeHostIp`：`[v6]` 方括号剥离、严格点分 v4 拒前导零防八进制歧义、严格 v6 支持一处 `::` 压缩与末尾内嵌 IPv4、拒 `%zone`/多压缩点/越界段），畸形 IP 字面量一律不匹配（fail-closed），跨族文本不等价（v4 主机不匹配 v6 SAN，`::ffff:x.y.z.w` 需证书侧显式声明）；`TlsEngine.VerifyCertificate` 由线性有序链走查升级为有界候选路径构建：签发者可在信任锚或对端链任意未用位置选取（乱序链可接受，used[] 防重用、步数 ≤ 链长+1），签名与完整 Name 比较仍由 `VerifySignedBy` 强制，每选中 issuer（含锚）按 RFC 5280 §4.15.1.1 求值其 pathLenConstraint 覆盖已消耗的中间 CA 数（hops），约束违规的候选被跳过而非整体拒绝。撤销检查边界：纯 Zan 验证器不做 CRL/OCSP 求值，撤销策略由平台桥接承担（Windows Crypt32 桥保持缓存不足/吊销未知 fail-closed，见 B-NET2），不把"无撤销信息"当 good。用例 `security_x509_path`（60 项：4 根 3 中间 pathLen 解析矩阵、IPv6 SAN 存储规范形 3 例、VerifyHost 矩阵 22 例含 v6 形态/括号/zone/八进制/越界/超长拒绝、TLS 回环 14 连接：root 锚全链、显式中间锚、无关根/PL0 无关根/无锚拒绝、方括号与长形 v6 主机过线、乱序链 [leaf,root,inter] 接受、PL0 中间签叶子接受、PL0 中间下再挂中间拒绝（与 OpenSSL `path length constraint exceeded` 互证）、PL1 锚挂一中间接受、PL0 锚挂中间拒绝、PL0 锚直签叶子接受）；`security_tls_record_bounds`/`tls_native`/`tls_auth_chain`/`tls_hostname`/`security_tls_trust`/`security_x509_extensions`/`x509_certificate`/`mysql_tls`/`redis_tls`/`sqlserver_tls` 复跑通过。
- [x] **B-NET4j** 批次 F：ECDSA 签名/验证、TLS scheme 协商与受限 IDNA（2026-09-29）：新增 `Ecdsa.zan`（P-256 曲线运算、RFC 6979 HMAC-SHA256 确定性 nonce、DER r/s 编码、验证）与 `EcKey.zan`（PKCS#8 与 SEC1 私钥加载，仅 prime256v1 命名曲线，结构不符一律 null）；`X509Certificate` 支持 EC SPKI（非压缩点 65 字节校验 + `Ecdsa.ValidPublicKey`）与 ecdsa-with-SHA256 签名算法（RFC 5758 要求 parameters 缺省，openssl 生成一致；内外两处 signatureAlgorithm 一致性强制），新增 `HasEcKey()`（叶子公钥类型）与 `SigIsEcdsa()`（证书签名算法）两个解耦判定；`TlsHandshake` 双端 ECDSA：服务端 TLS 1.3 CV 按叶子公钥类型选 ECDSA 方案（优先 0x0403=ecdsa_sha256，客户端仅广告 0x0805 时回落 0x0805）、TLS 1.2 SKE 0x0403、套件与叶子公钥类型绑定（EC→0xc02b ECDHE_ECDSA，RSA→0xc02f，4 处硬编码修复）；客户端接受 0x0805/0x0403（均 ECDSA-SHA256 语义）并强制方案与对端证书密钥类型一致（交叉拒绝）；EE 解析补 RFC 8446 允许的 supported_groups（openssl 3.2+ 默认回送，结构校验后忽略）。互操作边界：0x0805 CV 被 openssl 3.5.4 与 python ssl 双栈以 ILLEGAL_PARAMETER/wrong signature type 拒收（wire 经代理 dump 证实合法：P-256 证书、71 字节 DER、transcript 一致；同一签名换 0x0403 双栈全过），openssl 自家 s_server 对 P-256 证书也选 0x0403——落地 0x0403 为主、0x0805 为客户端显式要求的回落。双向互操作：openssl s_client→Zan EC 服务端（TLSv1.3, Verify return code: 0）、Zan 客户端→openssl s_server（握手 + 应用数据往返）。受限 IDNA：`DnsCanonical`（LDH+点 ASCII 限定、拒非 ASCII U-label/空标签/孤立点/双尾点/超长>255、恰一尾随根点双侧剥除）+ `MatchPattern` 重构（通配仅最左单标签、父域须 ≥2 label、A-label 首标签不受通配覆盖、`*` 入 host 拒绝）。用例 `security_x509_ec`（28 项：EC/RSA/混合型解析与 HasEcKey/SigIsEcdsa 解耦、Ed25519 证书解析拒绝、签发矩阵含同 CN 跨算法根拒绝与篡改 ECDSA 签名数学拒绝、TLS 1.3/1.2 EC 回环、EC 叶子×RSA 根与 RSA 叶子×EC 根混合型按密钥类型协商、PL0 深链拒绝、错误锚拒绝）与 `security_host_idna`（24 项：精确/大小写/尾根点/A-label/通配接受，空标签/非 ASCII/下划线/空格/星号/跨标签/A-label 通配/单标签父域/超长拒绝）。

## 未完成 · 通讯与 TLS

- [ ] **B-NET1** Windows 已接入收到的 DER 链与主机名的 Crypt32 链构建/SSL 策略验证；离线缓存吊销未知或错误失败关闭，显式 `AddTrustedCert` 走独立签名链。**批次 E（2026-09-29）**：`zan_io_crypto_windows_ssl_policy` 升级为诊断 seam（仅 1=信任；0=输入/环境拒绝；-1=Crypt32/API 缺失、-2=链构建失败、-3=部分链、-4=不受信根、-5=过期、-6=吊销未知（CACHE_ONLY+AIA 关闭下缓存缺失仍拒绝）、-7=其他信任位、-8=主机名不匹配、-9=其他策略错、-10=缓存 CRL 命中吊销），`TlsEngine` 新增 `OsTrustDiag()` 透出（非 Windows 恒 0）；`security_tls_windows_policy` 扩至 17 项：新增零证书链/含分隔符主机/超长主机恰 0 拒收、不受信链负码带断言、engine 诊断码一致性、OpenSSL `ca` 历史日期过期 fixture（`ValidNow` 拒绝 + 显式锚不可绕过过期）。桥接内部 -5（过期信任位）与 -8（策略主机名）分支需系统信任链才能到达，离线测试不可构造，仅代码评审覆盖；带缓存 CRL 的系统受信任正例、受禁根（Disallowed store 系统状态）、API 不可用（无法卸载系统 DLL）无法在测试内构造，保持 fail-closed 语义不变。Linux 系统 CA 候选路径遍历已在批次 C 加固（`TrustFromCandidates`，损坏候选跳过不阻断）。macOS/iOS 无 SecTrust 桥接，`osTrusted` 恒 false 严格失败关闭，新增桥接需 macOS 工具链编译验证——本仓库开发环境为 Windows，无法验证 Apple 平台代码，不盲发安全关键代码；如需补齐须在 macOS 环境实现并验证后闭账。
- [ ] **B-NET2** 自研 X.509 尚无完整 RFC 5280 路径构建、CRL/OCSP、pathLenConstraint 和名称约束求值；重复扩展与未实现的 `nameConstraints` 已失败关闭。需补中间 CA、撤销/未知状态、路径长度和离线/网络超时策略用例。
- [ ] **B-NET3** RSA/SHA-256 之外的对端算法与名称能力（批次 F 后的剩余边界，详见 B-NET4j）：ECDSA P-256 已支持并双向互操作；IPv6 `iPAddress` SAN 已支持（B-NET4i）；IDNA 已落地受限子集（LDH+点、尾根点、A-label、通配防护；完整 UTS-46 归一/同形字防护未实现，非 ASCII 主机名拒绝，应用层须先 to-ASCII）。仍 fail-closed 的未支持能力：EdDSA（Ed25519 证书解析拒绝，负例钉死）、P-384/secp384r1（SPKI 仅接受 prime256v1，其余命名曲线解析拒绝）、TLS 1.3 服务端 0x0805 仅作客户端显式要求时的回落（openssl 双栈对合法 0x0805 CV 误拒，实测边界记录于 B-NET4j）。这些边界均为安全拒绝（不得以关闭验证换取互通），是否以"严格 fail-closed 边界"闭账待用户确认。
- [x] **B-NET4** TLS 认证前握手累计大小、重复/乱序 `EncryptedExtensions`、畸形扩展向量、ClientHello 压缩方法和重复扩展已限制；Windows Crypt32 函数指针漏参导致的调用崩溃已修。用例 `security_tls_handshake_bounds`、`security_tls_windows_policy`，提交 `a6fbc562`。

## 最近验证

- 批次 D 受影响面直编直跑全绿：新 `security_x509_path` 60/60（.out 吻合，路径构建/pathLen/IPv6 SAN 全矩阵）；ctest TLS+X509 切片 15/15（含 `mysql_tls`/`redis_tls`/`sqlserver_tls`/`publish_tls_bundle`/`static_publish_tls_stub`）。
- 批次 D 提交前 standard 档 1004 项：**1002 通过**，2 个失败（`orm_schema_ddl` 超时、`tdengine_pool`）单独复跑 **2/2 全过**，均为并行负载下 DB 协议环境毛刺，不在 X.509 改动面上。本轮前三次 standard 尝试因并发会话重链 zanc/清理 _scratch 连坐中止（`package_install_mvc` 单跑 27.8s 复绿），与代码无关。
- `standard` 档 1004 项：**999 通过**；5 个失败（`win_automation/win_tray/win_uielement` UI 冒烟、`sqlserver_tds` 超时、`tdengine_pool`）单独复跑 **5/5 全过**，均属并行负载下环境敏感毛刺，不在 TLS 改动面上。
- 批次 C 受影响面直编直跑全绿：新 `security_tls_record_bounds` 42/42（.out 吻合）、`tls_native` 24、`tls_auth_chain` 25、`tls_hostname` 6、`security_tls_handshake_bounds` 33、`security_tls_receive_bounds` 9、`security_tls_trust` 8、`security_tls_windows_policy` 10、`https_binary_body` 12、`mysql_tls` 14、`redis_tls` 9、`sqlserver_tls` 17；ctest TLS 切片 9/9。
- 批次 B（fde0f92e）时 standard 档 1003 项 1002 过，唯一失败 `win_tray_screen_smoke` 单跑 4/4 过；首轮因会话后台任务被终止连坐（无关测试成批 0xc000026b 瞬死）的 7 项单独复跑全部通过。
