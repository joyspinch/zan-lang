> **归档说明（2026-09-21）**：本文档为早期标准库能力迁移计划草案，引用的外部迁移基准与能力规范已废止。现行标准库设计与命名空间规范请参考 [docs/STDLIB.md](../STDLIB.md)。

# 标准库补齐实施计划（来源：aardio 能力对照，2026-09-10 定稿）

> 来源盘点：`docs/aardio-capability-migration.md` §6（含 §6.4 示例面、§6.5
> 补充档）。本文是**可直接派工**的实施版：每一项都重新核对过 Zan stdlib
> 现状（2026-09-10），给出落点、前置依赖、验收方式。动工前不需要再盘点，
> 只需要按项执行。
>
> 总纪律（见 `app-migration` skill「语义照原版，写法归 Zan」）：aardio 源码
> 只当 API 清单与行为规格；实现用 Zan 现有底座 + 强类型类；每项一个提交，
> 带 conformance 测试与示例。

## 0. 一页总览

| # | 项 | 落点 | 类型 | 难度 | 前置 |
|---|---|---|---|---|---|
| 1 | System.Xml | `stdlib/System/Xml/` | 新库 | L | 无 |
| 2 | Base32 | `System/Security/Cryptography/Base32.zan` | 新类 | S | 无 |
| 3 | ChineseNumber | `System/Text/ChineseNumber.zan` | 新类 | S | 无 |
| 4 | Text.Patch（SEARCH/REPLACE） | `System/Text/Patch.zan` | 新类 | S | 无 |
| 5 | EditorOps（EOL 探测/统一） | `System/Text/EditorOps.zan` | 新类 | S | 无 |
| 6 | IntSegments | `System/Text/IntSegments.zan` | 新类 | S | 无 |
| 7 | ArgsTable + CmdLine | `System/Text/CmdLine.zan` | 新类 | S | 无 |
| 8 | Console 进度条/加载动画 | `System/ConsoleProgress.zan` | 新类 | S | 无 |
| 9 | 命名互斥体（单实例） | `System/Threading/NamedMutex.zan` | 扩展 | S | 无 |
| 10 | HostsFile | `System/Net/HostsFile.zan` | 新类 | S | 无 |
| 11 | Ftp 客户端 | `System/Net/Ftp/FtpClient.zan` | 新库 | M | 无 |
| 12 | Uri（宽松解析） | `System/Net/Uri.zan` | 新类 | S | 无 |
| 13 | 网络发现四件套 mDNS/SSDP/WOL/STUN | `System/Net/Discovery/` | 新库 | M | Socket 组播 |
| 14 | GBK/GB18030/Big5 编码 | `System/Text/Encoding` 扩展 + runtime | 扩展 | L | 无 |
| 15 | DateTime 格式串双向 | `System/DateTime` 扩展 | 扩展 | M | 无 |
| 16 | Zip 读侧 ZipCrypto | `System/IO/Compression/Zip` 扩展 | 扩展 | M | 无 |
| 17 | DES/3DES + RC4（遗留互操作） | `System/Security/Cryptography/{Des,Rc4}.zan` | 新类 | M | 无 |
| 18 | X.509 证书只读 | `System/Security/Cryptography/X509.zan` | 新类 | M | RsaKey |
| 19 | NamedPipe IPC | `System/IO/Pipes/` + runtime | 新库 | M | runtime 探针 |
| 20 | TF-IDF | `System/Text/Tfidf.zan` | 新类 | S | 无 |
| 21 | SunTimes（日出日落/节气时刻） | `System/Time/SunTimes.zan` | 新类 | M | 无 |
| 22 | LatestFile + 批量文件骨架 | `System/IO/FileOps.zan` | 新类 | S | 无 |
| 23 | 已迁库示例补齐 | `examples/` | 示例 | S | 对应库已在 |

