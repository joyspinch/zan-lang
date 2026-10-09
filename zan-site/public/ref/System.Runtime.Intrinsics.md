# System.Runtime.Intrinsics

> 源码: `stdlib/System/Runtime/Intrinsics/Cpu.zan`, `stdlib/System/Runtime/Intrinsics/Vector128.zan`, `stdlib/System/Runtime/Intrinsics/Vector256.zan`


## Cpu (class)

- [DllImport("crt", EntryPoint="zan_cpu_feature")]public static extern int CpuFeature(int id);

- public static bool HasPopcnt { get }

- public static bool HasLzcnt { get }

- public static bool HasSse42 { get }

- public static bool HasAvx2 { get }

- public static bool HasAesNi { get }

- public static bool HasNeon { get }


## Vector128 (struct)

- public long Low;

- public long High;

- public static Vector128 Create(long low, long high)

- public static Vector128 Zero { get }

- public static Vector128 AllBitsSet { get }

- public static extern Vector128 Create(byte value);

- public static extern Vector128 Create(int value);

- public static extern Vector128 Create(long value);

- public static extern Vector128 Create(float value);

- public static extern Vector128 Create(float e0, float e1, float e2, float e3);

- public static extern Vector128 Load(nint address);

- public static extern Vector128 Load(byte[]source);

- public static extern Vector128 Load(byte[]source, int offset);

- public static extern Vector128 Load(float[]source, int offset);

- public static extern void Store(nint address, Vector128 source);

- public static extern void Store(byte[]dest, Vector128 source);

- public static extern void Store(byte[]dest, int offset, Vector128 source);

- public static extern void Store(float[]dest, int offset, Vector128 source);

- public static extern Vector128 And(Vector128 left, Vector128 right);

- public static extern Vector128 Or(Vector128 left, Vector128 right);

- public static extern Vector128 Xor(Vector128 left, Vector128 right);

- public static extern Vector128 AndNot(Vector128 left, Vector128 right);

- public static extern Vector128 Equals(Vector128 left, Vector128 right);

- public static extern int ExtractMostSignificantBits(Vector128 value);

- public static extern Vector128 Shuffle(Vector128 value, Vector128 mask);

- public static extern Vector128 Add(Vector128 left, Vector128 right);

- public static extern Vector128 Subtract(Vector128 left, Vector128 right);

- public static extern Vector128 AddSaturate(Vector128 left, Vector128 right);

- public static extern Vector128 SubtractSaturate(Vector128 left, Vector128 right);

- public static extern Vector128 Min(Vector128 left, Vector128 right);

- public static extern Vector128 Max(Vector128 left, Vector128 right);

- public static extern Vector128 Average(Vector128 left, Vector128 right);

- public static extern Vector128 ConditionalSelect(Vector128 condition, Vector128 left, Vector128 right);

- public static extern Vector128 UnpackLow(Vector128 left, Vector128 right);

- public static extern Vector128 UnpackHigh(Vector128 left, Vector128 right);

- public static extern Vector128 GreaterThan(Vector128 left, Vector128 right);

- public static extern Vector128 LessThan(Vector128 left, Vector128 right);

- public static extern void Prefetch(nint address);

- public static extern void Prefetch(byte[]source, int offset);

- public static extern Vector128 Multiply(Vector128 left, Vector128 right);

- public static extern Vector128 AddFloat(Vector128 left, Vector128 right);

- public static extern Vector128 SubtractFloat(Vector128 left, Vector128 right);

- public static extern Vector128 MultiplyAdd(Vector128 a, Vector128 b, Vector128 c);

- public static extern Vector128 Sqrt(Vector128 value);

- public static extern Vector128 ReciprocalSqrt(Vector128 value);


## Vector256 (struct)

- public long V0;

- public long V1;

- public long V2;

- public long V3;

- public static Vector256 Create(long v0, long v1, long v2, long v3)

- public static Vector256 Zero { get }

- public static Vector256 AllBitsSet { get }

- public static extern Vector256 Create(byte value);

- public static extern Vector256 Create(int value);

- public static extern Vector256 Create(long value);

- public static extern Vector256 Load(nint address);

- public static extern Vector256 Load(byte[]source);

- public static extern Vector256 Load(byte[]source, int offset);

- public static extern Vector256 Load(float[]source, int offset);

- public static extern void Store(nint address, Vector256 source);

- public static extern void Store(byte[]dest, Vector256 source);

- public static extern void Store(byte[]dest, int offset, Vector256 source);

- public static extern void Store(float[]dest, int offset, Vector256 source);

- public static extern Vector256 And(Vector256 left, Vector256 right);

- public static extern Vector256 Or(Vector256 left, Vector256 right);

- public static extern Vector256 Xor(Vector256 left, Vector256 right);

- public static extern Vector256 AndNot(Vector256 left, Vector256 right);

- public static extern Vector256 Equals(Vector256 left, Vector256 right);

- public static extern int ExtractMostSignificantBits(Vector256 value);

- public static extern Vector256 Add(Vector256 left, Vector256 right);

- public static extern Vector256 Subtract(Vector256 left, Vector256 right);

- public static extern Vector256 AddSaturate(Vector256 left, Vector256 right);

- public static extern Vector256 SubtractSaturate(Vector256 left, Vector256 right);

- public static extern Vector256 Min(Vector256 left, Vector256 right);

- public static extern Vector256 Max(Vector256 left, Vector256 right);

- public static extern Vector256 Average(Vector256 left, Vector256 right);

- public static extern Vector256 ConditionalSelect(Vector256 condition, Vector256 left, Vector256 right);

- public static extern void Prefetch(nint address);

- public static extern void Prefetch(byte[]source, int offset);
