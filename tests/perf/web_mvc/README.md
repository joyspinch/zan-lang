# Web MVC 压测基线

度量 Zan Web MVC 框架**每请求开销**的固定场景，走真实框架路径：属性路由 →
编译器合成派发 → `AppController`（惰性请求租约）→ 视图 + 布局渲染。

| 场景 | 路径 | 内容 |
|---|---|---|
| `plain` | `/bench/plain` | 纯模板页：布局 + ~20 个变量 + 12 项菜单，不碰库（约 3.9 KB） |
| `list` | `/bench/list?page=3` | 后台列表页：COUNT + 分页 20 行（1000 行 SQLite 表）→ 渲染（约 9.3 KB） |

每个场景跑两档并发：连接池大小 ×1 与 ×4（默认池 8 → 8 / 32 条 keep-alive 连接）。
服务端为单进程单 `WebApp`（不开多 worker）。

## 运行

```bash
python3 tests/perf/web_mvc/run_bench.py --out tests/perf/web_mvc/baselines/<机器>.json
python3 tests/perf/web_mvc/run_bench.py --compare tests/perf/web_mvc/baselines/<机器>.json
```

- 需要已构建的 `build/zanc`；可执行文件产出到 `build/perf/`，SQLite 库在 `_scratch/`。
- `--compare`：吞吐跌幅 > 15% 或 p99 涨幅 > 30%（`--rps-drop` / `--p99-rise` 可调）
  即非零退出；任何 `errors` / `non200` / `stuck` 也非零退出。
- **只在同一台机器、空闲状态下比对才有意义。** 驱动记录 1 分钟负载，机器忙时打印
  警告；并行编译（其他会话的 zanc）会让吞吐波动 20% 以上。
- 不进 ctest：一轮几分钟、数字机器相关。

## 组成

- `server.zan`：基准服务端（`--port --pool --db --views`）。
- `load.zan`：纯 Zan 的 HTTP/1.1 keep-alive 压测客户端（跨平台，不依赖 wrk/ab），
  N 条连接闭环发 GET，按 `Content-Length` 读完整应答；接收带 10 s 时限，整轮有
  硬截止，挂死的连接记为 `stuck` 而不是拖死整轮。输出一行 JSON。
  在 macOS 上与 `ab -k` 同并发对照，吞吐差 < 6%，压测端 CPU 约 35%，非瓶颈。
- `run_bench.py`：编译、起服务、跑矩阵、写 / 比对基线。
- `baselines/`：各机器的基线 JSON（含提交号、机器信息、负载）。

## 历史与已知问题

- **已修（2026-11-23）：list 场景曾被运行时阻塞卸载唤醒主导。**
  kqueue / select 后端把阻塞调用的完成唤醒写进 pipe 的**读端**（EBADF，唤醒全丢），
  每个 `await` 原生阻塞调用（SQLite 每行一次 `sqlite3_step`、DNS）都要等 20 ms
  扫描（`ZAN_IO_SWEEP_MS`）才被收割，单次往返 10–40 ms。修复后 list 从 31 → ~3.9k
  rps（8 连接），p99 从 314 ms → ~2.8 ms。回归用例 `conformance/await_extern_wake_latency`。
  Linux（eventfd）与 Windows（IOCP）不受影响。
- **已修（2026-11-23）：服务端压测下间歇 SIGBUS。** `SharedTable.GetStringAt`（路由表每请求
  调用）返回运行时每线程 >1MiB 的读出缓冲；生成代码会探测返回串前方的头字（`ptr-8`，
  判定非串后再读 `ptr-16/-24`）。缓冲落在独立映射页首时前方是未映射页。缓冲前留 32 个
  零字节。回归用例 `conformance/shared_table_string_page_start`（旧运行时 8/10 崩）。
  根因（生成代码对外来指针无条件读头）在编译器侧，另行处理。
- **已修（2026-11-23）：两把全局锁占掉服务端四成 CPU。** `zan_rt_soft_scratch`（编译器插在
  大量普通函数里，只返回常量指针）与 `zan_rt_defer_pending`（release 出口读计数）每次都
  拿全局互斥锁；多 worker 在锁上排队。改为无锁快路径后 plain 12.4k → 30.9k rps（8 连接），
  p99 1.8 → 0.75 ms。list 不变（瓶颈在逐行阻塞卸载，见下）。
