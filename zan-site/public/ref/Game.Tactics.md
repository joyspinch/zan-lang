# Game.Tactics

> 源码: `packages/Zan.Game/src/Game/Tactics/BulletPool.zan`, `packages/Zan.Game/src/Game/Tactics/FlowField.zan`, `packages/Zan.Game/src/Game/Tactics/RvoAvoidance.zan`, `packages/Zan.Game/src/Game/Tactics/Tower.zan`, `packages/Zan.Game/src/Game/Tactics/WaveSpawner.zan`


## BulletPool (class)

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

- int[]freeStack;

- int freeTop;

- int[]activeList;

- int[]activePos;

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

- void Update(double dt, BulletHitFn onHitCallback)

- void DespawnAt(int ai)

- void Kill(int idx)


## FlowField (class)

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

- void SetCost(int col, int row, byte cost)

- bool IsBlocked(int col, int row)

- void SetTarget(int col, int row)

- void Rebuild()

- void SampleWorldVelocity(double wx, double wy, out float vx, out float vy)


## RvoSimulator (class)

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

- void RemoveAgent(int slot)

- void SetPrefVelocity(int slot, double pvx, double pvy)

- double GetX(int slot)

- double GetY(int slot)

- double GetVx(int slot)

- double GetVy(int slot)

- void Step(double dt)


## Tower (class)

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

- void AimAt(double targetX, double targetY)

- void TriggerFire()


## WaveSpawner (class)

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

- void Update(double dt, int aliveMonsterCount, SpawnMonsterFn onSpawnMonster, WaveCompleteFn onWaveCompleted)


## void (delegate)

`delegate void BulletHitFn(int bulletIndex, double x, double y, double damage, double splashRadius);`


## void (delegate)

`delegate void SpawnMonsterFn(int wave, int mobType);`


## void (delegate)

`delegate void WaveCompleteFn(int wave);`


## BulletType (enum)

- Normal

- Homing

- Artillery

- Laser = 瞬间贯通激光射线


## TargetPriority (enum)

- First

- Closest

- Strongest

- Weakest = 残血收割
