> **归档说明（2026-09-21）**：本文档为嵌入式运行时（ESP32-C3 / RV32）可行性评估与落地阶段记录，第 1~4 步已全部完成。现行参考代码与工程配置请参考 [examples/esp32_hello/](../../examples/esp32_hello/README.md) 与 [examples/rv32_qemu/](../../examples/rv32_qemu/README.md)。

# 嵌入式（ESP32 类单片机）可行性评估与裁剪方案

结论先说：**现在跑不了，但门槛不在 flash 体积，在 RAM 常驻和目标架构。** 下面全部是实测数字
（Linux x64，`class Program { static void Main() { Console.WriteLine("hi"); } }`）。

> **2026-09-07 更新：第 1 步（消灭固定大表）已落地**，见文末「落地记录」。原文的符号表
> 有两处过时：异常表当时已是动态增长；`__zan_site_dtors` 只在 `-leaks` 构建里存在，
> 默认 descriptor 模式从不生成。
>
> **2026-09-07 更新 2：第 2 步的交叉 publish `--gc-sections` 已补齐**——linux/OHOS/
> Android（pie+static）/Windows 交叉 PE/wasm 五处 ld.lld 链接在 publish 模式下现在
> 与 native MinGW 路径一样传 `--gc-sections`（macOS 路径本就有 `-dead_strip`）。
> 与 irgen_emit.c 已有的「publish 模式每函数一个 `.text.<fn>` 段」配合，交叉产物
> 不再原样携带全部函数段。hello world（linux-x64 静态）file 78,728 → 74,728 B、
> text 64,034 → 60,482 B；类多的程序收益更大。与 native publish text 44 KB 的
> 差值主要是 musl 静态 printf 进 text（Windows 上 printf 在系统 DLL，不入 text），
> 属预期。剩余的交叉/native 差值收口依赖运行时对象补 `-ffunction-sections`
> （需 CI 重出提交对象，另案）。防回退：新增 `size_budget_publish_host` /
> `size_budget_publish_linux_x64` 两条 ctest（tests/run_size_budget.cmake，
> ~1.5× 基线余量），固定大表回归或交叉链接丢 `--gc-sections` 都会在此爆掉。
>
> **2026-09-07 更新 3：第 3 步的 riscv32 裸机对象发射已落地**。`--target riscv32`
> / `--target esp32c3`（均映射 `riscv32-unknown-unknown-elf`，rv32imc + ilp32 软浮点，
> 对应 C3/C6 无 FPU 核）现在产出 ELF32 RISC-V **目标文件**（freestanding：无 CRT、
> 无 sysroot、不链接——ESP-IDF 拥有启动代码与最终链接，`.o` 里的 malloc/printf/
> `zan_timer_*` 未解析符号正是它的适配面）。实现：`crosscomp` 增加
> `ZAN_ARCH_RISCV32`（指针 4 字节）；`irgen_emit.c` 加 rv32 分支（generic-rv32、
> `+m,+c`、target-abi=ilp32 模块旗标）；`main.c` FREESTANDING 分支只做
> obj_tmp→obj_path 改名；程序侧新增 `RISCV32` 预定义宏。后端可用性仍由
> `ZAN_HAVE_LLVM_RISCV` 门控（CMake 按 `LLVM_TARGETS_TO_BUILD` 自动探测；
> mozbuild clang 不带 RISCV，这台机器用官方 LLVM 23.1.0 win64 发行版在
> _scratch 单独构建验证——LLVM 23 把 C-API `LLVMBr` 拆成 `LLVMCondBr`/`LLVMUncondBr`，
> 现以 `ZAN_LLVM_MAJOR` 编译定义分流，20/23 都能编）。防回退：`cross_riscv32_object`
> ctest（tests/run_riscv32_object.cmake）在校验 ELF32/EM_RISCV 头，且只在 LLVM 带
> RISCV 后端时注册（无后端的发行版上该测试不存在，其余目标不受影响）。
> 尚余：裸机适配层（UART/heap/timer/单线程协程驱动，原第 3 步后半）与 64 位原子
> （rv32 上 `__atomic_*_8` 需要 libatomic 或 IDF builtins）是 ESP-IDF 样例（第 4 步）前
> 的最后两块。
>
> **2026-09-07 更新 4：裸机适配层与 ESP-IDF 样例已落地（第 3 步收口 + 第 4 步样例）**。
> 先做了符号面审计（bare/hello/string/async 四探针的全量 `nm -u` 并集）：rv32 产物
> 的未解析面 = libc/libgcc（printf 族、堆、setjmp/longjmp、`__atomic_*`，ESP-IDF
> 自带）+ poll/pthread 三件（IDF vfs/pthread 自带）+ **6 个 `zan_timer_*` 与 2 个
> `zan_rt_soft_*`**——这正是唯一需要适配的部分。落地四件：
> ① `rt_timer.c` 新增 `ZAN_BARE_METAL` 分支（照 wasm 先例：空锁单线程、不装信号
> crash 日志、`zan_timer_now_ms` 在裸机下为 weak 可被目标覆盖；IDF 的
> clock_gettime(CLOCK_MONOTONIC) 直接映射 esp_timer，软日志 fopen 失败本就静默
> 降级为 stderr 一行）；
> ② 修了一个真 ILP32 ABI bug：IR 用 i64 声明 libc 的 size_t 参数，rv32 的寄存器
> 配对规则会让中段 i64 之后的所有实参错位（wasm 有严格签名检查暴露了同一问题，
> ELF 不查所以静默）——把 wasm32 的 32 位 libc 适配器门控扩到 riscv32
> （`__zan_w32ir_*` 按调用点类型生成包装），snprintf 仍路由 `zan_w32_snprintf`；
> ③ `src/runtime/rt_bare_shim.c`：无 SDK 场景的通用垫片（确定性 64KB 池分配器、
> poll/pthread/getenv 桩、snprintf 包装，主机端十万次级 churn 实测通过）；
> ④ `examples/esp32_hello/`：`zanc --target riscv32` 出对象 → ESP-IDF 工程
> （main/CMakeLists.txt 自动重编 .zan、`rt_timer.c -DZAN_BARE_METAL`、
> zan_adapter.c 提供 app_main 与 snprintf 包装）+ 符号契约表 README。
> `cross_riscv32_object` ctest 顺手加了对象体积硬预算（默认 32 KB，防固定表回归）。
> 本机验证：适配器确实接进 rv32 对象（nm 见 4 个 `__zan_w32ir_*.v0` 本地包装）、
> bare 分支主机编译导出全部 8 契约符号、smoke 除并行会话两个 gui AA 用例外全绿；
> `idf.py build` 与上真机需要带 ESP-IDF 的机器，README 的状态节写明了这一边界。
>
> **2026-09-07 更新 5：QEMU 全系统实测落地——rv32 裸机真正跑起来，体积/内存/CPU
> 三项拿到实测数**。`examples/rv32_qemu/` 是一套在 `qemu-system-riscv32 -M virt`
> 上从 `_start` 跑到 finisher 退出的裸机套件：crt0.S（gp 装载、栈卫兵涂写、bss
> 清零、CLINT 比较器装填、picolibc 的 `__libc_init_array`）、bsp.c（NS16550A
> UART——picolibc tinystdio 归档里没有 stdout 对象，由板卡用
> `FDEV_SETUP_STREAM` 提供；CLINT mtime 覆写 weak `zan_timer_now_ms`；poll()
> 用 mtimecmp+MTIE+wfi 让 `Task.Delay` 真睡眠；rv32imc 没有 A 扩展，zan 对象
> 实际引用的 4 个 `__atomic_*` 用关中断临界区实现；exit() 打印栈高水位并写
> sifive_test finisher 让 QEMU 自动退出）、link.ld（0x80000000、gp 窗口、
> .init_array 保留给 stdio 构造器）。Ubuntu 工具链 gcc-riscv64-unknown-elf +
> picolibc（rv32imac/ilp32 多库）+ qemu-system-misc，`build.sh`/`run.sh` 一键
> （WSL+Windows zanc.exe 混用时脚本内做 wslpath 转换）。hello.zan（5 tick、
> 间隔 1 s `Task.Delay`）实测：**体积** flash 载荷 4,608 B（Berkeley text
> 4,504 + data 96；--gc-sections 后 libc/libgcc 侧大头是整数 printf 与
> `__udivdi3`/`__umoddi3`）；**内存** bss 65,632 B——64 KiB 是垫片确定性池
> （`-DZAN_BARE_HEAP_BYTES` 可调，其余静态仅 96 B）+ 32 KiB 栈预留，栈卫兵
> 实测高水位 112 B（随程序复杂度增长，退出时自动打印）；**CPU** 整轮墙钟
> 4.106 s 对应 4×1 s 延时，QEMU 进程主机 CPU 约 45 ms（≈1%）——poll() 的
> wfi 睡眠生效，await 不忙等，tick 精度即 mtime（10 MHz）精度。顺带修了
> zanc 对 LLVM 23 的适配缺口：LLVM 23 移除了 Os/Oz 优化级，`--publish` 直接
> LLVM ERROR——optimizer.c 改走 O2 管线 + optsize/minsize 函数属性（clang
> -Os/-Oz 的现行编码），旧 LLVM 编译定义分流不受影响。边界照实说：QEMU virt
> 核是 rv32imac，套件用该多库链接而 zanc 产物仍是 rv32imc（真机 C3/C6 走
> ESP-IDF 工具链，第 4 步样例的验证边界不变）；原子只实现了本探针引用的
> 4 个，宽程序按符号契约表补齐。

