# Game.Physics2D

`Game.Physics2D` 是 2D 刚体动力学层：凸形状、窄相检测、顺序冲量求解器与关节。

它补的是 `Game.Arcade2D`（几何检测 + 渲染无关的实体池）之上缺的那一层——
**动力学**：积分器、约束求解、穿透修正、关节。

## 设计取舍

- **零反射、编译期定型**：形状是类（`CircleShape` / `PolygonShape`），
  求解器只认这两个具体类型；没有"按类型名查形状"的运行时字典。
- **确定性**：接触顺序、关节顺序、迭代次数全部固定，没有随机数——
  同一初始条件 + 同一操作序列必然得到同一结果（回放 / 锁步的前提）。
- **凸多边形**：SAT 需要凸性——凹多边形在**构造时就被拒绝**（抛
  `ArgumentException`），不静默给出错误的分离量。
- **推测接触代替 TOI 扫掠**：分离量在 `speculativeMargin` 以内也建接触，
  约束把接近速度限制为 `gap / dt`——高速物体在"还没撞上"时就被拦住，
  常规速度不穿墙。真正的 TOI（时间_of_impact）扫掠没有实现。
- **穿透修正用分离冲量**：位置修正走伪速度（split impulse），不把能量
  注入真实速度，堆叠物体因此不会"越修越弹"。

## 能力

| 能力 | 入口 |
|---|---|
| 形状 | `CircleShape`、`PolygonShape`（`Box()` 造盒子） |
| 刚体 | `RigidBody.Dynamic` / `RigidBody.Static`，质量与转动惯量按面密度算 |
| 世界 | `PhysicsWorld`：`AddBody` / `AddJoint` / `Step(dt)` |
| 窄相 | `Collision.Collide(...)` → `Manifold`（法线、接触点、分离量） |
| 关节 | `RevoluteJoint`（铰链）、`DistanceJoint`（定距 / 弹簧） |
| 宽相 | 扫掠剪枝（按 x 排序，帧间增量维护） |

## 用法

```zan
using System;
using Game.Physics2D;

PhysicsWorld world = new PhysicsWorld();
world.gravity = new Vec2(0.0, -10.0);

world.AddBody(RigidBody.Static(PolygonShape.Box(10.0, 0.5), new Vec2(0.0, -0.5), 0.0));

CircleShape ball = new CircleShape(0.5);
RigidBody body = world.AddBody(RigidBody.Dynamic(ball, new Vec2(0.0, 5.0), 1.0));
body.restitution = 0.3;
body.friction = 0.4;

for (int i = 0; i < 120; i = i + 1) {
    world.Step(1.0 / 60.0);
}
Console.WriteLine(body.position.y.ToString());
```

坐标约定：y 轴向上，重力默认 `(0, -10)`，长度单位由调用方决定（像素或米）。

## 关节

- `RevoluteJoint(bodyA, bodyB, worldAnchor)`：把两个刚体上的同一点钉在一起，
  只约束平移（摆、轮子、开门）。
- `DistanceJoint(bodyA, bodyB, anchorA, anchorB, length)`：保持定距；
  `frequency > 0` 时变成软约束（Hz + 阻尼比），比裸 stiffness 好调。

关节与碰撞**并存**：被约束的两个刚体之间仍会生成接触——多数关节需要
"扣在一起"，接触与约束同时存在反而更稳。

## 已知边界

- 只支持凸多边形：凹多边形在**构造时**被拒绝（抛 `ArgumentException`），
  不会进入求解器——SAT 对凹多边形没有唯一分离轴，硬算只会给出错值。
- 没有 TOI 扫掠：速度极高（每步位移大于物体尺寸）时仍可能穿过薄壁。
  调 `world.substepCount` 或减小步长可以缓解。
- 角-角接触（两个尖角相碰）退化成单点近似接触，不是精确的边-边接触。
- 睡眠（sleeping）没有实现：静止物体照样每帧参与求解。
- 连续碰撞检测（CCD）没有实现。

## 契约测试

`tests/conformance/physics2d_world.zan` 覆盖：自由落体积分、落地静止、
弹性反弹、库仑摩擦、旋转多边形的 SAT、穿透修正、铰链与定距关节、
凹多边形被拒绝、以及"同一场景跑两遍逐位一致"的确定性断言。
