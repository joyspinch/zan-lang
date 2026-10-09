# System.Numerics

> 源码: `stdlib/System/Numerics/BitOperations.zan`


## BitOperations (class)

- [DllImport("crt")]public static extern int PopCount(int value);

- [DllImport("crt")]public static extern int PopCount(long value);

- [DllImport("crt")]public static extern int LeadingZeroCount(int value);

- [DllImport("crt")]public static extern int LeadingZeroCount(long value);

- [DllImport("crt")]public static extern int TrailingZeroCount(int value);

- [DllImport("crt")]public static extern int TrailingZeroCount(long value);

- [DllImport("crt")]public static extern int RotateLeft(int value, int offset);

- [DllImport("crt")]public static extern long RotateLeft(long value, int offset);

- [DllImport("crt")]public static extern int RotateRight(int value, int offset);

- [DllImport("crt")]public static extern long RotateRight(long value, int offset);

- [DllImport("crt")]public static extern int ReverseEndianness(int value);

- [DllImport("crt")]public static extern long ReverseEndianness(long value);

- [DllImport("crt")]public static extern int Log2(int value);

- [DllImport("crt")]public static extern int Log2(long value);

- [DllImport("crt")]public static extern bool IsPow2(int value);

- [DllImport("crt")]public static extern bool IsPow2(long value);

- [DllImport("crt")]public static extern int RoundUpToPowerOf2(int value);

- [DllImport("crt")]public static extern long RoundUpToPowerOf2(long value);

- public static int ResetLowestSetBit(int value)

- public static long ResetLowestSetBit(long value)

- public static int ExtractLowestSetBit(int value)

- public static long ExtractLowestSetBit(long value)
