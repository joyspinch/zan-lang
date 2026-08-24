# Gui.Reactive

> 源码: `stdlib/Gui/Reactive/Events.zan`


## Dispatcher (class)

UI 线程派发队列。后台线程通过 Post/Invoke 将工作入队；
UI 线程每帧通过 Drain() 排空并执行一次。这正是
事件回调和属性写入跨线程安全的原因：在
UI 线程之外触发的工作会被调度回该线程，而不是从
另一线程触碰 UI 状态。

- List<Action> queue;

- nint lockHandle;

- string lockHandle;

- long uiThreadId;

- Dispatcher()
  - 创建由调用（UI）线程拥有的调度器。

- bool IsUiThread()
  - 当从创建调度器的线程调用时为 true。

- void Post(Action work)
  - 将工作排队到 UI 线程执行。任意线程调用均安全。

- void Invoke(Action work)
  - 在 UI 线程上运行工作：已在该线程则立即执行，否则
    经 Post 调度过去，调用方不会跨线程修改 UI 状态。

- int Drain()
  - 排空所有已排队的工作并在当前（UI）线程执行。请在
    UI/事件循环中每帧调用一次。返回执行的工作项数。


## EventHub (class)

C# 风格的事件聚合器。组件用 On() 将委托订阅到命名事件，
用 Emit() 触发。挂上 Dispatcher 时每个
处理器都会自动在 UI 线程运行，因此从工作线程
触发事件是安全的。

- List<NamedEventSlot> slots;

- Dispatcher dispatcher;

- EventHub(Dispatcher d)
  - 创建 hub。传入 Dispatcher 以自动在 UI 线程投递，或传
    null 以在调用线程同步调用处理器。

- int IndexOf(string evt)

- NamedEventSlot SlotFor(string evt)

- void On(string evt, Action handler)
  - 将处理器订阅到事件（类似 `evt += handler`）。

- int CountFor(string evt)
  - 当前订阅某事件的处理器数量。

- void Clear(string evt)
  - 移除某事件的所有处理器。

- void Emit(string evt)
  - 触发事件，调用每个订阅者。挂有 Dispatcher 时
    处理器被调度到 UI 线程；否则内联执行。


## NamedEventSlot (class)

保存单个事件的名称与订阅者。

- string name;

- List<Action> handlers;

- NamedEventSlot(string name)


## void (delegate)

事件和调度器使用的无参回调。

`delegate void Action();`
