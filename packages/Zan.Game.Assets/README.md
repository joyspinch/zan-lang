# Game.Assets

`Game.Assets` 是游戏资源运行时：**声明式清单 + 依赖图 + 引用计数加载 + 预算淘汰**。

它补的是 `Game.Kit.PackedAssets`（只读的加密资源包）与 `Game.Tools.AssetPipeline`
（离线元数据生成）之间缺的那一层——**运行期的资源生命周期管理**：哪些资源在内存里、
谁还引用着它、超预算了先卸谁。

## 设计取舍

- **清单与来源分离**：清单（`AssetManifest`）是纯数据，可从 JSON 读出、写回；
  来源（`IAssetProvider`）决定字节从哪来。同一份清单能配文件系统（开发）、
  打包文件（发布）、内存（测试 / 内嵌 / 网络注入），**调用代码一行不改**。
- **依赖是图的属性，不是引用次数**：`depCount` 恒等于"有多少个**已驻留**的资源
  依赖我"。资源驻留时对依赖 +1，被回收时 -1。把依赖计数和 acquire 次数混在一起，
  必然出现"资源提前被回收"或"永远回收不掉"二选一。
- **引用与驻留分离**：`Release` 只把 `refCount` 减一，资源**仍驻留**（缓存），
  由预算淘汰回收。用完立刻扔掉会让下一次使用重新读盘；内存回收交给显式预算，
  行为可预测。
- **确定性淘汰**：LRU 用单调递增序号（`lastUse`），**不用墙上时钟**——同一串操作
  必须得到同一结果，回放 / 锁步需要这个。同一 `lastUse` 时按状态建立序号定序。
- **超预算时宁可超着**：所有资源都被引用 / 依赖 / 钉住时，淘汰停手。
  偷偷回收正在用的资源比超预算危险得多。
- **pinned 不参与淘汰**：关卡常驻资源用 `Pin` / `Unpin`。
- **零反射**：解码器是按具体类型注册的（`AssetDecoder<T>` 委托），
  不是"按类型名查方法"。运行时一次查表都没有。
- **非线程安全**：面向单线程游戏主循环。要跨线程由调用方加锁
  （锁的粒度该由调用方决定，库不替你猜）。

## 能力

| 能力 | 入口 |
|---|---|
| 清单 | `AssetManifest`：`Declare` / `Add` / `Get` / `Validate` / `TopoOrder` / `ToJson` / `FromJson` |
| 资源条目 | `AssetEntry`（id / kind / path / depends / size）、`AssetKind`、`AssetKinds` |
| 来源 | `IAssetProvider`、`FileAssetProvider`、`MemoryAssetProvider` |
| 存储 | `AssetStore`：`Acquire` / `Release` / `Pin` / `Unpin` / `EnforceBudget` / `TrimIdle` |
| 加载器 | `AssetLoader`：把字节解码成强类型对象（`Load<T>`） |
| 资源组 | `AssetGroup`：按 id 前缀分组，整组加载 / 释放 |
| 错误 | `AssetException` + `AssetError`（`NotFound` / `DependencyFailed` / `ReadFailed` / `Underflow`） |

## 用法

```zan
using System;
using Game.Assets;

// 1. 声明清单（也可从 JSON 读：AssetManifest.FromJson(text)）
AssetManifest manifest = new AssetManifest();
manifest.Declare("tex/hero", AssetKind.Texture, "hero.png");
manifest.Declare("tex/ui", AssetKind.Texture, "ui.png").Depends("tex/hero");
manifest.Declare("scene/level1", AssetKind.Scene, "level1.bin")
       .Depends("tex/ui");
manifest.Validate();   // 悬空依赖 / 环依赖当场报错

// 2. 配来源 + 预算
AssetStore store = new AssetStore(manifest);
store.AddProvider(new FileAssetProvider("assets"));
store.WithBudget(64L * 1024L * 1024L);   // 64 MB

// 3. 加载（依赖自动递归拉取）
byte[] ui = store.Acquire("tex/ui");
// 此时 tex/hero 与 tex/ui 都已驻留，tex/hero 的 depCount = 1

// 4. 用完释放引用（资源仍驻留，可复用）
store.Release("tex/ui");

// 5. 超预算时按 LRU 回收闲置资源
store.EnforceBudget();
```

解码成强类型对象：

```zan
AssetLoader loader = new AssetLoader(manifest, store);
Texture tex = loader.Load<Texture>("tex/hero", data => Texture.Decode(data));
```

## 已知边界

- **没有异步加载**：`Acquire` 是同步的。流式加载（后台线程读盘 + 主线程回调）
  需要引入任务调度，暂未实现。
- **没有内容哈希 / 版本比对**：清单不记录内容指纹，热更时靠外部工具重生成清单。
- **淘汰只按字节数**：`AssetEntry.size` 是加载后才知道的真实字节数；
  清单里声明的 `size` 目前只作为元数据保留。
- **资源组是清单层面的划分**（按 id 前缀），不携带运行期状态；
  组不持有资源，资源也不持有组（无 ARC 环）。

## 验证

`tests/conformance/assets_store.zan` 覆盖：清单 JSON 往返、重复 id 拒绝、
悬空依赖拒绝、环依赖拒绝、未知 kind 拒绝、依赖自动拉取、depCount 的图语义、
引用计数累加 / 归零 / 下溢报错、预算淘汰、LRU 顺序、pinned 保护、
依赖阻塞淘汰、级联回退、资源组前缀分组与整组加载 / 释放、文件来源的缺失处理、
拓扑序。

## 放置

游戏包约定：`packages/Zan.Game.Assets/src/Game/Assets/*.zan`。
引擎级能力（物理 / 渲染 / ECS / 资源）走独立包并单独版本化，不进 stdlib。
