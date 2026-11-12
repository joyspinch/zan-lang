# Tasks.Jobs

后台任务队列：持久化任务存储 + 显式处理器注册 + 重试退避 + 延迟/周期调度 +
死信与人工重试 + 状态统计。纯 Zan，无反射，无原生依赖。

提供持久化 JobStore、处理器注册、重试策略、任务历史，以及五字段 cron 调度。
同一存储由单个调度所有者处理；不提供集群调度或内置 Web 仪表板。

## 设计取舍

### 1. 显式注册，不做反射式派发

任务名通过显式注册表解析，不依赖反射或按类型名构造对象。要让持久化下来的任务名在
重启后找到处理代码，应在**启动时显式注册**：

```zan
scheduler.Register("send-email", (argsJson) => {
    // argsJson 是参数 JSON 文本
    return JobResult.Success();
});
```

接线错误因此在**启动时**就暴露（重复注册 → `DuplicateHandler`），而不是等某个
任务半夜静默失败。

强类型参数走**泛型 + 显式序列化委托**通道，没有反射也能同时拿到"类型安全"和
"可持久化"：

```zan
scheduler.RegisterTyped<MailPayload>("send-email",
    (p) => { return Mailer.Send(p.to, p.body); },   // 处理
    (p) => "{\"to\":\"" + p.to + "\"}",              // 序列化
    (json) => {                                      // 反序列化
        MailPayload p = new MailPayload();
        JsonValue v = JsonValue.Parse(json);
        if (v != null) { p.to = v.Str("to", ""); }
        return p;
    });
```

**三个委托都要显式给**——没有反射就没有自动推导，"少给一个委托"的做法会退化成
"按名猜字段"。

### 2. 处理器返回结果，不靠抛异常表达重试意图

抛异常只能表达"失败了"，但后台任务还要回答"还要不要重试"——网络抖动导致的失败
重试有意义，参数非法导致的失败重试一万次也没意义。所以处理器自己把两件事说清楚：

```zan
JobResult.Success()      // 成功
JobResult.Retry("超时")   // 失败，但值得重试（退避后重排）
JobResult.Fail("参数非法")  // 失败且不可重试（直接进死信）
```

处理器**真的抛了异常**时按"可重试"处理（多数异常是临时的），并且 worker 不会
因此死掉。

### 3. 存储用追加日志，不是整表重写

`FileJobStorage` 是 **append-only 事件日志**：每次状态变更追加一行 JSON，启动时
重放一遍折叠成当前状态。整表重写在任务多时是 O(n) 每次变更，而追加只写一行，
崩溃时最多丢最后一行。代价是文件会变长，所以提供 `Compact()` 把历史折叠掉
（写临时文件再原子替换）。

### 4. 核心逻辑是同步方法

worker 的核心是同步的 `ProcessOnce()`——处理一轮当前到期的任务，返回处理条数。
后台线程只是它的薄包装。这么设计是因为**后台线程的执行时刻不确定，判定器没法
稳定复现"重试了几次""退避多久"**；同步入口让测试能精确驱动，也让你能"我就是想
同步跑一遍"。

### 5. 入队时校验任务名

没注册的处理器在**入队那一刻**就抛 `UnknownJob`，而不是让任务在队列里躺到被
worker 捡起来才变成死信。接线错误越早暴露越好。

## 能力

| 能力 | 说明 |
|---|---|
| 持久化存储 | `FileJobStorage`：追加日志 + 启动重放 + 压实；`InMemoryJobStorage`：只留当前状态 |
| 状态机 | `Enqueued` / `Scheduled` / `Processing` / `Succeeded` / `Failed`（还会重试）/ `Dead`（死信）/ `Deleted` |
| 处理器注册 | 显式注册（`Func<string, JobResult>`）或强类型泛型注册（`RegisterTyped`） |
| 重试退避 | 指数退避 + 延迟上限 + 抖动（打散同时重试的尖峰）；可配"总尝试次数" |
| 延迟调度 | `Schedule(name, args, runAtUnixMs)`：到点才执行 |
| 周期调度 | `EnqueueRecurring(name, args, intervalMs)`：每次成功后自动排下一次 |
| 幂等键 | `EnqueueWithId(id, ...)`：id 重复抛 `DuplicateId`（不静默跳过） |
| 死信处理 | 超过重试上限或不可重试 → `Dead`；`RetryDead(id)` 人工重排 |
| 状态统计 | `Stats()`：各状态计数快照；`List(name, state, limit)` 过滤查询 |
| 可区分错误码 | `JobError`：未注册 / id 冲突 / 存储失败 / 已在运行 / 找不到 / 非法结果 … |

## 用法

