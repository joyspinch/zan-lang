# System.Runtime.Intrinsics

> 源码: `stdlib/System/Runtime/Intrinsics/Cpu.zan`, `stdlib/System/Runtime/Intrinsics/Vector128.zan`, `stdlib/System/Runtime/Intrinsics/Vector256.zan`


## Cpu (class)

提供跨平台的 CPU 硬件特性与指令集运行时探测能力。
在 x86-64 架构下通过 CPUID 指令查询，在 ARM64 架构下查询硬件体系能力。

- [DllImport("crt", EntryPoint="zan_cpu_feature")]public static extern int CpuFeature(int id);
  - 查询底层硬件特性标记（由编译器合成单周期内联探测）。
    1: HasPopcnt, 2: HasLzcnt, 3: HasSse42, 4: HasAvx2, 5: HasAesNi, 6: HasNeon

- public static bool HasPopcnt { get }
  - 是否支持硬件 POPCNT 指令。

- public static bool HasLzcnt { get }
  - 是否支持硬件 LZCNT 指令。

- public static bool HasSse42 { get }
  - 是否支持 SSE 4.2 指令集（含硬件 CRC32、POPCNT 等）。

- public static bool HasAvx2 { get }
  - 是否支持 AVX2 256 位高级向量扩展。

- public static bool HasAesNi { get }
  - 是否支持 AES-NI 专用硬件加解密指令集。

- public static bool HasNeon { get }
  - 是否支持 ARM NEON 向量执行单元。


## Vector128 (struct)

128 位硬件向量结构（对应 x86 XMM 寄存器 / ARM NEON 128 位寄存器）。
包含 16 字节原始连续存储，由编译器硬件内建指令直接操纵。

- public long Low;

- public long High;

- public static Vector128 Create(long low, long high)

- public static Vector128 Zero { get }

- public static Vector128 AllBitsSet { get }

- public static extern Vector128 Create(byte value);
  - 广播 8 位无符号整数填满 128 位向量的 16 个字节槽位。

- public static extern Vector128 Create(int value);
  - 广播 32 位有符号整数填满 128 位向量的 4 个整型槽位。

- public static extern Vector128 Create(long value);
  - 广播 64 位有符号长整数填满 128 位向量的 2 个槽位。

- public static extern Vector128 Create(float value);
  - 广播 32 位浮点数填满 128 位向量的 4 个单精度浮点槽位。

- public static extern Vector128 Create(float e0, float e1, float e2, float e3);
  - 由 4 个单精度浮点数直接构造 128 位向量（XMM 寄存器）。

- public static extern Vector128 Load(nint address);
  - 从原始内存地址非对齐加载 16 字节 128 位向量。单周期 movdqu 指令。

- public static extern Vector128 Load(byte[]source);
  - 从字节数组起始位置非对齐加载 16 字节 128 位向量。

- public static extern Vector128 Load(byte[]source, int offset);
  - 从字节数组指定偏移处非对齐加载 16 字节 128 位向量。

- public static extern Vector128 Load(float[]source, int offset);
  - 从单精度浮点数组指定元素下标处加载 4 个 float（16 字节 128 位向量）。单周期 movups/vmovups 指令。

- public static extern void Store(nint address, Vector128 source);
  - 将 128 位向量非对齐写入原始内存地址。单周期 movdqu 指令。

- public static extern void Store(byte[]dest, Vector128 source);
  - 将 128 位向量非对齐写入字节数组起始位置。

- public static extern void Store(byte[]dest, int offset, Vector128 source);
  - 将 128 位向量非对齐写入字节数组指定偏移处。

- public static extern void Store(float[]dest, int offset, Vector128 source);
  - 将 128 位向量写入单精度浮点数组指定元素下标处。单周期 movups/vmovups 指令。

- public static extern Vector128 And(Vector128 left, Vector128 right);
  - 按位与运算（pand）。

- public static extern Vector128 Or(Vector128 left, Vector128 right);
  - 按位或运算（por）。

- public static extern Vector128 Xor(Vector128 left, Vector128 right);
  - 按位异或运算（pxor）。

