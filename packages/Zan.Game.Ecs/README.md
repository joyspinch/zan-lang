# Zan.Game.Ecs

实体-组件-系统运行时（纯 Zan，无原生依赖）。`using Game.Ecs;` 按需拉入。

C# 参考 DefaultEcs / Unity DOTS：把"数据"（组件）与"逻辑"（系统）分开，
让同一种逻辑作用在成百上千个实体上。**编译期定型、运行期零反射**。

## 文件

| 文件 | 内容 |
|---|---|
| `ComponentStore.zan` | `ComponentStore<T>`：一种组件的池（实体 id ↔ 槽位 ↔ 值） |
| `World.zan` | `World`：实体生命周期 + 系统调度 + 销毁清理 |
| `System.zan` | `System`：系统基类（抽象 `Update`） |
| `EcsGroup.zan` | `EcsGroup`：`PreUpdate` / `Update` / `PostUpdate` 三组 |
| `Query.zan` | `Query1<T>` / `Query2<T,U>`：类型化查询与遍历回调 |

## 用法

```zan
using System;
using Game.Ecs;

public struct Position { public double x; public double y; }
public struct Velocity { public double x; public double y; }

public class MoveSystem : System {
    public ComponentStore<Position> positions;
    public ComponentStore<Velocity> velocities;

    public MoveSystem(ComponentStore<Position> p, ComponentStore<Velocity> v) {
        this.positions = p;
        this.velocities = v;
        this.name = "move";
    }

    public override void Update(World world, double dt) {
        for (int i = 0; i < this.positions.SlotCount(); i = i + 1) {
            int entity = this.positions.EntityAt(i);
            if (!this.velocities.Has(entity)) { continue; }
            Position p = this.positions.Get(entity);
            Velocity v = this.velocities.Get(entity);
            p.x = p.x + v.x * dt;
            p.y = p.y + v.y * dt;
            this.positions.Set(entity, p);   // 值类型：改完要写回
        }
    }
}

World world = new World();
ComponentStore<Position> positions = new ComponentStore<Position>(new Position());
ComponentStore<Velocity> velocities = new ComponentStore<Velocity>(new Velocity());
positions.Bind(world);      // 实体销毁时自动清掉它的组件
velocities.Bind(world);

int e = world.CreateEntity();
positions.Set(e, new Position());
velocities.Set(e, new Velocity());

world.AddSystem(new MoveSystem(positions, velocities));
world.Update(0.016);
```

## 设计取舍

- **零反射**：组件池是泛型类，元素类型编译期定死。没有"按类型名查组件"的
  运行时字典——要那种东西得靠 `Gen*` 生成代码。
- **槽位顺序遍历**：实体 id 与值并置在两个平行数组里，按槽位遍历是连续内存
  访问；id 只在需要反查时用。
- **顺序稳定**：删除用顺序搬移（不是 swap-remove），槽位顺序不变——同种子
  同操作序列必然同结果（回放与锁步要的就是这个确定性）。
- **延迟销毁**：`DestroyEntity` 只打标记，真正的清理在帧末统一做。系统正在
  遍历组件时销毁实体是常态，立即删会改坏正在遍历的槽位。
- **值类型按值**：结构体组件取出来是副本，改完要 `Set` 写回；引用类型组件
  取出来就是对象本身，直接改。

## 查询

```zan
Query2<Position, Velocity> q = new Query2<Position, Velocity>(positions, velocities);
q.Each((entity, p, v) => { ... });
```

以元素较少的池为基准遍历（另一池用哈希查）——命中集小的一方决定遍历量。

## 已知边界

- 泛型类**不能持有值类型字段**（`class Box<T> { public T value; }`）：泛型类的
  字段布局不随实例特化，值类型字段的槽位大小算错。组件池因此把"缺失实体
  的零值"放进单元素 `List<T>` 盒子，而不是字段。编译器侧的修法见
  `TASKS.md` A421。
- 组件池不持有世界的引用（`Bind` 只注册回调），避免 world → 回调 → 池 →
  world 的 ARC 环。
- 系统之间没有"跨组依赖"声明（"A 必须在 B 之前"）；用加入顺序表达。
