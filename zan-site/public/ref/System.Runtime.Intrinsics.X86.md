# System.Runtime.Intrinsics.X86

> 源码: `stdlib/System/Runtime/Intrinsics/X86/Aes.zan`, `stdlib/System/Runtime/Intrinsics/X86/Sse2.zan`, `stdlib/System/Runtime/Intrinsics/X86/Sse42.zan`


## Aes (class)

提供 Intel/AMD x86-64 硬件 AES-NI 密码学加速原语。
单条指令在 CPU 专用物理执行单元中单周期完成整轮加密、解密与密钥扩展。

- public static bool IsSupported { get }
  - 当前 CPU 是否物理支持 AES-NI 指令集。

- [DllImport("crt")]public static extern Vector128 Encrypt(Vector128 value, Vector128 roundKey);
  - 执行单轮 AES 加密（SubBytes + ShiftRows + MixColumns + AddRoundKey）。生成单周期 aesenc 指令。

- [DllImport("crt")]public static extern Vector128 EncryptLast(Vector128 value, Vector128 roundKey);
  - 执行最后一轮 AES 加密（SubBytes + ShiftRows + AddRoundKey，不含 MixColumns）。生成单周期 aesenclast 指令。

- [DllImport("crt")]public static extern Vector128 Decrypt(Vector128 value, Vector128 roundKey);
  - 执行单轮 AES 解密（InvSubBytes + InvShiftRows + InvMixColumns + AddRoundKey）。生成单周期 aesdec 指令。

- [DllImport("crt")]public static extern Vector128 DecryptLast(Vector128 value, Vector128 roundKey);
  - 执行最后一轮 AES 解密（InvSubBytes + InvShiftRows + AddRoundKey，不含 InvMixColumns）。生成单周期 aesdeclast 指令。

- [DllImport("crt")]public static extern Vector128 KeygenAssist(Vector128 value, byte rcon);
  - 生成用于 AES 密钥扩展的辅助轮密钥。生成单周期 keygenassist 指令。

- [DllImport("crt")]public static extern Vector128 InverseMixColumns(Vector128 value);
  - 对输入向量执行逆列混合变换（InvMixColumns）。生成单周期 aesimc 指令。


## Sse2 (class)

提供 x86-64 SSE2 128 位硬件向量指令（加载、存储与向量位异或）。

- public static bool IsSupported { get }
  - 当前 CPU 是否支持 SSE2 指令集。

- public static extern Vector128 Xor(Vector128 left, Vector128 right);
  - 对两个 128 位硬件向量执行位异或操作。生成单周期 pxor 指令。

- public static extern Vector128 And(Vector128 left, Vector128 right);
  - 对两个 128 位硬件向量执行位与操作。生成单周期 pand 指令。

- public static extern Vector128 Or(Vector128 left, Vector128 right);
  - 对两个 128 位硬件向量执行位或操作。生成单周期 por 指令。

- public static extern Vector128 CompareEqual(Vector128 left, Vector128 right);
  - 对两个 128 位硬件向量执行逐字节相等比较。生成单周期 pcmpeqb 指令。

- public static extern int MoveMask(Vector128 value);
  - 提取 128 位向量中每个字节的最高有效位组合为 16 位整数掩码。生成单周期 pmovmskb 指令。

- public static extern Vector128 LoadVector128(nint address);
  - 从原始内存地址非对齐加载 128 位向量。生成单周期 movdqu 指令。

- public static extern void Store(nint address, Vector128 source);
  - 将 128 位向量非对齐写入原始内存地址。生成单周期 movdqu 指令。


## Sse42 (class)

提供 x86-64 SSE4.2 硬件指令集支持（硬件级 CRC32 校验累积计算）。

- public static bool IsSupported { get }
  - 当前 CPU 是否支持 SSE4.2 指令集。

- public static extern uint Crc32(uint crc, byte data);
  - 使用硬件 CRC32 指令累积 8 位无符号字节（crc32b）。

- public static extern uint Crc32(uint crc, ushort data);
  - 使用硬件 CRC32 指令累积 16 位无符号整数（crc32w）。

- public static extern uint Crc32(uint crc, uint data);
  - 使用硬件 CRC32 指令累积 32 位无符号整数（crc32l）。

- public static extern ulong Crc32(ulong crc, ulong data);
  - 使用硬件 CRC32 指令累积 64 位无符号长整数（crc32q）。