已确认**不缺**（曾有疑虑，复核排除）：`PathEx.Glob`、`Text.RegularExpressions`、
`Text.Bm25Index`、`IO.IniFile`、`IO.Watch.DirectoryWatcher`、`Net.CookieJar`、
`Net.HttpClient`（含 multipart 上传）、`Data.ZanDb.KvStore`（持久化配置）、
`IO.KnownFolders.Temp`、`Net.NetworkInterface`（含 MAC）、`Console`（内建
WriteLine/ReadKey/颜色）。`Sdk.Wechat.XmlUtil` 与 `WebDav.ParseMultiStatus`
是手写标签扫描（各自注释里都写了"需要完整 DOM 时换 System 层解析器"），
正是 #1 的两个收玫对象，不是已有等价物。

---

## 1. S 级（纯算法、零依赖、一天内一项，先冲量）

### 1.1 `System.Security.Cryptography.Base32`

- aardio 参考：`string/base32.aardio`（96 行，RFC 4648）。
- 落点：`stdlib/System/Security/Cryptography/Base32.zan`。
- API：`static string Encode(byte[] data, bool pad)`、`static byte[] Decode(string text)`
  （非法字符返回 null 或抛 `ArgumentException`，与 `Hex` 类风格一致）。
- 收尾动作：`Otp` 补一个 `FromBase32Secret(string)` 构造入口——RFC 6238
  的 secret 标准就是 Base32，现在 Otp 只收原始字节，接 Google Authenticator
  风格密钥时用户得自己转。
- 验收：`tests/conformance/base32_vectors.zan` 用 RFC 4648 官方向量
  （""/"f"/"fo"/"foo"/…，含无填充变体）。

### 1.2 `System.Text.ChineseNumber`

- aardio 参考：`string/chineseNumber.aardio`（216 行，简繁两套字符表）。
- 落点：`stdlib/System/Text/ChineseNumber.zan`。
- API：`static string Number(string digits)`（含千分位/下划线分隔输入）、
  `static string Number(double v)`、`static string Money(double v)`
  （壹贰叁 + 元角分/圓角分，简繁由构造参数或静态双入口定）。
- 注意：不要做成"万能类"。aardio 里的中文日期时间格式化不迁（`DateTime`
  格式串 #15 落地后自然表达）。
- 验收：aardio 示例 `examples/Text/chineseNumber.aardio` 的输入输出对
  逐条转成 `tests/conformance/chinese_number.zan` 断言（123456789…、
  12305000.137、金额 12003089.35、"010" 补零）。

### 1.3 `System.Text.Patch`

- aardio 参考：`string/patch.aardio`（530 行，Aider 风格 SEARCH/REPLACE 块，
  版本号 2026.07.19.0）。
- 落点：`stdlib/System/Text/Patch.zan`。
- API：`static PatchResult Apply(string text, string patch)`——多块解析、
  块内字面子串唯一性校验、未命中/多处命中报错（错误信息带块序号）；
  `PatchResult` 是类（成功标志 + 新文本 + 失败块号），不是字符串。
- 价值：AI 编码工具链直接消费（本仓库自己的 agent 工作流）。
- 验收：三态用例——全块应用成功 / SEARCH 不唯一被拒 / 文本无匹配被拒，
  输出 golden。

### 1.4 `System.Text.EditorOps`

- aardio 参考：`string/editor.aardio`（EOL 探测/统一/计数）。
- 落点：`stdlib/System/Text/EditorOps.zan`。
- API：`static string DetectEol(string text)`（返回 "\r\n"/"\n"/"\r"/""）、
  `static string Normalize(string text, string eol)`、`static int LineCount(string text)`。
- 消费方：`Gui/Component/CodeEditor`（现状组件内无 EOL 处理，grep 证实）。
- 验收：混合 EOL 探测、CRLF→LF 归一、空串/无换行边界。

### 1.5 `System.Text.IntSegments`

- aardio 参考：`string/intSegments.aardio`。
- 落点：`stdlib/System/Text/IntSegments.zan`。
- API：`static List<int> Parse(string spec)`（"1,3,5-8"、倒序区间 "8-5"、
  空段容错）、`static string Format(List<int> values)`（连续段合并）。
- 验收：往返一致 + 非法段报错的负例。

### 1.6 `System.Text.CmdLine`（两个语义合一）

