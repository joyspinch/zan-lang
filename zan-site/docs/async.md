# 异步、任务与并发

当前实现说明，按仓库源码与测试核实于 **2026-11-11**。已安装 SDK 可能与此仓库状态不同。本文区分语言的异步调用、任务句柄、库级取消和线程同步；这些概念不能仅按 C# 中的同名类型推断。

## async 与 await 的含义

`async` 方法由编译器转换为状态机。挂起时保存后续执行所需的状态，等待条件满足后再恢复；它不等于每次调用都创建一个操作系统线程。

`await` 表达对某个结果或等待条件的依赖。直接等待异步方法时，结果类型来自方法声明；普通参数和仍需使用的局部值跨挂起保存在帧中。

下面是完整程序“直接等待结果”，采用 `tests/conformance/async_await.zan` 的计算与入口形状，预期输出为 `42`。

```zan
using System;

class Program {
    static async int ComputeAsync(int x) {
        int result = x * 2;
        return result;
    }

    static void Main() {
        int value = await ComputeAsync(21);
        Console.WriteLine(value);
    }
}
```

这个例子还说明，同步 `Main` 中可以使用某些形式的 `await`。具体边界见本页后文，不能简单写成“`await` 只能出现在 `async` 方法里”。

## 返回类型与任务句柄

常见声明是 `async int Compute()` 或 `async void Work()`。当前解析器也接受 `async Task<int>` 与 `async Task` 声明，将其分别归一为异步方法的结果类型 `int` 与 `void`。

因此，异步方法声明中的 `Task<T>` 写法与显式保存的 `Task<T>` 任务句柄不能混为一谈。最明确的结果读取方式是直接 `await Compute()`；若需要先启动、稍后读取，则使用有结果的 `Task.Run`。

| 调用形状 | 可保存的类型 | 结果与完成语义 |
| --- | --- | --- |
| `await ComputeAsync(...)` | 表达式结果为方法结果类型 | 等待子调用，取得结果并传播错误 |
| `Task.Run(ComputeAsync(...))`，非 `void` | `Task<T>` | 保留结果供消费者读取 |
| `Task.Run(WorkAsync(...))`，`void` | `Task` | 无结果任务 |
| `Task.Run(action)` 或无参 `void` lambda | `Task` | 无结果回调任务 |
| `Task.Spawn(WorkAsync(...))` | `Task`，可交给 `long` 句柄 API | 分离任务，只观察完成 |

`Task` 是编译器内建类型，句柄底层使用整数形式。调用者应优先保留有意义的 `Task` / `Task<T>` 类型，只在需要的汇合 API 上使用对应的 `long` 句柄接口。

不能通过把 `Task.Run` 的无结果 lambda 赋给 `Task<int>` 来得到结果。当前检查器会拒绝这种形状；返回结果的工作应使用非 `void` 异步调用。

## 先启动，再等待结果

以下是方法体片段，形状对应 `cs_b15_task.zan`；要求所在类型已声明返回 `int` 的异步方法 `Cube`。

```zan-fragment
Task<int> first = Task.Run(Cube(3));
Task<int> second = Task.Run(Cube(4));
int sum = first.Result + second.Result;
Console.WriteLine(sum);
```

两个任务先被启动，再读取各自结果。该形状不同于依次执行两个直接 `await`，后者表达顺序依赖。

`Task<T>` 提供结果读取；无结果 `Task` 可通过 `Wait()`、`IsCompleted` 等接口观察完成。异步上下文应使用适合挂起的等待形状，避免不必要的同步阻塞。

当前结果帧的消费和释放有具体约束。一个结果应由明确的消费者读取，再把得到的普通值传给其他使用者；不要未经验证就假设同一句柄支持任意重复消费和多个并发读取者。

需要把外部回调转换成可等待结果时，可使用 `TaskCompletionSource<T>`，而不是伪造任务句柄。

## 分离任务与汇合

