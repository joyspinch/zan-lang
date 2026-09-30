# Game.Tactics

> 源码: `packages/Zan.Game/src/Game/Tactics/BulletPool.zan`, `packages/Zan.Game/src/Game/Tactics/FlowField.zan`, `packages/Zan.Game/src/Game/Tactics/RvoAvoidance.zan`, `packages/Zan.Game/src/Game/Tactics/Tower.zan`, `packages/Zan.Game/src/Game/Tactics/WaveSpawner.zan`


## BulletPool (class)

零 GC 内存预分配弹道投射物对象池。
支持单机千枚子弹高频模拟，支持直线投射、追踪制导与范围溅射爆炸。

- int capacity;

- int activeCount;

- bool[]active;

- int[]bulletType;

- double[]posX;

- double[]posY;

- double[]velX;

- double[]velY;

- int[]targetId;

- double[]targetX;

- double[]targetY;

- double[]damage;

- double[]splashRadius;

- double[]lifeTime;

- BulletPool(int cap)

- static BulletPool Create(int capacity)

- int Capacity { get }

- int ActiveCount { get }

- bool IsActive(int idx)

- int GetType(int idx)

- double GetX(int idx)

- double GetY(int idx)

- double GetDamage(int idx)

- double GetSplash(int idx)

- int Spawn(BulletType type, double spawnX, double spawnY, int tgtId, double tgtX, double tgtY, double speed, double dmg, double splashR, double maxLife)
  - 发射一枚直线或追踪子弹。

- void Update(double dt, BulletHitFn onHitCallback)
  - 逐物理步长更新所有活动子弹位置，返回命中目标的子弹索引委托回调。

- void Kill(int idx)


## FlowField (class)

海量单位集群流场寻路系统（Flow Field Pathfinding）。
解决红警、帝国时代、千万塔防怪群的寻路 CPU 爆炸问题。
算法原理：
1. 代价图（Cost Field）：障碍物与地形通行阻力。
2. 集成场（Integration Field）：以目标点为基准的波前扩散（Dijkstra/BFS）。
3. 向量场（Vector Field / Flow Field）：每个格子预先计算出指向最小代价邻居的移动方向向量。
无论视野内有 100 只怪还是 10,000 只怪，每个单位只需要 O(1) 地读取所在格子的向量即可完美避障并直奔目标！

- int cols;

- int rows;

- int cellSize;

- byte[]costField;

- int[]integrationField;

- float[]flowDirX;

- float[]flowDirY;

- int[]queueX;

- int[]queueY;

- int queueHead;

- int queueTail;

- int queueCap;

- int targetCol;

- int targetRow;

- bool isDirty;

- FlowField(int cols, int rows, int cellSize)

- static FlowField Create(int cols, int rows, int cellSize)

- int Cols { get }

- int Rows { get }

- int CellSize { get }

- int Index(int c, int r)

- void ResetCost()
  - 重置整个地图代价（默认全部通行代价为 1）。

- void SetCost(int col, int row, byte cost)
  - 设置指定格子的代价（255 代表不可通行的墙壁或建筑物）。

- bool IsBlocked(int col, int row)
  - 判断指定格子是否为障碍物。

- void SetTarget(int col, int row)
  - 指定流场汇聚的目的地（例如水晶、基地、领袖角色）。

- void Rebuild()
  - 重构集成场与向量场。
    只有在障碍物发生变化或目标移动时才需调用。

- void SampleWorldVelocity(double wx, double wy, out float vx, out float vy)
  - O(1) 采样任意世界坐标处的流场速度方向向量。
    输出单位化向量 (vx, vy)。


## RvoSimulator (class)

RTS 海量单位集群互斥避障（Reciprocal Velocity Obstacles - RVO2 / ORCA）模拟系统。
彻底解决红警、帝国时代、魔兽与塔防中“万人行军重叠穿模、窄道剧烈抖动卡死、原地互不相让”等运动学顽疾。

- int maxAgents;

- int agentCount;

- bool[]active;

- int[]agentIds;

- double[]posX;

- double[]posY;

- double[]velX;

- double[]velY;

- double[]prefVx;

- double[]prefVy;

- double[]radius;

- double[]maxSpeed;

- SpatialHash2D spatialGrid;

- int[]neighborBuf;

- int maxNeighbors;

- RvoSimulator(int capacity, int cellSize)

- static RvoSimulator Create(int capacity, int cellSize)

- int AgentCount { get }

- int Capacity { get }

- int AddAgent(double x, double y, double r, double speed)
  - 添加或重用一个 RVO 运动体。

- void RemoveAgent(int slot)

- void SetPrefVelocity(int slot, double pvx, double pvy)

- double GetX(int slot)

- double GetY(int slot)

- double GetVx(int slot)

- double GetVy(int slot)

- void Step(double dt)
  - 逐物理步更新互斥避障计算并推进位置。


## Tower (class)

通用防御塔基类。
内置射程检测、冷却倒计时、索敌策略与开火动画角度解算。

- int id;

- int towerType;

- int level;

- double x;

- double y;

- double range;

- double fireRate;

- double damage;

- int cost;

- double cooldown;

- int currentTargetId;

- double aimAngleRad;

- TargetPriority priority;

- Tower(int id, int type, double x, double y, double range, double fireRate, double damage, int cost)

- static Tower Create(int id, int type, double x, double y, double range, double fireRate, double damage, int cost)

- int Id { get }

- int TowerType { get }

- int Level { get }

- double X { get }

- double Y { get }

- double Range { get }

- double Damage { get }

- double AimAngle { get }

- int TargetId { get }

- bool CanFire { get }

- void SetPriority(TargetPriority p)

- void Upgrade(double rangeMul, double damageMul, double speedMul, int upgradeCost)

- void Update(double dt)
  - 更新冷却时间，并向目标旋转炮口。

- void AimAt(double targetX, double targetY)
  - 将炮口对准目标实体位置。

- void TriggerFire()
  - 触发开火，重置冷却时间。


## WaveSpawner (class)

塔防与战术游戏波次控制器。
控制怪物定时生成波次、关卡难度递增与通关奖励触发。

- int currentWave;

- int totalWaves;

- bool isWaveActive;

- int monstersInCurrentWave;

- int monstersSpawned;

- double spawnInterval;

- double spawnTimer;

- double wavePreparationTimer;

- double preparationDuration;

- WaveSpawner(int totalWaves, double prepDuration)

- static WaveSpawner Create(int totalWaves, double prepDuration)

- int CurrentWave { get }

- int TotalWaves { get }

- bool IsWaveActive { get }

- double PrepTimeLeft { get }

- void StartNextWaveImmediately()
  - 跳过准备倒计时，立即开启下一波怪物进击。

- void Update(double dt, int aliveMonsterCount, SpawnMonsterFn onSpawnMonster, WaveCompleteFn onWaveCompleted)
  - 逐物理帧推进波次状态机。
    当需要生成怪物时触发 onSpawnMonster 委托。


## void (delegate)

子弹命中回调：子弹槽位、命中点 x/y、伤害、溅射半径。

`delegate void BulletHitFn(int bulletIndex, double x, double y, double damage, double splashRadius);`


## void (delegate)

生成怪物回调：波次号、怪物类型。

`delegate void SpawnMonsterFn(int wave, int mobType);`


## void (delegate)

波次完成回调：波次号。

`delegate void WaveCompleteFn(int wave);`


## BulletType (enum)

子弹弹药类型。

- Normal

- Homing

- Artillery

- Laser = 瞬间贯通激光射线


## TargetPriority (enum)

防御塔索敌策略。

- First

- Closest

- Strongest

- Weakest = 残血收割
