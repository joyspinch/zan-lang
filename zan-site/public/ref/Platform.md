# Platform

> 源码: `packages/Zan.Platform.Compat/src/Platform/Runtime.zan`


## Runtime (class)

- public static string GetPlatform()

- public static bool IsWindows()

- public static bool IsLinux()

- public static bool IsMacOS()

- public static bool IsWasi()

- public static bool IsMusl()

- public static bool IsRiscv64()

- public static bool IsWasm32()

- public static int GetProcessId()

- public static int Pid=> Environment.ProcessId;

- public static string OS=> OperatingSystem.Platform;

- public static string ExePath=> Environment.ProcessPath;

- public static long TickCountMs=> Environment.TickCount64;

- public static string PathSeparator()

- public static string NewLine()
