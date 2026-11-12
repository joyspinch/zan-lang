# Zan.Hosting

应用生命周期编排：把"常驻进程里那些跑在后台、要能干净收摊的东西"统一成
可组合的托管服务。纯 Zan 实现，无原生依赖、无反射、无 codegen。
`using System.Hosting;` 即可。

## 内容

| 类型 | 作用 |
|---|---|
| `IHostedService` | 托管服务契约：`StartAsync` / `StopAsync`（异步、可取消） |
| `BackgroundService` | 周期循环型后台服务基类：只写一次循环体，异常不杀循环，停止有界 |
| `AppHost` | 宿主：顺序启动、逆序停止（每个服务独立超时）、失败记账、状态快照 |
| `HostStatus` | 宿主状态强类型快照（供状态行/健康检查/管理页直接读） |

## 用法

### 周期型后台服务

```zan
using System;
using System.Hosting;

class OutboxFlusher : BackgroundService {
    public OutboxFlusher() {
        this.name = "outbox-flusher";
        this.SetInterval(1000);          // 每轮之间等 1s
    }

    public override async bool ExecuteAsync(CancellationToken ct) {
        int n = this.Drain();            // 一次循环体
        if (n > 0) { Log.Info("flushed " + Convert.ToString(n)); }
        await Task.Yield();
        return true;                     // false = 干完了，主动结束循环
    }
}
```

### 宿主装配

```zan
static void Main() {
    AppHost host = new AppHost("collector");
    host.Add(new OutboxFlusher());
    host.Add(new MetricsReporter());
    host.SetStopTimeout(5000);           // 单个服务停止上限，超时就放弃等它
    host.Run();                          // 启动 → 等 RequestStop() → 逆序停止
}
```

关闭请求从别处触发（信号处理、管理端点、测试）：

```zan
host.RequestStop();
```

与 DI 容器结合（`ServiceCollection` 里已经注册好的实例直接登记）：

```zan
ServiceCollection sc = new ServiceCollection();
sc.AddSingletonInstance("svc.outbox", new OutboxFlusher());
ServiceProvider sp = sc.BuildServiceProvider();

AppHost host = new AppHost("collector");
if (!host.AddFrom(sp, "svc.outbox")) { Log.Warn("outbox service missing"); }
```

## 语义契约

- **启动顺序 = 注册顺序，停止顺序 = 逆序**。后注册的先收摊，因此"依赖别人
  的服务"要后注册（被依赖者最后停）。
- **单个服务启动失败不阻断其余服务**：`StartAsync` 逐条调用并记账
  （`startFailures`），返回 `startFailures == 0`。宿主启动失败不等于进程不能
  干活——比如指标上报起不来，业务循环仍应继续。
- **停止有两条保险**：宿主先 `Cancel()` 统一的关闭令牌，再对每个服务带
  `SetStopTimeout` 有界等待（`Task.IsDone` 轮询）；超时只记账
  （`stopFailures`）、不把进程卡在这儿。服务自身也应尊重取消令牌，两侧都
  有界才叫"有界收摊"。
- **`BackgroundService.ExecuteAsync` 抛异常不杀循环**：记 `errors`/`lastError`
  后直接进入下一轮。后台循环因为一轮失败而静默死掉是线上最常见的失效，
  这里刻意不让它发生；需要"一次失败就退出"的服务自己 `return false`。
- **`ExecuteAsync` 抛出的异常不会被吞掉到查不出来**：`lastError` 保留最后一
  条原因、`errors` 保留累计次数，`Status().Line()` 与健康检查直接可用。
- **`intervalMs == 0` 表示不等间隔**（让出执行权后立刻下一轮），适合靠
  `await` 挂起等待的队列消费型任务；**不要**用它做忙等轮询。
- **`StopRequested()` 供长任务分段自查**：一轮循环体里跑几百毫秒以上的活时，
  在内部循环里查它，别等整轮跑完才响应关闭。
- **`RequestStop()` 是"置位 + 放行等待者"**，顺序固定（先 `Cancel` 后
  `Signal`）；`Run()` 在 `RequestStop()` 之前调用也不会丢请求（令牌已置位）。

## 性能取向

- `AppHost` 只是一组接口指针 + 一个关闭令牌 + 一个 `Gate`：`Run()` 挂起时
  不轮询、不空转，唤醒即为关闭请求。
- `BackgroundService` 的循环体开销只有一次 `Task.Delay`/`Task.Yield`；
  `ticks`/`lastTickUs` 是整数累加，读状态不分配对象。
- `Status()` 才分配快照对象，热路径零分配。

## 持久化 cron 任务

`PersistentJobs(path)` 可以像其他 `BackgroundService` 一样加入 `AppHost`。
用 `StoreOk()` / `StoreError()` 检查打开结果，设置
`handler = delegate(string id, string name, string param) { return true; };`，
再用 `Upsert(id, expression, name)` 注册任务。`TriggerNow(id)` 使任务在下一轮可执行，
`Snapshot()` 返回定义及状态，`Remove(id)` 删除定义和参数。启动时重复注册同一份定义会保留执行状态。回调参数保持原样；未存参数时传空串。

- cron 按 UTC 分钟匹配。日与星期都受限时取并集，与旧 `System.Hosting.Cron` 一致。
  轮询间隔只决定检查频率。停机时错过的多个触发合并为一次；完成后从当前时间找下次。
- `maxRetries` 是每次触发首次执行之外的重试次数，`retryDelayMs` 是固定等待毫秒。
  回调返回 `false` 或抛异常均记失败；重试耗尽后恢复 cron。`runs` / `fails`
  分别累计成功和失败的执行，`inFlight` / `retryPending` 分别表示执行中和等待重试。
- 保留旧 `job:<id>:name|cron|next|last|retry|runs|fails` 和 `param:<id>` 键，
  并增加 `phase`、`attempts`、`error`。调用回调前以同步事务提交执行中状态；重开时
  重放该次尝试。成功后清除参数；失败时保留。旧记录单有 `param:<id>` 无法证明执行中，
  仍按原 `next` 和 `retry` 处理。
  回调副作用与结果提交之间中断会导致重放，回调应可重复执行。
- 一个存储由一个服务实例串行调度；`maxParallel` 保留字段，非零设置会报告参数错误并拒绝执行。
  停止时取消后续派发，并等待当前同步回调。先确认 `StopAsync`（或随后 `Join`）成功，再
  调用 `Close()`；关闭不会中断正在执行的回调，未退出时会拒绝关闭。

## 跨平台与依赖

只依赖 `System`（`Console`/`Exception`/`Convert`/`Stopwatch`）、
`System.Threading`（`CancellationToken`/`Gate`）与 `System.DependencyInjection`
（仅为 `AddFrom` 的可选集成）；无平台分支，Windows / Linux / macOS / 嵌入式
同一份代码。持久化 cron 另使用 `Zan.Jobs` 派发和 `Zan.Data` 的 ZanStore 兼容存储。

## 边界

- **不接管进程信号**：Zan 标准库当前没有 `SIGINT`/`Ctrl+C` 钩子，"什么时候
  该关"由调用方决定（信号处理、管理端点、测试代码）。宿主只提供"关闭请求
  到达之后怎么干净收摊"。
- **不做分布式抢单**：需要多实例唯一执行时，使用有明确抢单契约的调度服务。
- **不做服务自动发现**：注册就是 `Add(...)`/`AddFrom(...)`，没有反射扫描。
- `BackgroundService` 是**单循环**模型：一个服务一个循环；要并发跑多份就
  装多个服务实例（宿主的启动/停止顺序对它们一视同仁）。
