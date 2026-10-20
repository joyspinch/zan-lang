# Game.Common

`Game.Common` 是游戏玩法的**公共基座**：**存档**、**飘字**、**血条**、
**冷却条**、**背包格**、**小地图**。

它补的是 16 套游戏模板之间反复重写的那部分代码——每个模板都自带一份
`Profile`（手搓 key=value 文本解析）、一份 `Popup`（裸字段的飘字）、
一份自己的背包格和冷却逻辑。换个游戏就抄一遍，抄出三种口径。

## 设计取舍

- **模型与渲染分离**：本包只产出数据（位置、比例、剩余时间、槽位状态），
  不碰 Gui、不碰 SpriteBatch。渲染层拿数据自己画——所以这些逻辑能
  **无头跑**、能被 conformance 逐条断言，也能换任何渲染后端。
- **存档：JSON + 版本标记 + 原子替换**：走 `JsonValue` 不拼字符串；
  带 `format` 标记，版本不符**拒绝加载**（旧档不该被新代码悄悄按新语义
  改写）；写盘先写 `.tmp`、读回校验通过才顶替正式文件，替换前留 `.bak`。
- **存档损坏不毁档**：读不出来就返回 `false` + 原因，**内存不清空、
  磁盘不删**——玩家可能手动抢救，程序无权替他销毁。
- **血条：受伤才有滞后，回血立即跟随**：白色残影只在 `display > current`
  时按 `lagRate` 追赶；回血时显示值立刻跟上。回血还慢慢爬，看着像掉血。
- **冷却条口径只有一个**：`Ratio()` 是**剩余**比例（1 = 刚按下，
  0 = 就绪），UI 不用再猜方向；`Progress()` = 1 - `Ratio()`。
- **背包：先堆叠后开格**：先把已有同类槽位填满，再用空槽开新堆——反过来
  会让 20 格背包塞 3 种道具就满。空 = 没有物品 id（数量归零时 id 一并
  清掉），不存在"数量 0 但 id 非空"的第三种状态。
- **小地图：视口左上角为原点**：跟随玩家时只改视口中心，别的标记不会
  整体飘走；视口尺寸 <= 0 表示看整个世界。标记是**语义类别**（敌人 /
  友军 / 目标），颜色由渲染层查表。
- **容量硬上限 + 满了不新增**：飘字层与标记层满了就丢弃并计数
  （`DroppedCount()`）——静默吞掉会让"飘字偶尔不出现"这类 bug 查不出来。
- **非线程安全**：面向游戏主循环；跨线程由调用方加锁（锁的粒度该由
  调用方决定，库不替他猜）。

## 能力

| 能力 | 入口 |
|---|---|
| 存档 | `ProfileStore`：`SetInt/SetString/SetBool/SetDouble` + `GetXxx` / `Save` / `Load` / `RestoreBackup` / `LastError` |
| 飘字 | `FloatText`、`FloatTextLayer`（池化 + 容量上限 + `SpawnDamage/SpawnHeal/SpawnGold/SpawnCombo`） |
| 血条 | `HealthBar`：`Damage` / `Heal` / `Step` / `Ratio` / `DisplayRatio` / `IsGhosting` |
| 冷却条 | `Cooldown`：`Start` / `Step` / `Ready` / `Ratio` / `Progress` / `WithCharges` |
| 背包格 | `InventorySlot`、`Inventory`（先堆叠后开格 / `Add` / `Remove` / `ToJson` / `FromJson`） |
| 小地图 | `Minimap`（世界→像素映射 / `Contains`）、`MinimapEntity`、`MinimapLayer`（帧内复用 + 视口剔除） |

## 用法

存档：

```zan
using Game.Common;

ProfileStore store = new ProfileStore("save/profile.json");
store.SetInt("high", 1234);
store.SetString("name", "张三");
if (!store.Save()) {
    Console.WriteLine(store.LastError());   // 失败可区分
}

// 读档：文件不存在 = 全新档（不算错）；损坏/版本不符 = 拒绝，且不动原文件
ProfileStore loaded = new ProfileStore("save/profile.json");
if (loaded.Load()) {
    int high = loaded.GetInt("high", 0);
}
```

血条与冷却：

```zan
HealthBar hp = new HealthBar(100.0).WithLag(50.0);   // 50 点/秒的残影追赶
hp.Damage(30.0);
// 每帧：hp.Step(dt);  UI：hp.Ratio() / hp.DisplayRatio()

Cooldown cd = new Cooldown(10.0).WithCharges(2);
if (cd.Ready() && cd.Start()) {
    // 释放技能
}
// 每帧：cd.Step(dt);  UI：cd.Ratio()（剩余比例）
```

背包：

```zan
Inventory bag = new Inventory(20);
int leftover = bag.Add("potion", 5, 3);   // 先堆叠后开格：返回没装下的数量
bag.Remove("potion", 2);
string json = bag.ToJson();
```

小地图：

```zan
Minimap map = new Minimap(1000.0, 1000.0, 100.0, 100.0);
map.WithView(playerX, playerY, 200.0, 200.0);   // 跟随玩家
MinimapLayer layer = new MinimapLayer(map, 256);
// 每帧：
layer.BeginFrame();
layer.Add(enemyX, enemyY, 1);        // 1 = 敌人
int visible = layer.VisibleCount();  // 视口内的可见标记数
```

## 已知边界

- **没有加密存档**：本包提供的是明文 JSON 存档。需要防篡改（本地单机
  场景常见）时，自行套一层加密/签名——`ProfileStore` 的
  写入是原子的，换成密文只需要替换序列化与反序列化那两步。
- **没有多槽位存档管理**：一个 `ProfileStore` 对应一个文件。多槽位
  （存档 1/2/3）由调用方建多个实例。
- **背包不支持装备位 / 权重 / 排序**：只做"格子 + 堆叠"。
- **小地图只做投影与剔除**：绘制（圆点、连线、迷雾）由渲染层负责。
- **飘字只有垂直上升**：横向漂移要自己设 `vx`（字段公开）。

## 验证

`tests/conformance/game_common.zan` 覆盖：存档往返与默认值与格式不符拒绝
与损坏拒绝与原文件保留与原子替换不留 `.tmp` 与备份恢复、飘字的生成/上升/
淡出/过期回收/池复用/池满丢弃、血条的比例与夹取与残影滞后与追赶与回血
立即跟随、冷却条的口径与充能与冷却中不可触发与瞬发不耗充能、背包的
先堆叠后开格与满包剩余与取出清空与 JSON 往返与坏档不改背包、小地图的
映射与视口剔除与全图模式与帧内复用。

## 放置

游戏包约定：`packages/Zan.Game.Common/src/Game/Common/*.zan`。
引擎级能力（物理 / 渲染 / ECS / 资源）走独立包并单独版本化，不进 stdlib。
粒子等已有专门包的机制**不重复造**（见 `Zan.Game.Particles`）。
