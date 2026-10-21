# Ssh

`Ssh` 是 SSH 客户端层：RFC 4251 线格式编解码、RFC 4253 二进制包协议
（AES-GCM）、版本交换、算法协商、curve25519-sha256 密钥交换
（RFC 8731）、主机密钥指纹与 known_hosts 校验、口令 / 公钥认证、
以及 exec 通道。

它站在 `Zan.Security`（Curve25519 / SHA-256 / ECDSA / AES-GCM）与
`Zan.Net`（Socket / TcpClient）之上，不重复造轮子。

**已与真实第三方实现互通验证**（见下文「互通验证」）：不是"自己和自己
握手成功"，而是和 paramiko 的服务端跑通完整链路。

## 设计取舍

- **只做一条算法链，但做对**：curve25519-sha256 密钥交换 +
  ecdsa-sha2-nistp256 主机密钥 + aes256-gcm@openssh.com 加密。
  其余算法**明确报 `Unsupported`**，而不是"能协商但握手时才炸"。
  这是刻意的：SSH 的算法面很大，半吊子实现（协商得上、算不对哈希）
  比不支持更难排查。
- **抽象出 `SshBytePipe`**：握手是"客户端说一句、服务端答一句"的多轮
  交互。绑死 TCP 就只能在有 sshd 的机器上验证；抽象成字节管道之后，
  同一进程里就能跑两端握手（`SshMemoryLink`），任何一边算错哈希、
  编码错 mpint、切错加密时机都会立刻对不上。
- **主机密钥默认"不认识就拒绝"**（`acceptUnknownHostKey = false`）。
  把"第一次见"当成"可以接受"是 SSH 客户端最经典的漏洞——中间人只要
  抢在真服务器之前握手就能拿到会话密钥。known_hosts 匹配区分
  **Known / Mismatch / Unknown** 三态：把 Mismatch 当 Unknown 处理，
  等于把中间人攻击重新打开。
- **异常 + 错误码两个通道**：人看 `Message`，程序看 `error`
  （`SshError` 枚举，18 个取值）。"主机密钥变了"必须停下来问人，
  "认证被拒"可以换一种凭据重试，"没有共同算法"是版本问题只能换实现——
  这三种必须能被程序分开处理。
- **mpint 只有一个编码器**：`SshWriter.WriteMpint`。历史上这里有过
  第二份实现（只返回数值体、不带长度前缀），结果是密钥派生少算
  4 个字节、派生出来的密钥全不一样，症状是"握手成功、第一个包解不开"。
  同一个概念不允许存在两套编码。
- **并发安全**：单个 `SshClient` 实例**不是线程安全的**（收发状态是实例
  字段）。不同连接用不同实例。

## 能力

| 能力 | 入口 |
|---|---|
| 连接 | `SshClient(host, port, user)` → `Connect()` / `UsePipe(pipe)` |
| 握手 | `Handshake()`（版本交换 + 协商 + KEX + NEWKEYS 切换） |
| 主机密钥 | `SshHostKey.Fingerprint(blob)`、`SshKnownHosts.Parse(content).Check(host, blob)` |
| 认证 | `RequestUserAuthService()` → `AuthenticatePassword(pw)` / `AuthenticatePublicKey(priv)` |
| 执行命令 | `Exec(command)` → `SshExecResult{stdout, stderr, exitCode}` |
| 线格式 | `SshWriter` / `SshReader`（byte / bool / uint32 / uint64 / string / mpint / name-list） |
| 包协议 | `SshPacketLayer`（明文包与 AES-GCM 包） |

## 用法

```zan
using System;
using System.Ssh;

SshClient client = new SshClient("example.com", 22, "alice");
client.options.pinnedFingerprint = "SHA256:...";   // 或给 knownHostsContent

client.Connect();
client.Handshake();

client.RequestUserAuthService();
if (!client.AuthenticatePassword("s3cret")) {
    Console.WriteLine("认证失败");
    return;
}

SshExecResult result = client.Exec("uptime");
Console.WriteLine(result.stdout);
Console.WriteLine(result.exitCode.ToString());
client.Close();
```

### 主机密钥策略

默认**任何不认识的主机密钥都拒绝连接**。三种放行方式，按严格程度排：