- public static extern Vector128 AndNot(Vector128 left, Vector128 right);
  - 按位与非运算（pandn: (~left) & right）。

- public static extern Vector128 Equals(Vector128 left, Vector128 right);
  - 16 字节逐字节相等比较（pcmpeqb），相等字节置 0xFF，不相等置 0x00。

- public static extern int ExtractMostSignificantBits(Vector128 value);
  - 提取 16 个字节的最高有效位组合成 16 位整数掩码（pmovmskb）。

- public static extern Vector128 Shuffle(Vector128 value, Vector128 mask);
  - 字节级查表重排（pshufb，SSSE3 指令）。

- public static extern Vector128 Add(Vector128 left, Vector128 right);
  - 16 字节逐字节普通加法（paddb）。

- public static extern Vector128 Subtract(Vector128 left, Vector128 right);
  - 16 字节逐字节普通减法（psubb）。

- public static extern Vector128 AddSaturate(Vector128 left, Vector128 right);
  - 16 字节无符号饱和加法（paddusb），结果截断限制在 [0, 255]，常用于图像调光与 Alpha 混合。

- public static extern Vector128 SubtractSaturate(Vector128 left, Vector128 right);
  - 16 字节无符号饱和减法（psubusb），结果截断限制在 [0, 255]，减至负数自动截断为 0。

- public static extern Vector128 Min(Vector128 left, Vector128 right);
  - 16 字节无符号最小值（pminub）。

- public static extern Vector128 Max(Vector128 left, Vector128 right);
  - 16 字节无符号最大值（pmaxub）。

- public static extern Vector128 Average(Vector128 left, Vector128 right);
  - 16 字节无符号平均值（pavgb: (a + b + 1) >> 1），常用于双线性插值与图像半影下采样。

- public static extern Vector128 ConditionalSelect(Vector128 condition, Vector128 left, Vector128 right);
  - 16 字节基于掩码的无分支选择（ConditionalSelect / Blend: (condition & left) | (~condition & right)）。

- public static extern Vector128 UnpackLow(Vector128 left, Vector128 right);
  - 解包并交错两个向量的低 8 字节（punpcklbw）。

- public static extern Vector128 UnpackHigh(Vector128 left, Vector128 right);
  - 解包并交错两个向量的高 8 字节（punpckhbw）。

- public static extern Vector128 GreaterThan(Vector128 left, Vector128 right);
  - 有符号大于比较（pcmpgtb），left > right 的字节置 0xFF，否则置 0x00。

- public static extern Vector128 LessThan(Vector128 left, Vector128 right);
  - 有符号小于比较（pcmpgtb 反转），left < right 的字节置 0xFF，否则置 0x00。

- public static extern void Prefetch(nint address);
  - 预取指定内存地址至 CPU 缓存（_mm_prefetch）。

- public static extern void Prefetch(byte[]source, int offset);
  - 预取指定字节数组偏移处的数据至 CPU 缓存。

- public static extern Vector128 Multiply(Vector128 left, Vector128 right);
  - 4 维单精度浮点向量逐分量相乘（mulps）。

- public static extern Vector128 AddFloat(Vector128 left, Vector128 right);
  - 4 维单精度浮点向量逐分量加法（addps）。

- public static extern Vector128 SubtractFloat(Vector128 left, Vector128 right);
  - 4 维单精度浮点向量逐分量减法（subps）。

- public static extern Vector128 MultiplyAdd(Vector128 a, Vector128 b, Vector128 c);
  - 单周期 FMA 融和乘加运算：计算 (a * b) + c，中间无二次精度舍入。

- public static extern Vector128 Sqrt(Vector128 value);
  - 4 维单精度浮点向量逐分量平方根（sqrtps）。

- public static extern Vector128 ReciprocalSqrt(Vector128 value);
  - 4 维单精度浮点向量快速平方根倒数（1.0 / sqrt(v)，rsqrtps）。用于 3D 向量快速归一化。


## Vector256 (struct)

256 位硬件向量结构（对应 x86 AVX2 YMM 寄存器）。
包含 32 字节连续物理存储，由编译器硬件内建指令直接操纵。

- public long V0;

- public long V1;

- public long V2;

- public long V3;

