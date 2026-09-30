# System.Numerics

> 源码: `stdlib/System/Numerics/BitOperations.zan`


## BitOperations (class)

提供对现代 CPU 硬件位运算原语的直接访问。
由编译器 irgen 直接降低为 LLVM Intrinsics 及单周期机器指令
（POPCNT, LZCNT, TZCNT, ROL, ROR, BSWAP）。

- [DllImport("crt")]public static extern int PopCount(int value);
  - 统计 32 位整数二进制中 1 的个数（POPCNT）。

- [DllImport("crt")]public static extern int PopCount(long value);
  - 统计 64 位整数二进制中 1 的个数（POPCNT）。

- [DllImport("crt")]public static extern int LeadingZeroCount(int value);
  - 计算 32 位整数二进制前导零的个数（LZCNT / CLZ）。为 0 时返回 32。

- [DllImport("crt")]public static extern int LeadingZeroCount(long value);
  - 计算 64 位整数二进制前导零的个数（LZCNT / CLZ）。为 0 时返回 64。

- [DllImport("crt")]public static extern int TrailingZeroCount(int value);
  - 计算 32 位整数二进制尾随零的个数（TZCNT / CTZ）。为 0 时返回 32。

- [DllImport("crt")]public static extern int TrailingZeroCount(long value);
  - 计算 64 位整数二进制尾随零的个数（TZCNT / CTZ）。为 0 时返回 64。

- [DllImport("crt")]public static extern int RotateLeft(int value, int offset);
  - 32 位整数循环左移（ROL）。

- [DllImport("crt")]public static extern long RotateLeft(long value, int offset);
  - 64 位整数循环左移（ROL）。

- [DllImport("crt")]public static extern int RotateRight(int value, int offset);
  - 32 位整数循环右移（ROR）。

- [DllImport("crt")]public static extern long RotateRight(long value, int offset);
  - 64 位整数循环右移（ROR）。

- [DllImport("crt")]public static extern int ReverseEndianness(int value);
  - 颠倒 32 位整数的字节顺序（大小端转换，BSWAP）。

- [DllImport("crt")]public static extern long ReverseEndianness(long value);
  - 颠倒 64 位整数的字节顺序（大小端转换，BSWAP）。

- [DllImport("crt")]public static extern int Log2(int value);
  - 计算以 2 为底的整数对数（向下取整）。小于等于 0 时返回 0。

- [DllImport("crt")]public static extern int Log2(long value);
  - 计算以 2 为底的 64 位整数对数（向下取整）。小于等于 0 时返回 0。

- [DllImport("crt")]public static extern bool IsPow2(int value);
  - 判断整数是否为 2 的整数次幂。

- [DllImport("crt")]public static extern bool IsPow2(long value);
  - 判断 64 位整数是否为 2 的整数次幂。

- [DllImport("crt")]public static extern int RoundUpToPowerOf2(int value);
  - 将整数向上舍入到最接近的 2 的整数次幂。

- [DllImport("crt")]public static extern long RoundUpToPowerOf2(long value);
  - 将 64 位整数向上舍入到最接近的 2 的整数次幂。

- public static int ResetLowestSetBit(int value)
  - 清零最低置位位（BLSR 单周期硬件位操作，value & (value - 1)）。

- public static long ResetLowestSetBit(long value)
  - 清零 64 位整数最低置位位（BLSR 单周期硬件位操作，value & (value - 1)）。

- public static int ExtractLowestSetBit(int value)
  - 提取最低置位位（BLSI 单周期硬件位操作，value & (-value)）。

- public static long ExtractLowestSetBit(long value)
  - 提取 64 位整数最低置位位（BLSI 单周期硬件位操作，value & (-value)）。
