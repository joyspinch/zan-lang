# SSH 互通验证（paramiko 对端）

这条用例**不是 ctest 的一部分**：它需要第三方 Python 包 `paramiko`，
装不上就跑不了。它存在的理由是——包内的判定器
（`tests/conformance/ssh_transport.zan`）是**自对拍**的：同一个包里的
客户端与服务端对着跑，两边**同时算错同一个东西**时照样全绿。

互通验证查出来、自对拍查不出来的真 bug（都已修掉并加了回归用例）：

| 坑 | 症状 |
|---|---|
| 明文包的填充对齐基准搞错（差 4 字节） | 对端直接拒绝整条流，"握手毫无理由地卡住" |
| GCM 的 IV 计数器初值用了 0（应为派生 IV 的后 8 字节） | 第一个加密包解不开 |
| 交换哈希里的 KEXINIT **没算消息号** | 主机密钥验签失败 |
| 密钥派生里的 mpint **少了 4 字节长度前缀** | 握手成功、第一个包解不开 |
| ECDSA 签名 blob **少了一层 string** | 验签失败（报错误导） |

## 前置

```bash
pip install paramiko
```

## 跑法

三个终端，或者一条命令里串起来：

```bash
# 终端 1：起对端（会打印主机密钥指纹）
python tests/interop/ssh/paramiko_server.py 22222

# 终端 2：编探针
build/zanc tests/interop/ssh/client_probe.zan -o _scratch/ssh_interop.exe --package-project .

# 终端 3：跑（把终端 1 打印的指纹填进去）
_scratch/ssh_interop.exe 127.0.0.1 22222 "SHA256:..."
```

期望输出：

```
kex=curve25519-sha256@libssh.org
hostkey=ecdsa-sha2-nistp256
cipher=aes256-gcm@openssh.com
INTEROP OK
```

## 一条命令的版本（PowerShell）

```powershell
$repo = "D:\project\zan-lang"
$job = Start-Process python -ArgumentList "$repo\tests\interop\ssh\paramiko_server.py","22222" `
    -RedirectStandardOutput "$repo\_scratch\pkserver.out" -NoNewWindow -PassThru
Start-Sleep -Seconds 3
$fp = (Get-Content "$repo\_scratch\pkserver.out" | Where-Object { $_ -like 'FINGERPRINT*' }).Split(' ')[1]
& "$repo\_scratch\ssh_interop.exe" "127.0.0.1" "22222" "$fp"
Stop-Process -Id $job.Id -Force
```

## 排查

设 `PK_DEBUG=1` 环境变量，paramiko 会把包级日志写到
`tests/interop/ssh/pk_debug.log`（含每条消息的类型与长度）——互通卡住
时先看这个。
