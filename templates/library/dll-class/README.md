# {{NAME}}

可复用的 Zan 类库。公开 API 都在 `namespace {{NAME}}` 下：

| 类型 | 作用 |
|---|---|
| `IMetric` | 指标接口：`Name()` / `Value()` / `Describe()` / `Reset()` |
| `Counter` | 单调计数器：`Increment()` / `Add(n)` / `Count()` |
| `RunningStats` | 在线统计：`Add(x)` / `Mean()` / `StdDev()` / `Min()` / `Max()` |
| `MetricRegistry` | 注册表：`Register()` / `Find()` / `Report()` / `ResetAll()` |
| `LibraryInfo` | `Version()` / `Name()` |

```zan
using {{NAME}};

MetricRegistry reg = new MetricRegistry();
Counter hits = (Counter)reg.Register(new Counter("hits"));
RunningStats latency = (RunningStats)reg.Register(new RunningStats("latency_ms"));
hits.Increment();
latency.Add(12.5);
latency.Add(9.0);
Console.Write(reg.Report());
// hits: 1
// latency_ms: n=2 均值=10.75 σ=2.47 范围=[9, 12.5]
```

## 被其他 Zan 工程引用（源码包，推荐）

Zan 包以源码分发、按需编译，类型与泛型都完整可用：

1. 在本工程根目录加一个 `zan.pkg`：
   ```
   name = "{{NAME}}"
   version = "1.0.0"
   description = "..."
   ```
2. 把整个工程目录（含 `zan.pkg` 与 `src/`）复制到使用方工程的
   `.zan-packages/{{NAME}}/` 下（或 SDK 的 `packages/` 下供所有工程共用）。
3. 使用方源码里 `using {{NAME}};`，编译器自动发现并拉入。

## 编译成二进制共享库

`target = dll` 时 IDE "生成" 产出 `.dll / .so / .dylib`。二进制形态只能经
C-ABI 被调用（标量参数的 `public static` 方法，符号名 `类名_方法名`），类、接口、
字符串不跨边界——需要给 C/Python 等其他语言调用时，参考"动态共享库"模板。