```zan
using System;
using Tasks.Jobs;

// 1) 建存储（文件持久化 / 内存）
FileJobStorage store = new FileJobStorage("data/jobs.jsonl");

// 2) 建调度器
JobScheduler scheduler = new JobScheduler(store, RetryPolicy.Default());

// 3) 注册处理器（启动时一次性做完）
scheduler.Register("send-email", (argsJson) => {
    Console.WriteLine("sending " + argsJson);
    return JobResult.Success();
});

// 4) 入队（生产者侧：只管写，不等执行）
string id = scheduler.Enqueue("send-email", "{\"to\":\"a@b.c\"}");

// 5) 起 worker（消费者侧：后台跑）
scheduler.StartWorker(200);

// 6) 看状态
Console.WriteLine(scheduler.Stats().Describe());
```

周期任务与人工重排：

```zan
scheduler.EnqueueRecurring("cleanup", "{}", 60000);          // 每分钟
scheduler.RetryDead("job-123-4");                          // 死信重跑
```

只用内存存储、单进程同步跑（测试 / 短任务）：

```zan
JobScheduler scheduler = new JobScheduler(new InMemoryJobStorage());
scheduler.Register("tick", (args) => { return JobResult.Success(); });
scheduler.Enqueue("tick", "{}");
scheduler.ProcessOnce();   // 同步跑一轮，返回处理条数
```

## 持久化 cron

用 `CronJobs` 管理按日历触发的任务。它复用 `JobStorage`、`JobRegistry` 和同步处理器，
可以使用 `FileJobStorage` 保存定义、下次触发、重试及执行中状态。重新注册相同定义会保留这些状态。

```zan
JobRegistry registry = new JobRegistry();
registry.Register("report", (args) => JobResult.Success());
FileJobStorage storage = new FileJobStorage("data/cron.jsonl");
CronJobs cron = new CronJobs(storage, registry, RetryPolicy.Fixed(3, 60000));
cron.Upsert("daily-report", "0 9 * * 1-5", "report", "{}");
cron.ProcessOnce(CancellationToken.None);
```

调用方周期性驱动 `ProcessOnce`，也可用 `TriggerNow(id)` 使任务立即到期。`TryStop()`
在派发结束后封闭调度器；活跃派发期间返回 `false`。每轮按快照检查到期记录，串行执行同步
处理器，取消只在回调之间生效。回调成功或重试耗尽后计算当前时刻之后的下一个 UTC 匹配，
停机错过的分钟不会逐条补跑。固定间隔的 `EnqueueRecurring` 仍沿用原有语义。

`CronExpression.Parse` 和 `CronJobs.Upsert` 默认要求日和星期同时匹配。旧 Hosting cron
用 `ParseHosting` 和持久化兼容标志，在两域都受限时取并集。旧数据不会改变匹配规则。

每次回调前先写入 `Processing`；如果结果未提交，下次派发会重放同一次尝试，且不额外消耗
重试额度。文件存储支持重开重放；回调副作用和结果写入不在同一事务中，因此需要幂等回调。
同一存储只允许一个 cron 调度所有者。原 `JobWorker` 不领取 cron 记录。

## 并发边界

- **进程内**：多个线程安全。文件存储的所有写操作走同一把进程级互斥锁。
- **跨进程**：文件存储用 `File.TryLock` 拿文件锁，**拿不到就直接报
  `StorageFailure`**（不做等待重试）。另一个进程持有锁通常意味着另一个服务实例
  正在写同一份存储——宁可让这次入队失败重试，也不要两个进程互相覆盖数据。
  换句话说：**单写者假设**，多实例部署要给每个实例独立的存储路径，或把存储换成
  数据库实现。
- **多个 worker 进程**：本包不做分布式锁、不做 leader 选举。需要时用存储实现
  自行扩展（`JobStorage` 是可继承的）。

## 验证

conformance `jobs_scheduler`：成功路径 / 未注册任务名在入队时拒绝 /
幂等 id 冲突 / 可重试失败走 Failed→退避→重试→超上限 Dead / 不可重试直接 Dead /
处理器抛异常按可重试处理 / 延迟任务不到点不跑 / 周期任务自动续命 / 死信人工重排 /
删除 / 状态统计 / 指数退避数值与上限 / 强类型注册往返 / 持久化重启后继续处理 /
压实后状态不丢。另覆盖 cron 日期语义、取消、重试、执行中重放和持久化删除。

## 已知边界

- **没有集群调度 / 分布式锁 / leader 选举**。
- **没有内置仪表板**。状态查询走 `Stats()` / `List()`，接自己的 Web 面板。
- **参数是 JSON 文本**，没有类型信息；强类型靠注册时显式给的序列化委托。
- **固定间隔任务每次执行是一条独立记录**（历史完整，但记录会变多，需要定期清理
  终结记录或 `Compact()`）。
- **文件存储是单写者**（见「并发边界」）。
- 存储层没有做索引，全量扫描；任务量到万级需要换实现。

## 放置

`packages/Zan.Jobs` —— 命名空间 `Tasks.Jobs`。不依赖 `Zan.Hosting` 或 `Zan.Data`。
