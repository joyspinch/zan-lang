# Platform

> 源码: `packages/Zan.Platform.Compat/src/Platform/Runtime.zan`


## Runtime (class)

运行时平台检测与信息（历史兼容层）。
建议新代码直接使用符合 C# 标准的 `System.OperatingSystem` 和 `System.Environment`。
所有查询均解析为编译时选定的目标平台，交叉编译程序报告目标系统。

- public static string GetPlatform()
  - 返回当前平台名称（"windows" | "linux" | "macos" | "wasi"）。

- public static bool IsWindows()
  - 在 Windows 上运行时返回 true。

- public static bool IsLinux()
  - 在 Linux 上运行时返回 true。

- public static bool IsMacOS()
  - 在 macOS 上运行时返回 true。

- public static bool IsWasi()
  - 在 WASI 上运行时返回 true。

- public static bool IsMusl()
  - 使用 musl libc 时返回 true。

- public static bool IsRiscv64()
  - 目标为 RISC-V 64 位时返回 true。

- public static bool IsWasm32()
  - 目标为 WebAssembly 32 位时返回 true。

- public static int GetProcessId()
  - 获取当前进程 ID。

- public static int Pid=> Environment.ProcessId;
  - 当前进程 ID 属性别名。

- public static string OS=> OperatingSystem.Platform;
  - 当前平台标识属性别名。

- public static string ExePath=> Environment.ProcessPath;
  - 当前可执行文件路径属性别名。

- public static long TickCountMs=> Environment.TickCount64;
  - 启动毫秒滴答数别名。

- public static string PathSeparator()
  - 返回当前平台的路径分隔符。

- public static string NewLine()
  - 返回当前平台的换行符。