- **已修（2026-11-23）：list 场景每行一次阻塞卸载、每次卸载新建一个 OS 线程。**
  运行时阻塞作业改为线程复用（跑完的线程挂起等下一个移交，空闲 2 s 退出）；
  `SqliteConnection.QueryAsync` 只有首个 `sqlite3_step` 卸载，后续 step 在协程线程按
  1 ms 时间片内联（单步 > 200 µs 或超片即重新卸载）。list 8 连接 3.7k → 10.9k rps，
  p99 3.2 → 2.1 ms；32 连接受 8 条连接池限制（4.2k）。回归用例
  `conformance/blocking_pool_stress`、`conformance/sqlite_query_async_slices`。
- **已修（2026-11-23）：协程任务登记表每次线性扫描。** `rt_timer_task.c` 以全局数组存帧，
  config/allocator/child/retire 每次按帧指针线性查找、retire 还全表清子边，都在一把全局
  自旋锁下——O(活跃协程数)。改为开放寻址索引 + 空闲槽栈 + 父回边。同 HEAD A/B：plain
  32 连接 20.9k → 29.5k rps、p99 10.3 → 4.1 ms；8 连接 +8.7%。剩余：全局自旋锁仍有争用
  （自旋计入 zan_task_child 自身时间）。
- **待查：HEAD 自上一基线后 plain 8 连接 33.6k → 26.7k。** 期间唯一运行时相关提交是
  66b32700e（读串头前的可读性守卫）；剖析中其慢路径 `zan_rt_hdr_readable`→`vm_read_overwrite`
  约占 1% 采样，不足以解释，机器负载波动大，未定论。
- **已查明（2026-11-23）：压测端偶发 "out of memory"/SIGSEGV/double free 是 load.zan 自身的数据竞争。**
  连接协程跑在多个调度线程上，曾共用一个 List 记延迟、用普通 int 计数；并发扩容时分配器
  报 double free。已改为每连接独立结果槽、结束时合并（`connect_fail`/`send_fail`/`recv_fail`
  分项）。
- **已修（2026-11-23）：keep-alive 连接约 1/4 的后续请求永不应答（macOS arm64）。**
  根因：`PosixNetwork.SetNonBlocking` 把变参 `fcntl(int, int, ...)` 当三参普通 extern
  调用；Apple arm64 变参走栈，第三参落在寄存器里被忽略——返回 0 却没设上 `O_NONBLOCK`，
  所有经它的套接字一直阻塞。WebApp 的监听套接字因此在一个调度 worker 上阻塞 `accept()`，
  该 worker 负责轮询的 IO 分片（`fd % 分片数`，默认 4 片即 1/4 的连接）就绪事件无人收割：
  首个请求的字节通常已在缓冲区、非阻塞读直接拿到，之后的请求要等 kqueue 事件——永远
  等不到。之前"新建连接首请求不应答"的描述不准确，实为 keep-alive 后续请求。改调运行时
  的非变参 C 包装 `zan_io_set_nonblocking`。回归用例 `conformance/socket_nonblocking_accept`。
  修后整轮 errors=0、stuck=0，基线重录。
- **已修（2026-11-23）：list 32 连接吞吐反比 8 连接低一半。** 两处"每协程步/每帧完成都拿
  全局锁"的运行时路径，代价随在途量上涨：① 编译器在每个异步帧完成处调
  `zan_timer_cancel_delay`，它持全局定时器锁全堆扫描 + 重建堆；连接池排队时每次门等挂一个
  250 ms 看门狗，堆常驻上千条。改为帧 → 条目索引 + 惰性删除（死条目过半再压缩）。
  ② `co_run` 每步末尾只要有非 worker 线程在 `co_ext_wait`（服务端主线程终身在等根任务）
  就拿 `g_co_ext_mx` 广播；改为只在刚跑的帧正是被等的帧时广播。同 HEAD A/B：list 8 连接
  9.7k → 13.5k rps（p99 1.45 → 0.99 ms），32 连接 4.1k → 8.3k（p99 19 → 10 ms）。回归用例
  `runtime_timer_delay_cancel_index`。剩余差距在连接池等待路径（每次门等的 Gate 锁、
  派生看门狗协程、`PoolCore.IsClosed` 加锁）。
- **未解（间歇）：压测端偶发 1 条连接 `stuck`。** 带 10 s 时限的接收在 25 s 内都没返回，
  指向 socket 读路径上另一处丢唤醒（在压测客户端进程里），与上面已修的阻塞卸载
  路径不同。高负载（load ~30）下 3 轮里出现 1 次；未复现到可调试的程度。