- aardio 参考：`string/cmdline.aardio`（argv 切分）+ `string/args.aardio`
  （参数表 ↔ 命令行文本双向序列化，dash-case 与引号转义）。
- 落点：`stdlib/System/Text/CmdLine.zan`。
- API：
  - `static List<string> Split(string line)`——按平台 shell 规则切 argv
    （Windows CommandLineToArgvW 语义：反斜杠转义 + 引号；纯算法实现，
    不调 Shell32，保证跨平台一致，规则写进类注释）。
  - `static string Join(List<string> argv)`——反向拼接（含转义）。
- 与 `Environment.ArgAt`（编译器内建，只有裸数组）互补：一个管"程序收到
  什么"，一个管"拼出去给别人"。
- 验收：含引号/反斜杠/内嵌空格的往返用例，对照
  `CommandLineToArgvW` 官方语义表。

### 1.7 `System.ConsoleProgress`

- aardio 参考：`console/progress.aardio`（单行重绘）+ `console/loading`
  （十几套 ASCII 动画帧表，`examples/Console/loading.aardio`）。
- 落点：`stdlib/System/ConsoleProgress.zan`。
- API：`ProgressBar`（`Set(int percent, string msg)`/`Add`/`Done(string msg)`）
  与 `Spinner`（帧表内置 5~6 套，照搬 aardio 帧数据；`Start`/`Stop`）。
- 行为规格：非 TTY（重定向）时降级为逐条打印——检测方式：`isatty` 缺
  就走降级（探针确认 Windows 侧 `_isatty`/POSIX `isatty` 经 DllImport 可用）。
- 颜色：`Console.ForegroundColor`（内建 ANSI SGR）。
- 验收：TTY 下人工看（examples 手动示例），重定向下 golden 断言。

### 1.8 `System.Threading.NamedMutex`

- aardio 参考：`process/mutex.aardio`（Open→conflict 判定 → 单实例守护）。
- 落点：`stdlib/System/Threading/NamedMutex.zan`，不改既有 `Mutex`。
- 现状：`Threading.Mutex` 已有 Windows `CreateMutexA` 与 POSIX pthread
  两条分支，但名字传空串（匿名），命名语义未暴露。
- API：`static NamedMutex OpenOrCreate(string name)`、`static bool Exists(string name)`
  （冲突检测=单实例判定）、`Release()`/`Close()`。
- 平台：Windows `CreateMutexA(name)`/`OpenMutexA`；POSIX 文件锁
  （`/tmp` 下按名建锁文件 + `flock`，语义=同名互斥，文档写明与内核对象
  的差异：崩溃后锁自动释放，Windows 则需 `Abandoned` 处理）。
- 验收：同进程二次 Open 冲突断言；Windows 上双进程 smoke。

### 1.9 `System.Net.HostsFile`

- aardio 参考：`fsys/hosts.aardio`。
- 落点：`stdlib/System/Net/HostsFile.zan`。
- API：`static List<HostsEntry> Read()`、`static void Add(string ip, string host)`、
  `static void Remove(string host)`、`static void FlushDns()`。
- 平台：路径常量两套（`%SystemRoot%\System32\drivers\etc\hosts` 与
  `/etc/hosts`）；FlushDns Windows 走 `DnsFlushResolverCache`（dnsapi），
  POSIX 空操作（文档注明）。写操作需要管理员——失败抛明确异常不静默。
- 验收：读写往返（测试用临时 hosts 路径注入，不碰真文件）。

### 1.10 `System.Text.Tfidf`

- aardio 参考：`string/tfidf.aardio`。
- 落点：`stdlib/System/Text/Tfidf.zan`，与 `Bm25Index` 同族（API 风格对齐
  `Bm25Index`：AddDocument/Query）。
- 验收：小型语料打分排序 golden。

### 1.11 `System.IO.FileOps`（LatestFile + 批量骨架合并）

- aardio 参考：`fsys/latest.aardio` + `fsys/batch.aardio`（合并成一个
  小类，不做两个 API 面）。
