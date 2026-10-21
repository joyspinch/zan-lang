# System.Jobs

后台任务队列：持久化任务存储 + 显式处理器注册 + 重试退避 + 延迟/周期调度 +
死信与人工重试 + 状态统计。纯 Zan，无反射，无原生依赖。

对标 Hangfire / Quartz.NET 的核心子集（持久化 JobStore、触发器、重试策略、
任务历史），但**只做后台任务这一件事**：不做 cron 表达式语言、不做日历触发、
不做集群调度、不做内置 Web 仪表板。

## 设计取舍

### 1. 显式注册，不做反射式派发

Zan 没有运行期反射，也没有"从字符串构造对象"的能力。想让持久化下来的任务名在
重启后还能找到处理代码，唯一的办法是**启动时显式注册**：

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
| 处理器注册 | 显式注册（`FuncT<string, JobResult>`）或强类型泛型注册（`RegisterTyped`） |
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
using System.Jobs;

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

conformance `jobs_scheduler`（38 条断言）：成功路径 / 未注册任务名在入队时拒绝 /
幂等 id 冲突 / 可重试失败走 Failed→退避→重试→超上限 Dead / 不可重试直接 Dead /
处理器抛异常按可重试处理 / 延迟任务不到点不跑 / 周期任务自动续命 / 死信人工重排 /
删除 / 状态统计 / 指数退避数值与上限 / 强类型注册往返 / 持久化重启后继续处理 /
压实后状态不丢。

## 已知边界

- **没有 cron 表达式语言**。周期调度用固定间隔；需要 cron 时用
  `System.Hosting.Cron` 算出下次时间再调 `Schedule`。
- **没有集群调度 / 分布式锁 / leader 选举**。
- **没有内置仪表板**。状态查询走 `Stats()` / `List()`，接自己的 Web 面板。
- **参数是 JSON 文本**，没有类型信息；强类型靠注册时显式给的序列化委托。
- **周期任务每次执行是一条独立记录**（历史完整，但记录会变多，需要定期清理
  终结记录或 `Compact()`）。
- **文件存储是单写者**（见「并发边界」）。
- 存储层没有做索引，全量扫描；任务量到万级需要换实现。

## 放置

`packages/Zan.Jobs` —— 命名空间 `System.Jobs`，不依赖任何其它包
（cron 协作是可选的，调用方自己组合）。