`Task.Spawn` 接收异步调用表达式，例如 `Task.Spawn(Worker(1))`。它不是接收任意方法组的通用启动函数。

分离任务不为调用者保留返回值，也不通过其句柄交付异常。等待它完成，只说明工作已经结束，不能据此判定业务成功。

若需要交付结果，应选择有结果的 `Task.Run`、通道、结果源或清楚的共享状态协议。分离任务应在自身边界处理并记录错误。

| API | 作用 |
| --- | --- |
| `Task.IsDone(handle)` | 查询句柄是否完成 |
| `await Task.WhenAll(handles)` | 等待句柄列表全部完成 |
| `await Task.WhenAny(handles)` | 等待一个完成，返回其索引 |
| `TaskJoin.WhenAll` / `WhenAny` | 标准库的汇合助手 |
| `TaskJoin.CancelAll` / `DoneCount` | 对一组句柄请求取消或统计完成数量 |

句柄列表使用 `List<long>`。汇合不自动汇总分离任务的业务值和异常，也不会为任务组自动建立完整的结构化资源生命周期。

## 调度器与 worker

原生 64 位 Windows、Linux、macOS 等受支持目标默认接入多 worker 协程执行器。其他目标的驱动能力不同，应同时参考[平台支持](platforms.md)。

当前 `co_worker_count` 的解析顺序如下。

1. 程序通过 `System.Threading.AsyncRuntime.SetWorkers` 提供的有效设置。
2. `ZAN_CO_WORKERS` 环境变量。
3. 默认取逻辑处理器数与 4 的较小值。
4. 最终限制在 1 到 256 个 worker。

应用需要设置 worker 数时，应在启动异步工作之前配置。修改配置不应被理解为会随时重建一个正在运行的执行器。

任务可以在不同 worker 上恢复，不保证恢复到原线程。UI 线程关联、线程局部状态和原生线程绑定资源需要使用各自的调度或派发协议。

`await Task.Delay(ms)` 在异步方法中挂起到计时器，`await Task.Yield()` 主动让出执行机会。轮询任务状态的紧循环会浪费执行资源，通常应使用等待或信号机制。

当前编译器还会在异步方法的部分循环回边插入协作式调度检查。默认时间片由 `ZAN_CO_QUANTUM_MS` 控制，默认 2 毫秒，设为 0 可关闭；它不提供实时截止时间保证。

普通同步函数中的计算循环不因此自动具备让出能力。耗时同步工作会占用调用它的线程，应按实际任务拆分或安排合适的执行资源。

## 同步方法内的 await

检查重点是目标是否必须挂起到协程帧，而不是外层是否带 `async`。

| 同步上下文中的目标 | 当前语义 |
| --- | --- |
| 普通非异步值 | 求值后直接使用 |
| 异步方法调用 | 创建子帧并驱动到完成，调用方等待结果 |
| `Task.Delay(ms)` | 同步等待，不是挂起当前同步栈 |
| `Task.JoinWait(entry)` | 使用同步汇合路径 |
| `Task.Yield()` | 没有当前协程可让出，退化为空操作 |

`Gate.Park`、`Socket.ReadReady`、`Socket.WriteReady` 以及部分底层重叠 I/O、解析等待目标必须挂接协程帧，不能在同步方法中使用相同的挂起形状。编译器在对应降级位置给出诊断。

同步等待异步结果可以用于入口或边界适配，但不能据此断言调用链上的同步工作完全不占线程。

## 异常与跨挂起清理

直接等待的异步子调用如果失败，异常会在等待点进入调用方的异常处理。`try`、`catch`、`finally` 跨挂起的状态、异常类型及拥有关系有专门支持。

待恢复的局部引用保持有效；正常完成、异常完成和协作式取消会经过相应清理路径。资源关闭仍须由资源协议表达，见[内存与资源](memory.md)。

不同目标的异常传输实现有差别，例如 wasm32 使用引擎异常机制，不能把原生目标的全部异常边界行为无条件推广过去。

## 两种取消机制

任务帧取消与库级取消令牌是独立机制。