- 落点：`stdlib/System/IO/FileOps.zan`。
- API：`static string LatestMatching(string dir, string glob)`、
  `static int RenameBatch(string dir, string glob, Func<string,string> rename)`
  （回调签名按 Zan delegate 惯例；探针确认 delegate 传参现状）。
- 验收：临时目录冒烟。

## 2. M 级（有协议/数据表/平台分支，单项 1~3 天）

### 2.1 `System.Net.Ftp`

- aardio 参考：`inet/ftp.aardio`（WinInet；语义=连接/列目录/上传/下载/
  建删目录/改名的动词集）。
- 落点：`stdlib/System/Net/Ftp/FtpClient.zan`。
- 实现：**不迁 WinInet**（仅 Windows），用 `TcpClient` + `async` 手写
  FTP 控制连接（USER/PASS/PASV/LIST/RETR/STOR/CWD/DELE/MKD/RNFR/RNTO/QUIT），
  数据连接走 PASV 被动模式。aardio 源码只用来核对回复码与 PASV 解析语义
  （227 进入被动模式返回 `h1,h2,h3,h4,p1,p2`）。
- 验收：对本地起一个极简 FTP 服务端（测试内嵌，仿 `examples/net/http_client`
  自带服务端模式）做全动词往返；`examples/net/ftp_client.zan` 对公网
  只读站点（如 RFC mirror）做一次目录列举示例。

### 2.2 `System.Net.Uri`

- aardio 参考：`inet/urlpart.aardio`（scheme/host/user/port/query 分段）。
- 落点：`stdlib/System/Net/Uri.zan`。
- 与 `ExternalTarget` 的分工写进两个类注释：`ExternalTarget.Parse` 是
  **安全严格**解析（拒绝 userinfo/百分号/片段，服务出站策略用）；
  `Uri.Parse` 是**通用宽松**解析（允许 userinfo/片段/任意 scheme，
  HttpClient/WebSocket/日常用）。字段：scheme/user/host/port/path/query/
  fragment，`TryParse` 返回 null 而非抛。
- 验收：`http/https/ws/wss/ftp` scheme、IPv6 字面量、缺省端口表、
  带 userinfo 与 fragment 的解析 golden。

### 2.3 网络发现四件套 `System.Net.Discovery`

- aardio 参考：`wsock/udp/{mdnsClient,ssdpClient,wolClient,stunClient}.aardio`。
- 落点：`stdlib/System/Net/Discovery/{Mdns,Ssdp,WakeOnLan,Stun}.zan`。
- 前置探针（半天）：`UdpClient` 目前只有 Bind/Send/Recv/Broadcast，无组播。
  `Socket.SysSetSockOpt` 是直转发且已暴露——探针用 `IP_ADD_MEMBERSHIP`
  （Windows/POSIX 同为 level 0, optname 10）验证组播加入/收包，过了再开工；
  探针进 `_scratch/`，结论回填本节。需要给 `UdpClient` 补
  `JoinMulticastGroup(string group, string localIp)`。
- 协议常量照 aardio（**只抄常量与报文格式，不抄实现**）：mDNS 组播
  224.0.0.251:5353、SSDP 239.255.255.250:1900（M-SEARCH/NOTIFY）、
  WOL 魔术包（6×0xFF + 16×MAC）、STUN magic cookie 0x2112A442。
- 验收：WOL（报文字节 golden）、SSDP/STUN（本地双端 self-test）、
  mDNS（同机自问自答 smoke）。

### 2.4 GBK/GB18030/Big5 编码转换

- aardio 参考：`fsys/codepage.aardio`（按 BOM/名称转码）。
- 现状：`Encoding` 仅 UTF-8 一族；GBK 解码散落在 `Process.WinCapture`
  的 `#if WINDOWS` 分支（kernel32 `MultiByteToWideChar` 直调）。
- 落点分两层：
  - Windows：继续 kernel32（代码页常量换 936/54936/950），把
    `Process` 里的做法提炼成 `Encoding` 的公共静态方法。
  - POSIX：**musl 内置 iconv**（`iconv_open("UTF-8","GBK")`，静态链接无
    外部依赖），经 `[DllImport("crt")]` 接入；OHOS/Android 同 musl。