> **2026-09-07 更新 6：可靠性三件套上机——trap 向量、栈哨兵、分配器水位 +
> 8000 轮浸泡全绿，顺带抓出并修掉三个真缺陷**。①**统一 trap 向量**
> （crt0.S `zan_trap`）：mcause 位 31 = 中断且码 7（MTIP）走两寄存器快路径
> （清比较器 + mret），其余同步异常保存现场调 `zan_fault_report` 打印
> `mcause/mepc/mtval/ra` 后 FAIL 停机（QEMU 退出码 139）——裸机不再有
> "未装 mtvec 一切异常 = 静默跑飞"；对象里埋非法指令实测：
> `TRAP mcause=0x2 mepc=0x80000574 mtval=0x0 ra=0x8000056c`。②**栈哨兵**：
> crt0 涂写卫兵之外，poll() 每轮检查 28 KiB 预算线卫兵字，越线当场打印
> STACK OVERFLOW 停机，而不是静默踩内存；exit() 全扫描报高水位
> （hello 128 B、soak 216 B / 32 KiB 预留）。③**垫片池水位 + 实勘**：
> malloc/free 记 live/peak/oom/frees，exit 报告加 free-list 巡检
> （total/maxhole/holes）与常驻块 top（live-top）。**浸泡**（soak.zan，
> 8000 轮混合尺寸字符串 churn + 变长延时，全程 `--publish`）：exit 0、
> **oom 0、frees 24040/24044 平衡、live 稳定不漂移、自由表始终一整块**
> （maxhole = total）。浸泡抓出的三个缺陷都修在根上：
> （a）**--publish 优化在 datalayout 绑定前运行**——LLVM 用默认 64 位指针
> 布局烘焙 8 字节指针步长进 IR，rv32 实际 32 位布局导致去混淆字面量表
> 读 NULL 即 trap（x86-64/arm64 默认布局恰好等于真实布局所以无症状）；
> 修法：main.c 在优化前调 `zan_irgen_bind_target`（irgen_emit.c 拆出
> 目标三件套绑定），wasm32 同样带此隐患一并消掉。（b）**EH 自指针 TLS 在
> 裸机 rv32 必然野指针**——`__zan_eh_self` 以 InitialExec 发射，裸机没有
> tp 寄存器与 TLS 块，GOT 槽读 0 再 `add tp` 即野指针 trap；修法与
> wasm/Windows 同例：riscv32 不发射线程局部，回落线程表探针（单线程契约
> 下探针即全帧代价）。（c）**垫片分配器 free 不合并**——churn 模式
> （缓冲每轮 +64 B）让每个刚释放的块都比下一轮请求小 64 B，first-fit
> 永远复用不上，池在 90% 空闲时碎片化 OOM（40 洞、最大 1200 B、请求
> 1392 B）；修法：free 改地址序插入 + 前后立即合并。另一项**已修的硬伤**
> （2026-09 审计）：裸机 try/catch 曾一触即 OOM——picolibc 没有 `_setjmp`
> （链接期 undefined reference），且 EH chunk 默认几何（64×1040 B 处理器
> chunk + 4096×8 B 临时 chunk）首次 try 即申请 ≈97 KiB，超过整个 64 KiB
> 默认池直接 abort。修法：riscv 裸机发射 `setjmp`，chunk 收窄为 2×1040 B /
> 64×8 B（chunk 表仍 64 项，只是粒度变细；宿主几何不变）——首次 try 的
> 常驻降到 ≈3 KiB（状态块 536 B + 处理器 chunk 2080 B + 临时 chunk 512 B）。
> 裸机 try/catch + await-in-catch 已在 QEMU 实测通过（eh.zan 探针）。
> `soak` 的常驻 live-top 由 32768+8192+8192+536 降到 8192+8192+536+512。
> 垫片分配器同批加固：块 magic + 范围校验拒绝野/二次 free（退出报告输出
> BAD-FREE n）、calloc 溢出检查、零头吸收精确归还。POSIX 文件桩
> （open/read/write/…、stderr FILE）补齐后，稍宽的程序也能上板验证。

