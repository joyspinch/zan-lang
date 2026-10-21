# Game.Physics3D

`Game.Physics3D` 是 3D 刚体动力学层：球 / 盒形状、四元数姿态、SAT 窄相
（含面裁剪多接触点）与顺序冲量求解器。

它补的是 `Game.Physics2D` 之上缺的那一层——**三维**：自由度的姿态表示、
面-面接触的多点求解、盒-盒的 SAT 分离轴。

## 设计取舍

- **double 而不是 float**：物理里累加惯量与冲量时 float 的舍入会让
  "能量守恒"这类断言在边界上飘——同样的两次运行结果不同就谈不上确定性。
  物理用 double，渲染时再转 float。
- **四元数而不是欧拉角**：欧拉角有万向锁与顺序歧义，插值时会翻面。
  四元数乘法无这些毛病，代价是要归一化（每步积分后归一化一次）。
- **零反射、编译期定型**：形状是类（`SphereShape` / `BoxShape`），
  求解器只认这两个具体类型；没有"按类型名查形状"的运行时字典。
- **确定性**：接触顺序、迭代次数全部固定，没有随机数——同一初始条件 +
  同一操作序列必然得到同一结果（回放 / 锁步的前提）。宽相的排序是插入
  排序，顺序稳定；接触对按 id 规范化（低 id → 高 id），不会因为两个刚体
  在宽相里换了先后就建出两个方向相反的接触。
- **盒-盒走完整 SAT + 面裁剪**：最小穿透轴是面法线时，把另一个盒朝向它
  的那一面裁剪到参考面范围内，最多 4 个接触点；是边-边轴时取两条边的
  最近点。
- **多接触点不是为了好看**：单点接触下，滑动摩擦作用在质心下方会产生
  "把盒子掀翻"的力矩，单点的法向冲量没法在面内重新分配来抵抗它——盒子
  会越滑越歪、最后沉进地里。多点接触让法向冲量在面上重新分布，抵消
  倾覆力矩，盒子才能平地跑、三盒堆叠不塌（Box2D / Bullet 稳定堆叠的
  同一个机制）。
- **推测接触代替 TOI 扫掠**：分离量在 `speculativeMargin` 以内也建接触，
  约束把接近速度限制为 `gap / dt`——高速物体在"还没撞上"时就被拦住。
  真正的 TOI（time of impact）扫掠没有实现。
- **穿透修正用分离冲量**：位置修正走伪速度（split impulse），不把能量
  注入真实速度，堆叠物体因此不会"越修越弹"。

## 能力

| 能力 | 入口 |
|---|---|
| 形状 | `SphereShape(radius)`、`BoxShape(halfX, halfY, halfZ)` / `BoxShape.FromSize(...)` |
| 数学 | `Vec3`（值语义）、`Quat`（单位四元数）、`Aabb` |
| 刚体 | `RigidBody.Dynamic` / `RigidBody.Static`，质量与转动惯量按体密度算 |
| 世界 | `PhysicsWorld`：`AddBody` / `RemoveBody` / `Step(dt)` |
| 窄相 | `Collision.Collide(...)` → `Manifold`（法线、接触点、分离量） |
| 宽相 | SAP（按 minX 插入排序，帧间增量维护） |
| 材质 | 每个刚体各自的 `restitution` / `friction`；接触取几何平均（摩擦）与较大者（弹性） |

## 用法

```zan
using System;
using Game.Physics3D;

PhysicsWorld world = new PhysicsWorld();
world.gravity = new Vec3(0.0, -10.0, 0.0);

world.AddBody(RigidBody.Static(
    BoxShape.FromSize(20.0, 1.0, 20.0), new Vec3(0.0, -0.5, 0.0)));

SphereShape ball = new SphereShape(0.5);
RigidBody body = world.AddBody(
    RigidBody.Dynamic(ball, new Vec3(0.0, 5.0, 0.0), 1.0));
body.restitution = 0.3;
body.friction = 0.4;

for (int i = 0; i < 120; i = i + 1) {
    world.Step(1.0 / 60.0);
}
Console.WriteLine(body.position.y.ToString());
```

坐标约定：y 轴向上，重力默认 `(0, -10, 0)`，长度单位由调用方决定
（像素或米）。刚体的 `position` 永远是**形状质心**的世界坐标（球与盒的
质心都在局部原点），姿态用 `orientation`（单位四元数）。

## 惯量张量

形状只提供**局部主轴系下的对角分量**（`Shape.InertiaPerMass()`），世界系
下按 `R·I⁻¹·Rᵀ` 变换（`RigidBody.ApplyInvInertia`）。这样盒体绕任意轴的
转动惯量是自动正确的，不用为每种姿态单独存一张张量。

## 接触流形

`Manifold` 最多 4 个接触点：

- 球-球 / 球-盒：1 个点（球面接触本就只有一点）。
- 盒-盒面-面：4 个点（裁剪出来的）。
- 盒-盒边-边：1 个点（两条边的最近点）。

`manifold.normal` 永远从 A 指向 B，`points[i].separation` 为负表示已侵入。

## 已知边界

- 没有关节：3D 版本没有做 `RevoluteJoint` / `DistanceJoint` 之类的约束
  （那是独立的一块工作量，2D 版可以先顶）。
- 没有 TOI 扫掠：速度极高（每步位移大于物体尺寸）时仍可能穿过薄壁。
  调 `world.substepCount` 或把 `speculativeMargin` 调大可以缓解。
- 盒-球只有 1 个接触点：盒面压在球上时，单点接触没法让法向冲量在盒面
  内重新分配，盒会慢慢歪。
- 睡眠（sleeping）没有实现：静止物体照样每帧参与求解。
- 连续碰撞检测（CCD）没有实现。
- 没有凸包（convex hull）形状：只有球与盒。

## 契约测试

`tests/conformance/physics3d_world.zan` 覆盖：向量与四元数代数、包围盒、
三种窄相配对的几何、自由落体积分、落地静止与不漂移、动量守恒、能量不
增、弹性反弹高度、偏心碰撞产生角速度、库仑摩擦（减速度与滑行距离对着
解析解比）、三盒堆叠不塌不歪、静态体不动、高速物体不穿墙、移除刚体连带
清掉接触、以及"同一场景跑两遍逐位一致"的确定性断言。
