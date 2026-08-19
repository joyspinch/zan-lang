# 嵌入式（ESP32 类单片机）可行性评估与裁剪方案

结论先说：**现在跑不了，但门槛不在 flash 体积，在 RAM 常驻和目标架构。** 下面全部是实测数字
（Linux x64，`class Program { static void Main() { Console.WriteLine("hi"); } }`）。

## 1. 实测：一个 hello world 的成本

| 段 | 大小 | 说明 |
|---|---|---|
| text | 82 KB | 代码，flash 占用；-Oz --publish 后整个 exe 212 KB |
| data | 33 KB | 其中 `__zan_site_dtors` 单独 32 KB |
| **bss** | **192 KB** | **常驻 RAM，与程序写了什么无关** |
| 可执行文件 | 150 KB | 动态链接 glibc |

bss 里最大的几个（全部是编译期固定大小的静态表，无条件链接）：

| 符号 | 大小 | 出处 |
|---|---|---|
| `zan_shared_string` | 64 KB | `rt_sync.c`（`ZAN_TABLE_MAX_STRING`） |
| `zan_plat_text` | 64 KB | `rt_sync.c`（`ZAN_PLAT_TEXT_MAX 65536`） |
| `__zan_site_dtors` | 32 KB | 站点析构表（data 段） |
| `g_fh_table` | 16 KB | `rt_file.c`（`ZAN_FH_CAP 1024` 个文件句柄槽） |
| `g_dispatch_ring` | 8 KB | 协程调度环 |
| `__zan_eh_tids` / `__zan_eh_states` | 各 8 KB | 异常状态按线程 id 的固定表 |
| `zan__crash_exe` / `zan__crash_logdir` | 各 4 KB | 崩溃日志路径缓冲 |

对照：ESP32-S3 只有 **512 KB SRAM**（可用更少），ESP32-C3 是 400 KB。
**一个什么都没干的 Zan 程序就吃掉 ~225 KB（bss+data），接近一半 RAM。**

## 2. 实测：hello world 拉进来的系统依赖

`nm -uD` 显示即使只打印一行字，也无条件链接了：

- **epoll 反应堆**：`epoll_create1/ctl/wait`、`eventfd`
- **线程**：`pthread_create/detach/mutex*/once`、`sched_yield`、`gettid`
- **套接字**：`socket/accept/send/recv/getaddrinfo/getifaddrs/if_nametoindex`
- **动态加载**：`dlopen/dlsym/dlerror`
- **内存映射与共享内存**：`mmap/munmap/msync/mincore/shm_open/shm_unlink`
- **崩溃诊断**：`backtrace/backtrace_symbols_fd/sigaction`
- 文件与 glob：`glob/globfree/readlink/futimens/...`

单片机上这些**大部分不存在**（无 epoll、无 dlopen、无 shm、无 backtrace），
所以不是「大不大」的问题，是链不上。

## 3. 目标架构（更硬的门槛）

`zanc --list-targets` 现有：win-x64/arm64、linux-x64/musl/arm64、macos-x64/arm64、
wasm32-wasi、riscv64-linux-musl。

- **ESP32 / S2 / S3 是 Xtensa LX6/LX7** —— 上游 LLVM 的 Xtensa 后端仍是实验性的，
  Espressif 维护自己的 LLVM 分支。走这条路要换 LLVM 或接入 esp-clang。
- **ESP32-C3 / C6 / H2 是 RISC-V 32 位（rv32imc/rv32imac）** —— 现有 riscv64 目标不能用，
  需要新增 `riscv32imc-unknown-none-elf`（裸机）或 `-esp-elf`（ESP-IDF newlib）。
  这条路现实得多，**建议以 ESP32-C3/C6 为首个嵌入式目标**。

## 4. 裁剪方案：`embedded` 运行时 profile

目标：hello world 常驻 RAM < 16 KB，flash < 64 KB，除 libc 子集外零系统依赖。

1. **运行时分层（前提）**。现在 `zanrt_io/sync/timer/file/mem` 是无条件链接的一整块。
   拆成 `core`（ARC、字符串、数组、异常、数值格式化）/ `os`（文件、进程、环境）/
   `net`（套接字、反应堆）/ `async`（协程调度、线程池）/ `diag`（崩溃日志、backtrace）；
   编译期按 profile 选择，嵌入式只要 `core`。
2. **消灭编译期固定大表**。上表 7 个符号都改成「按需分配 + 编译期可配上限」
   （`ZAN_TABLE_MAX_STRING`、`ZAN_PLAT_TEXT_MAX`、`ZAN_FH_CAP`、eh 表按线程数），
   嵌入式 profile 下取小值或改为单线程静态单槽。**这一项单独就能砍掉 ~200 KB RAM，
   而且对桌面也是纯收益**（现在这些内存是白占的）。
3. **函数级死代码消除**。`-ffunction-sections -fdata-sections` + `--gc-sections`
   （链接器脚本已有的话确认打开），配合 stdlib 按引用裁剪：现在一个 hello world
   编译了 80 个 stdlib 文件。
4. **裸机适配层**（约 300 行）：`Console` → UART 写；`malloc/free` → ESP-IDF heap 或自带
   bump/TLSF 分配器；时间 → `esp_timer`；无线程（协程用单栈软调度）；无异常展开时
   `throw` 退化为错误码或 abort（需要产品决策）。
5. **新目标**：`--target esp32c3`（`riscv32imc-unknown-none-elf`），产出 `.a` 或 `.o`，
   由 ESP-IDF（CMake/idf.py）链接进固件；不自己做 flash/bootloader。
6. **CI 与验证**：qemu-riscv32 或 ESP-IDF 的 `qemu` 目标跑一组最小用例；
   用 `size` 报告把「hello world 的 text/data/bss」变成一条硬性预算测试，防止回退。

## 5. 工作量与顺序（我自己的口径，会话数）

| 步 | 内容 | 估计 | 单独收益 |
|---|---|---|---|
| 1 | 消灭固定大表（7 个符号可配/按需） | 1 会话 | 桌面也立刻省 ~200 KB RAM |
| 2 | 运行时分层 core/os/net/async/diag + gc-sections | 1–2 会话 | 所有目标体积下降 |
| 3 | riscv32 裸机目标 + 适配层（UART/heap/timer/单线程） | 2 会话 | ESP32-C3 能跑 |
| 4 | ESP-IDF 集成样例 + qemu CI + 体积预算测试 | 1 会话 | 防回退 |
| （可选） | Xtensa（ESP32/S3）经 esp-clang 或上游后端 | 2+ 会话，风险高 | 覆盖老型号 |

第 1 步无论要不要做嵌入式都值得做，建议先做它。
