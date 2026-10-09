# System.Runtime.Intrinsics.X86

> 源码: `stdlib/System/Runtime/Intrinsics/X86/Aes.zan`, `stdlib/System/Runtime/Intrinsics/X86/Sse2.zan`, `stdlib/System/Runtime/Intrinsics/X86/Sse42.zan`


## Aes (class)

- public static bool IsSupported { get }

- [DllImport("crt")]public static extern Vector128 Encrypt(Vector128 value, Vector128 roundKey);

- [DllImport("crt")]public static extern Vector128 EncryptLast(Vector128 value, Vector128 roundKey);

- [DllImport("crt")]public static extern Vector128 Decrypt(Vector128 value, Vector128 roundKey);

- [DllImport("crt")]public static extern Vector128 DecryptLast(Vector128 value, Vector128 roundKey);

- [DllImport("crt")]public static extern Vector128 KeygenAssist(Vector128 value, byte rcon);

- [DllImport("crt")]public static extern Vector128 InverseMixColumns(Vector128 value);


## Sse2 (class)

- public static bool IsSupported { get }

- public static extern Vector128 Xor(Vector128 left, Vector128 right);

- public static extern Vector128 And(Vector128 left, Vector128 right);

- public static extern Vector128 Or(Vector128 left, Vector128 right);

- public static extern Vector128 CompareEqual(Vector128 left, Vector128 right);

- public static extern int MoveMask(Vector128 value);

- public static extern Vector128 LoadVector128(nint address);

- public static extern void Store(nint address, Vector128 source);


## Sse42 (class)

- public static bool IsSupported { get }

- public static extern uint Crc32(uint crc, byte data);

- public static extern uint Crc32(uint crc, ushort data);

- public static extern uint Crc32(uint crc, uint data);

- public static extern ulong Crc32(ulong crc, ulong data);
