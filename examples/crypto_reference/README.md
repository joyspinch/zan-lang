# crypto_reference — 纯 Zan 参考实现（教学用）

本目录保存 AES、AES-GCM、SM4 的**纯 Zan 逐位实现**。它们不参与编译期
驱动解析，也不被 stdlib 引用——stdlib 里的同名类自 Zan 2.7 起走 OpenSSL
libcrypto 的 EVP 接口（AES-NI/PCLMULQDQ 硬件加速），本目录的实现仅作
为算法教学与核对基准存在。

## 文件

| 文件 | 内容 | 对应 stdlib |
|------|------|-------------|
| `AesReference.zan`    | AES 分组密码（S-box/逆 S-box、shiftRows、mixColumns、GF(2^8) 乘法），CBC/ECB/CTR | `System.Security.Cryptography.Aes` |
| `AesGcmReference.zan` | AES-GCM（GHASH、GF(2^128) 右移乘法、J0=IV\|\|0^31\|\|1，96 位 IV） | `System.Security.Cryptography.AesGcm` |
| `Sm4Reference.zan`    | SM4（GB/T 32907-2016，S 盒、τ 变换、L/L' 线性变换、32 轮），ECB/CBC | `System.Security.Cryptography.Sm4` |

> 这些文件是它们被移出 stdlib 时点的快照。命名空间用 `CryptoReference`
> （2026-10-01 起：`System.Security.Cryptography` 已随 Zan.Security 包
> 提供同名 Aes/AesGcm/Sm4，同框即撞重声明，快照改名空间以保持可编译；
> Hex/Bits 辅助类仍借包内实现，文件头保留对应 `using`）。要恢复纯 Zan
> 行为：拷回包对应路径、把命名空间改回 `System.Security.Cryptography`、
> 删掉对包内 Hex/Bits 的依赖（或一并带走），再把包版本的 EVP 实现
> 换掉（不推荐——性能差两个数量级）。

## 为什么 stdlib 换成了 EVP

纯 Zan 版性能（Windows x64 实测）：AES-CBC 加密约 **2 MiB/s**，
AES-GCM 单 token 约 **103 µs**——无 T 表、GF 乘法逐位循环、每轮分配
临时数组，全部是解释成本之外的真实 CPU 开销。同一台机器上 EVP 路径：
CBC **~196 MiB/s**（含 API 层拷贝；裸 EVP 1250+ MiB/s）、GCM token
**~3.6 µs**。安全性上 EVP 还带来 OpenSSL 的常数时间查表与侧信道加固。

## 使用差异（对照 stdlib 契约时注意）

- `AesGcm.Encrypt` 的 `tagOut`：参考实现直接写调用方缓冲（`byte[16]`
  即可）；stdlib EVP 版同样如此，但内部经 `byte[]` 载荷中转——
  因为 GET_TAG 是 C 层就地写，不能交给 Zan string（元素写会撞长度
  缓存，见 stdlib 源内注释）。
- `Sm4` 的单分组 ECB 在 EVP 上没有独立 cipher，stdlib 版用"零 IV +
  无填充 CBC"承载，语义等价（CBC 第一个块与 ECB 相同）。
- 两者对同一输入产出**逐字节相同**的密文与 tag（NIST/GB/T 向量均过），
  可以任意互换。
