# Zan.Game.Server

高并发、低延迟游戏服务端核心架构包（High-Concurrency Game Server Framework for Zan）。

专为实时多人对局、房间制匹配、分布式场景大世界等强状态、长连接网络游戏设计。提供从**底层封包与断线重连网关**、**单线程无锁 Actor 并发系统**、**分布式集群位置透明路由**，到**空间九宫格 AOI 广播裁剪**、**对局房间定频 Tick 驱动**以及**状态三级放置合并回写队列**的全链路解决方案。

---

## 核心特性

1. **Network 网关接入层 (`Game.Server.Network`)**
   - `Packet`: 标准游戏二进制通信报文结构（Header 20 字节，包含 `totalLength`, `opCode`, `seq`, `routeId` 与负载）。
   - `PacketFramer`: 流式分帧编解码器，大端序编码，自动处理 TCP 粘包与半包拆包，内存零不必要分配。
   - `Session`: 客户端长连接会话实体，支持心跳保活、离线未确认报文暂存缓冲（Outbox）以及优雅断线重连（Resume by Token）。
   - `SessionManager`: 高并发会话管理器，提供双向 O(1) 检索、心跳超时巡检踢人、断线保持窗口管理。

2. **Core 无锁 Actor 并发系统 (`Game.Server.Core`)**
   - `Actor`: 游戏业务实体基类（玩家实体、公会实体、场景实体等）。
   - `Mailbox`: 环形私有信箱队列。
   - `ActorSystem`: 单节点 Actor 调度中心。在调度器的分发下，单个 Actor 的 `OnReceive` 绝对串行执行，**业务逻辑无需编写任何互斥锁**，彻底根除死锁与竞态。

3. **Cluster 集群位置透明路由 (`Game.Server.Cluster`)**
   - `LocationService`: 全局 Actor 位置注册服务，维护 `ActorId -> NodeId` 与租约版本，支持跨场景原子迁移（Migrate）。
   - `ClusterRouter`: 跨节点透明路由分发器。调用方直接 `Route(msg)`，本地实体走 `ActorSystem` 投递，跨节点实体自动打包为集群内部 RPC 压入远端出站队列。

4. **Aoi 空间视野管理 (`Game.Server.Aoi`)**
   - `AoiGrid`: 2D 网格九宫格视野裁剪算法。
   - 将广播复杂度从全员广播的 $O(N^2)$ 降为局部格子的 $O(1)$。
   - `Move` 时自动精确计算跨格子差集（`enters` 进入视野实体集、`leaves` 离开视野实体集），实现 0 冗余精准视野同步。

5. **Room 房间与对局调度 (`Game.Server.Room`)**
   - `Room`: 房间生命周期管理（等待、准备就绪检测、对局进行中、结算关闭）。
   - 定频逻辑步进 `Tick(deltaMs)`，驱动帧号推进，天然无缝对接 `Zan.Game.Netcode`。
   - `RoomManager`: 房间池与快速匹配调度。

6. **Storage 状态三级回写队列 (`Game.Server.Storage`)**
   - `WriteBackQueue`: 内存权威模式下的防抖与合并异步回写队列。
   - 内存读写低至微秒，通过脏标记（DirtyMask）原位合并高频修改（同一玩家 1 秒内变更 100 次只保留最新 1 条记录），根据防抖时间与最大延迟批量导出落盘，兼顾极速响应与防回档。

---

## 快速上手

### 1. 创建 Actor 接收并处理消息

```zan
using System;
using Game.Server.Core;

public class PlayerActor : Actor {
    public int score;

    public PlayerActor(long id) : base(id, 1) {
        this.score = 0;
    }

    public override void OnReceive(ActorMessage msg) {
        if (msg.msgType == 1001) {
            this.score = this.score + 10;
        }
    }
}
```

### 2. 调度与透明路由

```zan
ActorSystem system = new ActorSystem();
PlayerActor player = new PlayerActor(10001L);
system.Spawn(player);

LocationService location = new LocationService();
location.Register(10001L, 1, 1, 1000L);

ClusterRouter router = new ClusterRouter(1, system, location);
router.Route(ActorMessage.Create(1001, 0L, 10001L, null));

// 驱动消息轮询分发
system.Dispatch(16);
```

### 3. 九宫格 AOI 视野跟踪

```zan
AoiGrid grid = new AoiGrid(0.0f, 0.0f, 1000.0f, 1000.0f, 100.0f);
grid.Enter(1L, 50.0f, 50.0f);
grid.Enter(2L, 60.0f, 60.0f);

// 2 号实体在 1 号实体的九宫格视野内
long[] surrounds = grid.GetSurroundEntities(grid.ToCellX(50.0f), grid.ToCellY(50.0f), 1L);
```