## 1. 实测：一个 hello world 的成本

| 段 | 大小 | 说明 |
|---|---|---|
| text | 82 KB | 代码，flash 占用；-Oz --publish 后整个 exe 212 KB |
| data | 33 KB | 其中 `__zan_site_dtors` 单独 32 KB |
| **bss** | **192 KB** | **常驻 RAM，与程序写了什么无关** |
| 可执行文件 | 150 KB | 动态链接 glibc |

bss 里最大的几个（全部是编译期固定大小的静态表，无条件链接）：

| 符号 | 大小 | 出处 | 状态（2026-09-07） |
|---|---|---|---|
| `zan_weak_buckets` | 64 KB | 编译器发射进每个模块（`irgen_weak.c`） | ✅ 已改懒分配（首次弱写入时 calloc，弱自旋锁内，无竞态） |
| `zan_shared_string` | 64 KB | `rt_sync.c`（`ZAN_TABLE_MAX_STRING`） | ✅ 已改每线程懒分配（POSIX pthread key / Windows FLS），线程退出释放；Windows 64 KB 静态兜底撤除 |
| `zan_plat_text` | 64 KB | `rt_sync.c`（`ZAN_PLAT_TEXT_MAX 65536`） | ✅ 已改 POSIX 每线程懒分配（本就只服务网络接口枚举） |
| `__zan_site_dtors` | 32 KB | 站点析构表（data 段） | ℹ️ 仅 `-leaks` 构建存在，默认构建零成本（原文误计） |
| `g_fh_table` | 16 KB | `rt_file.c`（`ZAN_FH_CAP 1024` 个文件句柄槽） | ✅ 已改按需增长（64 槽起步，倍增到 1M 上限，句柄编码与失败码不变） |
| `g_dispatch_ring` | 8 KB | 协程调度环 | ✅ 初始容量 1024→64（增长路径本就存在） |
| `__zan_eh_tids` / `__zan_eh_states` | 各 8 KB | 异常状态按线程 id 的固定表 | ℹ️ 原文过时：当时已是动态表（1024 起步自动翻倍，从不 throw 零成本） |
| `zan__crash_exe` / `zan__crash_logdir` | 各 4 KB | 崩溃日志路径缓冲 | ⏸ 保留静态：信号处理器内禁 malloc，安装期一次性解析的设计如此 |

对照：ESP32-S3 只有 **512 KB SRAM**（可用更少），ESP32-C3 是 400 KB。
**一个什么都没干的 Zan 程序就吃掉 ~225 KB（bss+data），接近一半 RAM。**

### 落地后的实测（Windows x64，同一 hello world）

PE 的 bss 计入 .data 虚拟尾部，按段 VMA 空洞测得：

| 构建 | bss | 变化 |
|---|---|---|
| 改造前 | 77,824 B | — |
| 改造后 | **12,288 B** | **−84%**（弱表 64 KB 消失，其余在链接对象里） |

运行时对象 bss：`zanrt_sync.obj` ~76.5 KB → **3,264 B**；`zanrt_file.obj` ~21.1 KB → **4,704 B**。
POSIX 侧每线程另省 128 KB TLS（shared_string + plat_text 各 64 KB）：百线程服务约省 12.8 MB。
行为语义保持：共享表 64 KB 字符串上限、句柄/错误码、超限「table full」答案均不变；
Windows FLS 失败从「共享静态兜底（潜在数据竞争）」改为 host_oom 致命中止（修一处潜伏竞争）。

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