| 机制 | 请求方式 | 观察与传播 |
| --- | --- | --- |
| 协程帧取消 | `Task.Cancel(handle)` | 沿当前等待子帧链传播，在协作点检查 |
| 库级令牌 | `CancellationTokenSource.Cancel()` 等 | 显式传给业务方法，由方法检查、注册或等待 |

帧取消不会异步杀死线程。已挂在计时器或套接字等待上的操作，可能要等等待完成后才观察到请求；发出取消请求不等于资源已经可以马上销毁。

`Task.IsCancellationRequested()` 读取当前协程的取消标志。框架清理区域不会仅因请求取消而被直接跳过。

库级令牌提供 `IsCancellationRequested`、`Register`、`ThrowIfCancellationRequested()` 和异步 `WaitHandle()`。编译器不会自动把普通令牌参数与协程取消链绑定。

当前 `CancellationToken.ThrowIfCancellationRequested()` 抛普通 `Exception`，`TaskCompletionSource<T>.SetCanceled()` 也用普通异常表示取消结果。不能假设它们一定抛出 .NET 的专用取消异常类型。

取消注册也会持有回调，应按返回的注册对象和资源协议解除关系，避免任务结束后仍保留捕获对象。

## 通道与外部结果源

`System.Threading.Channel<T>` 提供有界和无界通道。`TryWrite` / `TryRead` 表达立即尝试，异步 `Write` / `Read` 表达等待可写或可读条件。

有界容量可为生产者提供背压。`Complete()`、`Close()`、`IsCompleted()` 与读写返回值共同构成结束协议；关闭并读空后，读操作返回失败而不是无限等待。

`TaskCompletionSource<T>` 将生产结果与消费结果分离。生产者调用 `SetResult`、`SetException` 或 `SetCanceled`，消费者使用 `await source.Task` 等待结果。

`TrySet...` 返回是否成功设置，适合处理多个可能同时结束操作的路径。`Set...` 在已经完成时会报告错误；资源释放仍要调用对象提供的关闭入口。

`AttachCancellationToken` 可将一个库级令牌桥接为结果源的取消结果。桥接是显式的业务选择，不是所有异步方法的隐式行为。

## 共享数据与同步

ARC 保护对象持有关系，不为任意字段、集合和复合操作提供互斥。检查后再写入这样的多步操作，通常仍需要同步协议。

可根据用途选择锁、原子量、`Gate`、异步锁、信号量、阻塞队列或通道。普通 `Mutex` 与语言 `lock` 不是可以随意互换的实现，重入与等待行为要按各自契约核实。

避免持有同步锁等待异步工作或执行可回调到自身的用户代码。让锁保护短小的状态变化，耗时工作与外部调用放在清楚的边界之外。

## 核实来源

- `tests/conformance/async_await.zan`：完整“直接等待结果”程序的依据。
- `tests/conformance/async_task_result.zan`、`cs_b15_task.zan`、`task_run_action_handle.zan`：声明结果类型与两类任务形状。
- `src/compiler/builtin_api.c`、`src/compiler/checker/checker_expr_adv.c`：任务内建与 `await` 类型检查。
- `docs/ASYNC_CPS_DESIGN.md`：Result slot、Exception propagation、Cancellation、Runtime and scheduler、Where await may appear。
- `src/runtime/io/rt_io_co_pool.c`：`co_worker_count` 的优先级、默认上限与范围。
- `docs/ZANC_CLI.md`：§4 运行期环境变量。
- `stdlib/System/TaskJoin.zan`、`stdlib/System/Threading/Channel.zan`：汇合与通道接口。
- `stdlib/System/Threading/CancellationToken.zan`、`CancellationTokenSource.zan`、`TaskCompletionSource.zan`：库级取消、结果和异常交接。
- `tests/conformance/async_sync_await_in_co.zan`、`async_sync_delay.zan`，以及 `tests/diag/await_gate_park_outside_async.zan`：同步等待边界。
