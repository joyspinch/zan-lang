# Game.Particles

`Game.Particles` 是统一的粒子系统：对象池化的粒子、按速率/突发的发射器、
曲线驱动的尺寸与颜色、确定性噪声湍流。

仓库里 13 套游戏模板各自实现了一套粒子（`templates/game/*/src/.../Particle.zan`
+ `idle` 模板的 `ParticleSystem.zan`），逻辑重复且参数口径不一。本包把它们
收敛成一份可复用实现。

## 能力

| 能力 | 入口 | 说明 |
|---|---|---|
| 粒子 | `Particle` | 位置/速度/旋转/寿命/尺寸/颜色；`CurrentSize()` / `CurrentColor()` 给渲染方读 |
| 发射器 | `EmitterConfig` / `Emitter` | 速率发射 + 突发发射；扇形角度、速度/寿命/尺寸区间、起止色 |
| 曲线 | `Curve` | 关键帧曲线（线性 / smoothstep 插值），驱动尺寸、透明度等 |
| 噪声 | `Noise` | 确定性值噪声（2D、含分形叠加），用于湍流与发射点漂移 |
| 随机源 | `Rng` | 带种子的随机源（取 LCG 高位，避免低位带状分布） |
| 系统 | `ParticleSystem` | 对象池 + 积分 + 回收；容量硬上限 |

## 用法

```zan
using System;
using Game.Particles;

ParticleSystem system = new ParticleSystem(2000, 20261022);
system.gravityY = -300.0;

EmitterConfig config = new EmitterConfig();
config.rate = 60.0;            // 每秒 60 个
config.burst = 24;             // Trigger() 时一次 24 个
config.lifetimeMin = 0.4;
config.lifetimeMax = 0.9;
config.speedMin = 80.0;
config.speedMax = 200.0;
config.angleDeg = 90.0;        // 向上
config.spreadDeg = 30.0;
config.sizeMin = 6.0;
config.sizeMax = 14.0;
config.colorStart = 0xFFFFE082;
config.colorEnd = 0x00FF7043;
config.noiseAmplitude = 60.0;  // 湍流
config.noiseFrequency = 0.05;

Curve sizeCurve = new Curve();
sizeCurve.AddKey(0.0, 1.0);
sizeCurve.AddKey(1.0, 0.2);
config.sizeCurve = sizeCurve;

Emitter emitter = new Emitter(config);
emitter.x = 100.0;
emitter.y = 200.0;
emitter.emitting = true;
system.AddEmitter(emitter);

// 每帧
system.Update(dt);

// 渲染（引擎相关，调用方自己做）
for (int i = 0; i < system.Count(); i = i + 1) {
    Particle p = system.Get(i);
    // p.x / p.y / p.CurrentSize() / p.CurrentColor() / p.rotation
}
```

## 设计取舍

- **引擎无关**：本包不依赖 `Gui` / `SpriteBatch`，只产出粒子数据。渲染由
  调用方做——包可以无头跑、可以在 conformance 里断言。
- **对象池**：粒子用完回收复用，运行期不分配。容量是硬上限（满了就不再
  发射），内存占用有界。
- **确定性**：随机源带种子，噪声是纯函数（同一坐标永远同一值）——同样的
  种子 + 同样的调用序列给出同样的粒子状态，回放 / 锁步可复现。随机数取
  LCG 的**高位**（低位周期太短，会出现可见的带状分布）。
- **积分器是半隐式欧拉**（先更新速度再更新位置）：粒子多时它比显式欧拉
  稳定，代价是对连续解有系统性偏差（自由落体 0.5 秒得 -12.9167 而不是
  -12.5）。
- **有序移除**：死亡粒子按顺序移除，渲染顺序 = 出生顺序，叠加混合的视觉
  结果稳定。

## 已知边界

- 没有 GPU 粒子（模拟与渲染分离，渲染侧可以自己批量提交）。
- 没有碰撞（粒子撞到东西要由调用方处理）。
- 噪声是值噪声不是梯度噪声（没有解析导数，做不了流体）。
- 池里的粒子不跨系统共享。

## 契约测试

`tests/conformance/particles_system.zan` 覆盖：速率发射、突发发射、寿命到期
回收、重力与阻力积分、容量上限与对象池、确定性、曲线求值与钳位、噪声的
确定性与范围、曲线驱动的尺寸与颜色插值。