- public static Vector256 Create(long v0, long v1, long v2, long v3)

- public static Vector256 Zero { get }

- public static Vector256 AllBitsSet { get }

- public static extern Vector256 Create(byte value);
  - 广播 8 位无符号整数填满 256 位向量的 32 个字节槽位。

- public static extern Vector256 Create(int value);
  - 广播 32 位有符号整数填满 256 位向量的 8 个整型槽位。

- public static extern Vector256 Create(long value);
  - 广播 64 位有符号长整数填满 256 位向量的 4 个长整型槽位。

- public static extern Vector256 Load(nint address);
  - 从原始内存地址非对齐加载 32 字节 256 位向量（vmovdqu）。

- public static extern Vector256 Load(byte[]source);
  - 从字节数组起始位置非对齐加载 32 字节 256 位向量。

- public static extern Vector256 Load(byte[]source, int offset);
  - 从字节数组指定偏移处非对齐加载 32 字节 256 位向量。

- public static extern Vector256 Load(float[]source, int offset);
  - 从单精度浮点数组指定元素下标处加载 8 个 float（32 字节 256 位向量）。单周期 vmovups 指令。

- public static extern void Store(nint address, Vector256 source);
  - 将 256 位向量非对齐写入原始内存地址（vmovdqu）。

- public static extern void Store(byte[]dest, Vector256 source);
  - 将 256 位向量非对齐写入字节数组起始位置。

- public static extern void Store(byte[]dest, int offset, Vector256 source);
  - 将 256 位向量非对齐写入字节数组指定偏移处。

- public static extern void Store(float[]dest, int offset, Vector256 source);
  - 将 256 位向量写入单精度浮点数组指定元素下标处。单周期 vmovups 指令。

- public static extern Vector256 And(Vector256 left, Vector256 right);
  - 按位与运算（vpand）。

- public static extern Vector256 Or(Vector256 left, Vector256 right);
  - 按位或运算（vpor）。

- public static extern Vector256 Xor(Vector256 left, Vector256 right);
  - 按位异或运算（vpxor）。

- public static extern Vector256 AndNot(Vector256 left, Vector256 right);
  - 按位与非运算（vpandn）。

- public static extern Vector256 Equals(Vector256 left, Vector256 right);
  - 32 字节逐字节相等比较（vpcmpeqb），相等字节置 0xFF，不相等置 0x00。

- public static extern int ExtractMostSignificantBits(Vector256 value);
  - 提取 32 个字节的最高有效位组合成 32 位整数掩码（vpmovmskb）。

- public static extern Vector256 Add(Vector256 left, Vector256 right);
  - 32 字节逐字节并行加法（vpaddb）。

- public static extern Vector256 Subtract(Vector256 left, Vector256 right);
  - 32 字节逐字节并行减法（vpsubb）。

- public static extern Vector256 AddSaturate(Vector256 left, Vector256 right);
  - 32 字节逐字节无符号饱和加法（vpaddusb）。用于 32 像素图像调光/混色。

- public static extern Vector256 SubtractSaturate(Vector256 left, Vector256 right);
  - 32 字节逐字节无符号饱和减法（vpsubusb）。用于 32 像素图像对比度截断。

- public static extern Vector256 Min(Vector256 left, Vector256 right);
  - 32 字节逐字节无符号最小值（vpminub）。

- public static extern Vector256 Max(Vector256 left, Vector256 right);
  - 32 字节逐字节无符号最大值（vpmaxub）。

- public static extern Vector256 Average(Vector256 left, Vector256 right);
  - 32 字节逐字节无符号均值计算（vpavgb，(a + b + 1) >> 1）。

- public static extern Vector256 ConditionalSelect(Vector256 condition, Vector256 left, Vector256 right);
  - 32 字节无分支条件向量选择（vpblendvb）：(condition & left) | (~condition & right)。

- public static extern void Prefetch(nint address);
  - 预取目标原始内存地址的数据到 CPU 高速缓存（PREFETCHT0）。

- public static extern void Prefetch(byte[]source, int offset);
  - 预取字节数组指定偏移处的数据到 CPU 高速缓存（PREFETCHT0）。