- API：`static string FromCodePage(byte[] bytes, int codePage)`、
  `static byte[] ToCodePage(string text, int codePage)` + 常量
  `Gbk/Big5/Gb18030`；`DetectBom(byte[])` 照 aardio 的 BOM 表。
- 验收：双向往返（中文 GBK、繁体 Big5、emoji 遇 GBK 的替换符行为）；
  conformance 不依赖平台——Windows 用 kernel32、CI Linux 用 iconv，
  两侧各跑各的（golden 相同）。
- 风险：musl iconv 不认 "GB18030" 别名时需用 "CP936"/"UTF-8" 全名探测，
  探针先行。

### 2.5 `DateTime` 格式串双向

- aardio 参考：`time/util.aardio`（`%Y-%m-%d %H:%M:%S` 族）。
- 现状：只有 `ToString()`（固定类 ISO）与严格 `ParseDate("YYYY-MM-DD")`。
- 落点：`DateTime` 扩展 `static string Format(string pattern)`、
  `static DateTime ParseExact(string text, string pattern)`
  （占位符集：Y m d H M S + 字面量；不做 locale）。
- 验收：往返 + 非法输入 null 负例。

### 2.6 Zip 读侧 ZipCrypto

- aardio 参考：`zlib/zip.aardio`（minizip 带 password 参数）。
- 现状：`Zip` 明写"无加密"（读 `Extract` + 写 `Create` 均不处理加密位）。
- 落点：`Zip.Extract`/`Zip.Read` 补：检测 entry 加密位（general purpose
  bit 0），带口令参数重载走 ZipCrypto 解密（CRC32 已有，密钥流三表
  初始化照 PKZIP 规范——aardio 里 minizip 的 `crypt.c` 逻辑即此）。
  只做**读侧**；写侧不加密（新软件不该再产出 ZipCrypto，注释写明）。
- 验收：用 Python `zipfile`（ZipCrypto）造测试包 → Zan 解密读出比对；
  错误口令报错不崩溃。

### 2.7 DES/3DES + RC4（遗留互操作）

- aardio 参考：`crypt/des.aardio`（CryptoAPI）、`crypt/rc4.aardio`。
- 落点：`stdlib/System/Security/Cryptography/{Des,Rc4}.zan`。
- 纯算法实现（DES 轮函数/RC4 KSA+PRGA），不调 CryptoAPI（仅 Windows）。
  类注释开头写明：**仅用于遗留系统互通，新代码用 Aes**。
- 验收：DES/3DES 用 NIST 已知向量；RC4 用 RFC 6229 向量。

### 2.8 X.509 证书只读解析

- aardio 参考：`crypt/cert.aardio`（666 行：PEM/DER 加载 + 字段读取）。
- 落点：`stdlib/System/Security/Cryptography/X509.zan`。
- 复用：`RsaKey.zan` 已有 PEM 剥壳 + DER TLV 读取层（`PemToDer`/DER
  INTEGER），把这两个 helper 提为可共享（internal 同 stdlib 内直接调）。
- 范围：只读解析——subject/issuer RDN、validity、serial、SAN、指纹
  （Sha1/Sha256 已有）；**不做**证书链校验与签名验证（后置单独立项）。
- 验收：自签测试证书（openssl 生成入库 `tests/data/`）字段断言。

### 2.9 NamedPipe IPC

- aardio 参考：`fsys/namedPipe.aardio`（服务端）+ `fsys/stream`。
- 前置探针（半天）：runtime 目前无 `CreateNamedPipe`/`mkfifo` 调用
  （grep 证实）。Windows 探针经 DllImport `kernel32` 三函数即可纯 Zan
  实现（`MemoryMappedFile` 同款套路）；POSIX `mkfifo` 经 `[DllImport("crt")]`
  + 普通 File 读写。探针过了就不用动 `src/runtime`——**优先纯 Zan 方案**。
