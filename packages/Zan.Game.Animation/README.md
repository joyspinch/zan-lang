# Game.Animation

`Game.Animation` 是 3D 动画运行时：**TRS 变换 + 骨骼层级 + 线性混合蒙皮 + 关键帧片段 + 播放器 + 状态机 + 两骨解析 IK**。

它补的是 `Game.Graphics` 里那点薄薄的动画支持（`AnimPlayer` 走 2D 帧表、
`Animation3DTrack` 只有加权轨道、`PaperdollRenderer` 只能换贴图）缺的那一层：
**蒙皮 + 状态机混合 + IK**——也就是 3D 角色真正能动起来的那三件事。

## 设计取舍

- **纯变换，不碰矩阵**：`System.Numerics` 的 `Matrix4x4` 缺通用求逆与四元数
  构造，拿它做蒙皮得自己补一整套矩阵层。本包改走 TRS 路线：骨骼姿势就是
  平移 + 旋转 + 缩放，组合与求逆都在 `Transform3D` 里闭合。代价写在下面
  的"已知边界"里。
- **姿势是值语义**：`Transform3D` 是 struct，逐帧传值没有堆分配；每根骨骼
  每帧算一次变换是常态，分配会直接变成帧率抖动。
- **绑定姿势是蒙皮的分母**：蒙皮矩阵 = 当前世界姿势 × 绑定世界姿势的逆。
  绑定姿势改了必须重算（`SetBindPose` 自动标脏），否则蒙皮会把顶点送回
  旧姿势——症状是"改了模型但蒙皮没变"。
- **姿势按骨骼名索引**（片段里），不按下标：动画资产来自 DCC 工具
  （Blender / Maya / Mixamo），那里的引用单位是骨骼名。
- **片段只描述"动的部分"**：某根骨骼在片段里没有轨道时，采样回落到它的
  绑定姿势，而不是单位变换。
- **状态机自持参数**：浮点 / 布尔参数与一次性触发器都在状态机里，转移条件
  由参数驱动，调用方不需要另建一层"条件系统"。
- **IK 是解析解**：两骨 IK 用余弦定理一次算完，不用迭代收敛，也不像 CCD
  那样"迭代次数不够就差一点"。够不着时把距离钳到边界（手臂伸直指向目标），
  不炸、不抖。
- **非线程安全**：面向单线程游戏主循环。要跨线程由调用方加锁（锁的粒度
  该由调用方决定，库不替你猜）。

## 能力

| 能力 | 入口 |
|---|---|
| 变换 | `Transform3D`：`Identity` / `Compose` / `Inverse` / `LocalFromWorld` / `TransformPoint` / `TransformDirection` |
| 骨架 | `Skeleton`：`AddBone` / `SetBindPose` / `ResetToBindPose` / `SetLocalPose` / `SetWorldPose` / `ComputeWorldPoses` / `SkinMatrix` |
| 骨骼 | `Bone`：名字 / 父下标 / 绑定姿势 / 局部姿势 / 世界姿势 |
| 片段 | `AnimationClip` + `AnimationTrack`：三通道关键帧、线性插值与球面插值 |
| 播放 | `AnimationPlayer`：推进 / 循环 / 变速 / 播完停止 |
| 混合 | `PoseBuffer`：姿势缓冲与交叉淡化混合 |
| 状态机 | `AnimationStateMachine` + `AnimationState` + `AnimationTransition`：参数触发 + 交叉淡化 |
| 蒙皮 | `SkinMesh`：四槽权重、线性混合蒙皮、权重归一 |
| IK | `TwoBoneIk`：`SolveMidPosition` / `Solve` / `RotationFromDirection` |
| 错误 | `AnimationException`（越界 / 重名 / 顺序错 / 几何退化一律当场报错） |

## 用法

