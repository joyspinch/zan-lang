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
- **未解（间歇）：压测端偶发 1 条连接 `stuck`。** 带 10 s 时限的接收在 25 s 内都没返回，
  指向 socket 读路径上另一处丢唤醒（在压测客户端进程里），与上面已修的阻塞卸载
  路径不同。高负载（load ~30）下 3 轮里出现 1 次；未复现到可调试的程度。
