# Platform

> 源码: `stdlib/Platform/Runtime.zan`


## Runtime (class)

运行时平台检测与信息。所有查询都解析为
编译时选定的目标平台（WINDOWS / LINUX / MACOS），
因此交叉编译的程序报告目标系统而非构建主机。

- [DllImport("crt", EntryPoint="_getpid")]static extern int plat_getpid();

- [DllImport("crt", EntryPoint="getpid")]static extern int plat_getpid();

- static string GetPlatform()
  - 返回当前平台名称。

- static bool IsWindows()
  - 在 Windows 上运行时返回 true。

- static bool IsLinux()
  - 在 Linux 上运行时返回 true。

- static bool IsMacOS()
  - 在 macOS 上运行时返回 true。

- static bool IsWasi()
  - 在 WASI 上运行时返回 true。

- static bool IsMusl()
  - 使用 musl libc 时返回 true。

- static bool IsRiscv64()
  - 目标为 RISC-V 64 位时返回 true。

- static bool IsWasm32()
  - 目标为 WebAssembly 32 位时返回 true。

- static int GetProcessId()
  - 获取当前进程 ID。

- static string PathSeparator()
  - 返回当前平台的路径分隔符。

- static string NewLine()
  - 返回当前平台的换行符。
