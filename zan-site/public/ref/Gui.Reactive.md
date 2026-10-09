# Gui.Reactive

> 源码: `packages/Zan.Gui/src/Gui/Reactive/Events.zan`


## Dispatcher (class)

- List<Action> queue;

- nint lockHandle;

- long uiThreadId;

- Dispatcher()

- bool IsUiThread()

- void Post(Action work)

- void Invoke(Action work)

- int Drain()


## EventHub (class)

- List<NamedEventSlot> slots;

- Dispatcher dispatcher;

- EventHub(Dispatcher d)

- int IndexOf(string evt)

- NamedEventSlot SlotFor(string evt)

- void On(string evt, Action handler)

- int CountFor(string evt)

- void Clear(string evt)

- void Emit(string evt)


## NamedEventSlot (class)

- string name;

- List<Action> handlers;

- NamedEventSlot(string name)


## void (delegate)

`delegate void Action();`
