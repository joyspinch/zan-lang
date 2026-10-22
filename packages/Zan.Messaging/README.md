# System.Messaging

`System.Messaging` 是**进程内消息总线**：类型化通道 + 发布 / 订阅 + 命令语义 +
请求-响应端点 + 中间件管道。

它补的是 MassTransit（7,809★）那一档的**进程内**那一半：把"这里发生了一件事"
和"谁负责处理它"解耦。纯 Zan 实现，无原生依赖，无反射。

## 设计取舍

- **通道由使用者建，不由总线按类型自动建**：Zan 没有运行期类型名（没有
  `typeof` / `GetType().Name`），总线没法判断"这个类型是不是已经建过通道"
  ——同一个类型建两个通道，消息就会被处理两遍。一个类型一个通道、通道由
  使用者持有，重复的问题就不存在了。
- **类型判定在通道的构造器里定型**：Zan 里**泛型类的虚方法体拿不到类型参数**
  ——`message is T` 经基类虚调用恒为 false（实测，见 `MessageChannelBase`
  注释）。所以判定逻辑由 `MessageChannel<T>` 的构造器定型成**委托**（那时
  类型参数是具体的），总线只调用委托，不碰类型信息。
- **消息必须是引用类型（类）**：总线内部按运行期类型分发，值类型没有运行期
  类型可言。更根本的是 Zan 的 `object` 目前**装不下值类型**（显式装箱直接崩
  进程，见 `TASKS.md` 记的编译器缺陷）。
- **订阅者异常默认原样抛出（不吞）**：总线里的异常多半是处理逻辑的 bug，吞掉
  会让问题变成"消息莫名其妙没到"。登记 `OnError` 之后才改走错误处理器。
- **中间件必须调用 `next`**：不调用就是"吞掉这条消息"，这是显式的取舍，不是
  bug。中间件链包裹的是**一次投递**（一次 `Publish` / `Send` / 查询跑一遍链，
  不是每个订阅者跑一遍）。
- **查询端点由使用者持有，结果类型完整保留**：Zan 的 `object` 装不下值类型，
  把结果擦成 `object` 再转回来这条路不通。端点是泛型类，处理函数是
  `FuncT<T, TResult>`，结果一路都是具体类型（int、double、结构体都行）。
- **非线程安全**：通道表与中间件表是逐线程读写的状态。

## 能力

| 能力 | 入口 |
|---|---|
| 总线 | `MessageBus`：`Register` / `Publish` / `Send` / `Use` / `OnError` / `RegisterEndpoint` + `ChannelCount` / `MiddlewareCount` / `DeliveredCount` |
| 通道 | `MessageChannel<T>`：`Subscribe` / `Unsubscribe` / `Publish` / `Send` / `SubscriberCount` |
| 查询端点 | `QueryEndpoint<T, TResult>`：`Handler` / `Handle` |
| 中间件 | `MessageMiddleware(object message, Action next)` |
| 错误 | `MessageException` + `MessageError`（`NoHandler` / `TooManyHandlers` / `HandlerFailed` / `InvalidHandler` / `InvalidMessage`） |

## 用法

```zan
using System;
using System.Messaging;

class OrderPlaced {
    public string customer;
    public int amount;
}

MessageBus bus = new MessageBus();

MessageChannel<OrderPlaced> orders = new MessageChannel<OrderPlaced>();
orders.Subscribe((OrderPlaced e) => { Console.WriteLine("处理订单 " + e.customer); });
bus.Register(orders);

bus.Publish(new OrderPlaced { customer = "张三", amount = 42 });
```

命令（**恰好一个**订阅者，零个或多个都当场报错）：

```zan
MessageChannel<Refund> refunds = new MessageChannel<Refund>();
refunds.Subscribe((Refund r) => { ... });
bus.Register(refunds);
bus.Send(new Refund { orderId = 7 });      // 订阅者不是 1 个 -> TooManyHandlers
```

中间件与错误路由：

```zan
bus.Use((object message, Action next) => {
    Console.WriteLine("收到消息");
    next();                                  // 不调 next 就等于吞掉这条消息
});

bus.OnError((Exception ex, object message) => {
    MessageException me = (MessageException)ex;
    Console.WriteLine("处理失败: " + me.ErrorName());   // HandlerFailed
});
```

请求-响应（结果类型不丢）：

```zan
QueryEndpoint<OrderPlaced, int> endpoint = new QueryEndpoint<OrderPlaced, int>();
endpoint.Handler = (OrderPlaced e) => { return e.amount * 2; };
bus.RegisterEndpoint(endpoint);             // 接上中间件链

int total = endpoint.Handle(new OrderPlaced { customer = "李四", amount = 21 });   // 42
```

## 验证

`tests/conformance/messaging_bus.zan` 覆盖：类型分发（消息只投给类型匹配的
通道）、广播（多订阅者按订阅顺序）、退订、命令语义（`NoHandler` /
`TooManyHandlers`）、中间件顺序与"不调 next 就吞掉"、错误路由（没登记处理器时
异常原样抛出；登记后包装成 `HandlerFailed` 且原异常在 `InnerException`）、
查询端点（值类型结果完整保留、字符串结果、接总线后中间件照样生效）、
参数校验（null 通道 / 中间件 / 错误处理器 / 消息各自报可区分错误码）。

ARC 守卫（`run_case_modes.cmake`）干净，`--check-leaks` 无泄漏。

## 已知边界

- **没有跨进程 / 跨机器传输**：只做进程内。跨进程要另做（序列化 + 传输层）。
- **消息不能是值类型**：总线按运行期类型分发，值类型进不来。
- **接总线时查询类型也必须是引用类型**：查询要经 `object` 传给中间件；
  没接总线时任意类型都可以。
- **没有消息重试 / 死信 / 延迟投递 / 出站箱**：中间件可以自己实现重试与
  延迟，但总线不提供持久化语义（那是 `Zan.Jobs` 的活）。
- **没有订阅者优先级、没有按类型名查找通道**：一个类型一个通道，靠持有关系
  找通道。
- **不支持线程安全并发投递**：通道表与中间件表是逐线程读写的状态。

## 放置

包约定：`packages/Zan.Messaging/src/System/Messaging/*.zan`。
非游戏领域能力走独立包并单独版本化，不进 stdlib。