```zan
using System;
using System.Numerics;
using Game.Animation;

// 1. 建骨架（父在子前）
Skeleton skel = new Skeleton();
skel.AddBone("root", -1);
skel.AddBone("hip", 0, new Transform3D(new Vector3(0.0f, 1.0f, 0.0f), Quaternion.Identity, Vector3.One));
skel.AddBone("knee", 1, new Transform3D(new Vector3(0.0f, 1.0f, 0.0f), Quaternion.Identity, Vector3.One));

// 2. 做一段动作片段
AnimationClip walk = new AnimationClip("walk", 1.0f);
walk.AddPositionKey("root", 0.0f, Vector3.Zero);
walk.AddPositionKey("root", 1.0f, new Vector3(2.0f, 0.0f, 0.0f));
walk.AddRotationKey("hip", 0.0f, Quaternion.Identity);
walk.AddRotationKey("hip", 1.0f, Quaternion.FromAxisAngle(Vector3.UnitY, 1.5707963f));

// 3. 播放
AnimationPlayer player = new AnimationPlayer(skel);
player.Play(walk);
player.Update(0.016f);          // 每帧推进，姿势写回骨架

// 4. 蒙皮（顶点是绑定姿势下的坐标）
SkinMesh mesh = new SkinMesh();
mesh.AddVertexInfluenced(new Vector3(0.0f, 2.0f, 0.0f), 1, 0.75f, 2, 0.25f);
Vector3 posed = mesh.SkinVertex(skel, 0);
```

状态机 + 交叉淡化：

```zan
AnimationStateMachine sm = new AnimationStateMachine();
sm.AddState("idle", idleClip);
sm.AddState("run", runClip);
sm.AddTransition(AnimationTransition.OnFloat("idle", "run", "speed", 1.0f, 0.3f));  // 0.3 秒淡化
sm.SetState("idle");

sm.SetFloat("speed", 2.0f);
sm.Update(skel, 0.016f);       // 触发转移并开始交叉淡化
```

两骨 IK（末端落在目标上）：

```zan
// root → upper → fore；极向量决定弯曲平面的朝向
TwoBoneIk.Solve(skel, 0, 1, 2, new Vector3(1.0f, 1.0f, 0.0f), new Vector3(0.0f, 0.0f, 1.0f));
```

## 约定（对接模型前先看这里）

- **骨骼局部 +Y 指向骨骼末端**（Unity 约定）。两骨 IK 与骨长换算都按这个
  走；模型用别的约定时在导入阶段把骨骼旋转让对齐，别在运行时改。
- **骨骼加入顺序是父在子前**。`AddBone` 遇到父下标越界直接报错——层级顺序
  错乱会让世界姿势递推少算一截，症状是"某根骨飘了"，很难查。
- **变换约定**：点 `p' = R * (S * p) + T`，方向 `d' = R * (S * d)`；
  `Compose(parent, child)` 表示先施加 child 再施加 parent。

## 已知边界

- **TRS 在"父级非等比缩放 + 子级带旋转"时无法精确分解**（缩放与旋转不可
  交换，业界通病）。骨骼缩放请用等比缩放；确有需要时在自己的渲染层用矩阵
  承接。
- **不做曲线缓动**：关键帧之间一律线性插值（旋转走球面插值）。要缓动得
  在导入阶段把缓动烘进关键帧。
- **不做动画层（layer）与遮罩（mask）**：状态机是单层 + 交叉淡化。
- **IK 只有两骨**：三骨以上要自己迭代求解。
- **蒙皮是线性混合（LBS）**，不做 Dual Quaternion 蒙皮——极端弯曲处会有
  体积塌陷。
- **不做 DCC 导入器**：片段与网格由外部工具导出成数据，库里只消费。

## 验证

`tests/conformance/animation_pipeline.zan` 覆盖：变换的单位性 / 组合顺序 /
父级缩放对子级位移的影响 / 求逆回代 / 方向不含平移；骨架的重名与越界报错 /
世界姿势递推 / 绑定姿势重置 / 世界姿势反解局部姿势；蒙皮的绑定姿势不动点 /
位移精确落位 / 旋转保半径；片段的关键帧插值 / 时长自动延长 / 无轨道回落绑定
姿势 / 越界钳位 / 球面插值 / 180° 插值方向确定 / 乱序关键帧报错 / 姿势缓冲
长度校验；播放器的推进 / 循环回绕 / 变速 / 播完停止；状态机的浮点与布尔参数
触发 / 触发器一次性 / 交叉淡化的权重曲线 / 未知状态报错；蒙皮网格的权重混合
与归一；两骨 IK 的末端命中目标 / 骨长不变 / 够不着时朝向不歪。

## 放置

游戏包约定：`packages/Zan.Game.Animation/src/Game/Animation/*.zan`。
引擎级能力（物理 / 渲染 / ECS / 资源）走独立包并单独版本化，不进 stdlib。