```zan
// 1. 固定指纹（最严）：指纹对不上直接断，不给"第一次见"的余地。
client.options.pinnedFingerprint = "SHA256:...";

// 2. known_hosts：第一次见到会报 HostKeyUnknown，见过且一致才连。
client.options.knownHostsContent = File.ReadAllText("known_hosts");

// 3. 显式接受未知主机（仅限测试）：默认 false。
client.options.acceptUnknownHostKey = true;
```

known_hosts 支持通配符（`*.example.com`）与 `|1|salt|hash` 哈希主机名
（HMAC-SHA1）。

### 错误处理

```zan
try {
    client.Exec("whoami");
} catch (SshException ex) {
    if (ex.error == SshError.HostKeyMismatch) {
        // 主机密钥变了：停下来问人，不要重试。
    } else if (ex.error == SshError.AuthFailed) {
        // 凭据不对：换一种凭据重试。
    }
    Console.WriteLine(ex.ErrorName() + ": " + ex.Message);
}
```

## 互通验证

包内的判定器是**自对拍**的（同一进程里跑客户端与服务端两端），它能守住
内部一致性，但**守不住"两边都算错同一个东西"**。所以这个包另外做了
**真实互通验证**：

- 对端是 **paramiko 4.0** 的服务端（第三方实现，与本包无代码关系）。
- 链路：版本交换 → 算法协商 → curve25519 密钥交换 → 主机密钥验签 →
  AES-GCM 切换 → 口令认证 → 开通道 → exec → 取 stdout 与退出码。
- 跑法见 `tests/interop/ssh/README.md`（需要 `pip install paramiko`）。

互通验证查出了 5 个自对拍查不出来的真 bug（都已修掉并加了回归用例）：
明文包的填充对齐基准、GCM 的 IV 计数器初值、交换哈希里的 KEXINIT
**没算消息号**、密钥派生里的 mpint **少了长度前缀**、ECDSA 签名 blob
**少了一层 string**。

## 已知边界

- **没有 SFTP**：只有 exec 通道。SFTP 是另一个子系统（RFC 4253 §5 之上
  再叠一层文件协议），不在本包范围。
- **没有实现 agent 转发、没有端口转发、没有多通道并发**。
- **只支持一种主机密钥算法**（ecdsa-sha2-nistp256）。OpenSSH 默认的
  ed25519 / rsa 主机密钥不支持——协商阶段就会报 `Unsupported`。
- **公钥认证只做了最小路径**：用裸私钥做签名，不读 OpenSSH 私钥文件
  格式（`BEGIN OPENSSH PRIVATE KEY`），也不读 PuTTY 格式。
- **不支持重密钥（rekey）**：包序列号按 RFC 4253 §6.4 单调递增，
  但收到对端的重密钥请求时**没有实现重新协商**。长时间大流量会话会
  到 OpenSSH 的重密钥阈值（默认 1 GB / 2 小时）。
- **没有 zlib 压缩**：只协商 `none`。
- **没有 PTY / shell / 端口转发 / X11 转发**。
- **主机密钥签名只验 ECDSA**：`SshHostKey.Verify` 不支持 ssh-rsa /
  ssh-ed25519 的签名 blob。
- **不做 known_hosts 的自动写入**：本包只读不写，落盘由调用方决定。

## 契约测试

`tests/conformance/ssh_transport.zan` 覆盖：

- 线格式：byte / bool / uint32 / string / mpint（补前导零、零是零长度、
  前导零要剥掉、负数要拒）/ name-list（空名字要拒）。
- 版本串：格式、控制字符、`SSH-1.99-` 视作 2.0。
- 算法协商：按**客户端顺序**选，不是服务端顺序。
- 主机密钥：blob 解析、指纹（OpenSSH 口径）、签名验签、篡改必失败、
  **签名 blob 的结构**（RFC 5656 §3.1.2 的"string 套两个 mpint"）。
- **交换哈希的已知答案**（含 KEXINIT 消息号这条规则）。
- **密钥派生的已知答案**（含 mpint 长度前缀这条规则）。
- known_hosts：通配符、哈希主机名、Known / Mismatch / Unknown 三态。
- 端到端：内存管道 + 线程，完整跑一遍握手 → 认证 → exec，并检查
  stdout / stderr / 退出码；服务端按**真实实现的顺序**发消息
  （数据先于 `CHANNEL_SUCCESS`，且发完不关通道）。
- 主机密钥策略：指纹不匹配必须报 `HostKeyMismatch`。
