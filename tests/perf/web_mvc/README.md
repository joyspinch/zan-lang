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

## 已知：list 场景被运行时阻塞卸载延迟主导（macOS，2026-11-22）

`SqliteConnection.QueryAsync` 每个 `sqlite3_step`（每行一次）都 `await` 过运行时
的阻塞卸载边界；在 macOS 上每次往返约 10–40 ms（同步 `Query` 只要几十微秒），
20 行一页 ≈ 200 ms。根因在运行时调度：kqueue/epoll 下阻塞完成的唤醒 fd 只注册在
shard 0，且单 shard 时也要等 20 ms 扫描（`ZAN_IO_SWEEP_MS`）才被收割；Windows 走
IOCP、向全部 shard 投递，未在本机验证。修复后该场景吞吐应有数量级变化，届时
重录基线。