- 落点：`stdlib/System/IO/Pipes/NamedPipeServer.zan` +
  `NamedPipeClient.zan`；Windows 名字 `\\.\pipe\<name>`，POSIX
  `/tmp/<name>.fifo` 由类内统一拼，调用方只给逻辑名。
- 验收：父子进程 echo smoke（Windows + Linux 各跑）。

### 2.10 `System.Time.SunTimes`

- aardio 参考：`time/sun.aardio`（Delta T 数据表 + 节气时刻）+
  `time/julianDay.aardio`（儒略日，直接照搬公式）。
- 现状：`Lunar` 只覆盖"当天是哪个节气"（逐日表查表），无时刻计算。
- 落点：`stdlib/System/Time/SunTimes.zan`：`JulianDay`（fromGregorian）、
  `SolarTermMoment(int year, int n)`（第 n 个节气的精确时刻）、
  `Sunrise/Sunset(double lat, double lng, DateTime date)`。
  Delta T 表照搬 aardio（约 60 组系数）。
- 验收：与 NOAA/权威算法已知值比对（北京/赤道两点，容差 ±2 分钟）。

## 3. L 级（结构最大的一项）

### 3.1 `System.Xml`

- aardio 参考：`string/xml.aardio`（849 行：实体表 252 项 HTML + 5 项 XML、
  eachChild 遍历、queryEles 按标签/属性查询）。
- 落点：`stdlib/System/Xml/XmlDocument.zan` + `XmlNode.zan` + `XmlWriter.zan`。
- 范围（一期）：
  - 解析：DOM 树（元素/属性/文本/CDATA/注释/PI/自闭合）、实体反转义
    （5 个 XML 实体内建；HTML 252 实体表作独立静态数据，`HtmlDecode` 入口）；
  - 遍历/查询：`Children(tag)`、`Descendants(tag)`、`Attr(name)`——
    不做 XPath（后置）；
  - 写出：`XmlWriter` 转义规则与 `Xlsx.Esc` 对齐（含非法控制字符剥离）。
- 收敛动作（本项的独有收益）：`Sdk.Wechat.XmlUtil`、
  `WebDavClient.ParseMultiStatus`、`Xlsx` 的手写扫描迁到公共解析器上
  （各模块自己的注释都预留了这个后路）；`Gui`/`Android` 的 SVG 图标
  （`IconSvgData`）如读侧有手撕 XML 一并评估。
- 实现注意：Zan 禁 `Any`，节点 children 用 `List<XmlNode>`，属性用
  `List<XmlAttribute>`（插入序，与 Dict 语义差异写注释）。
- 验收：aardio `xml` 库的实体表抽查 + 畸形输入负例 + Wechat 报文
  （CDATA）真实样本 round-trip。

## 4. 示例补齐（#23，随各库走，不单独立工程）

- 模式：照 `examples/input`（README + 多个 demo）与 `examples/net/http_client`
  （自带测试服务端）。
- 优先补：`examples/net/`（ftp_client、mdns_discover、wol、stun）、
  `examples/text/`（chinese_number、patch、cmdline、tfidf）、
  `examples/system/`（clipboard、registry、process_list、screen——
  第一二期已迁但零示例的库）。
- aardio 示例只取场景与断言数据（见
  `aardio-capability-migration.md` §6.4），形态按 Zan 惯例。

## 5. 排期建议（依赖驱动，非严格串行）

1. **第 1 批（纯算法冲量，互不依赖）**：§1.1~§1.7、§1.10、§1.11 十项
   S 级 + §3.1 System.Xml 开工（L 级提前起跑）。
2. **第 2 批（探针先行）**：§2.3 组播探针、§2.4 iconv 探针、§2.9 fifo
   探针——三个探针同一两天内做完，结论回填本文档。
3. **第 3 批**：探针通过后的 M 级（§2.1~§2.10），每项独立提交。
4. **第 4 批**：System.Xml 落地后的三处收敛（Wechat/WebDav/Xlsx）
   + 示例集中补齐。
5. 每批收尾跑 `scripts\test.ps1 standard`（stdlib 变更的提交门禁，
   见 AGENTS.md 规则 8）。
