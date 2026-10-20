# Game.Netcode

`Game.Netcode` 是 **rollback（回滚预测）网络同步**层：**确定性校验和**、
**快照环形缓冲**、**逐帧输入追踪与预测**、**回滚重放会话**（含分叉检测）、
**服务器侧延迟补偿**。

它补的是 `Game.Net` 里 lockstep + 回放缺的那一半。lockstep 要求所有客户端
**同时**推进——任何一方网络卡顿，所有人一起等。rollback 让本地**不等远端**：
本地输入立即生效，远端输入没到就先预测，猜错了就回滚重算。

## 设计取舍

- **状态用不透明字节**：回滚层不知道游戏状态长什么样（实体位置？血量？
  棋盘？）。游戏自己 `Capture` / `Restore` 成字节，回滚层只按帧号存取 +
  算校验和。代价是**状态字节数必须恒定**（环形槽位按固定长度分配）。
- **预测只用"沿用上次输入"**：不做方向外推、不做位置预测。简单、不会
  引入"预测出来的怪动作"，代价是远端输入来得越晚、猜错率越高。这是可
  换的策略点，但换错方向比猜错更糟。
- **预测基线是"该帧之前最近的权威输入"**，不是"最近收到的那个值"：远端
  输入可能**提前**到达（第 5 帧的输入先到），拿它预测第 2 帧就是拿未来
  当过去。
- **回滚失败要明说**：快照或输入被环形缓冲覆盖时，`Resimulate()` 返回
  **-1** 而不是悄悄用错的状态继续跑——拿别的帧的状态恢复会把世界改成
  另一个样子，比不同步更难查。
- **校验和用 FNV-1a 而不是哈希表**：哈希表的迭代顺序不保证（键类型、
  插入顺序都会影响），拿它算校验和 = 每次结果可能不同 = 永远在报"分叉"。
- **截到 31 位**：校验和落到 int 最高位时在不同平台呈现不同。非负值可
  直接打印、可按字典序比较。
- **延迟补偿只回溯、不改写历史**：补偿是**判定时**的一次查询。改写历史
  会让"回溯过的判定"污染后续帧。
- **回溯窗口有上限**：`maxRewindFrames` 限制最多往回看多少帧。一个延迟
  10 秒的客户端不能让服务器回到 10 秒前判定命中。

## 能力

| 能力 | 入口 |
|---|---|
| 校验和 | `Checksum.Of` / `Checksum.Combine` |
| 快照 | `Snapshot`、`SnapshotBuffer`（环形、按帧号存取、覆盖检测） |
| 输入 | `InputFrame`（权威/预测标记）、`InputHistory`（环形） |
| 回滚会话 | `RollbackSession`：`Advance` / `SubmitRemoteInput` / `Resimulate` / `VerifyChecksum` |
| 延迟补偿 | `PositionHistory`（按帧存位置）、`LagCompensation`（回溯帧钳制 + 取位置）、`NetPoint` |

## 用法

游戏侧要提供四个回调：状态字节数、把状态写进字节、从字节恢复状态、推进
一帧。

```zan
using Game.Netcode;

// 状态编码：长度必须恒定
void Capture(byte[] buf) { /* 写世界状态 */ }
void Restore(byte[] buf) { /* 还原世界状态 */ }
void Step(int frame, int[] inputs) {
    // inputs[0] = 本地输入，inputs[1..] = 远端输入（可能是预测值）
}

RollbackSession session = new RollbackSession(2, 64,
    () => 128,                 // 状态字节数
    (byte[] buf) => { Capture(buf); },
    (byte[] buf) => { Restore(buf); },
    (int frame, int[] inputs) => { Step(frame, inputs); });

// 每帧：本地输入立即生效
session.Advance(myInput);

// 网络线程收到远端输入时提交
session.SubmitRemoteInput(frameIndex, playerId, remoteInput);

// 猜错了 → 回滚重放（本地零延迟，代价是重算）
if (session.NeedsResimulation()) {
    int replayed = session.Resimulate();
    if (replayed < 0) {
        // 快照已被覆盖 → 需要一次硬同步
    }
}

// 分叉检测：与服务器给的校验和比对
if (!session.VerifyChecksum(frameIndex, serverChecksum)) {
    // 已经分叉
}
```

服务器侧延迟补偿：

```zan
PositionHistory history = new PositionHistory(64, 128);
history.Capture(frameIndex, entityId, x, y);      // 每帧记录

LagCompensation comp = new LagCompensation(history, 10);
int rewindFrame = comp.ResolveRewindFrame(clientFrame, serverFrame);
NetPoint p = new NetPoint();
if (comp.Rewind(rewindFrame, entityId, p)) {
    // 用 p.x / p.y 判定"客户端当时看到的位置"上是否命中
}
```

## 已知边界

- **没有网络传输层**：本包只管同步语义（帧、输入、快照、判定），不碰
  socket。传输由 `Zan.Net` 负责。
- **没有输入延迟（input delay）**：GGPO 那一套"本地输入提前 N 帧送达"
  能显著降低猜错率，本包没实现——当前模型是"本地输入在当帧生效"。
- **状态字节数必须恒定**：背包格数随进度增长的游戏需要自己归一化，或用
  变长编码 + 长度表。
- **回滚窗口 = 环形缓冲容量**：超出容量的历史帧无法回滚，`Resimulate()`
  返回 -1。
- **只回溯位置，不回溯完整世界**：延迟补偿这里只做"某实体在某帧在哪"，
  复杂判定（技能范围、持续状态）需要游戏自己扩展历史记录。
- **单会话单线程**：网络线程只负责投递输入，推进与回滚在主循环。

## 验证

`tests/conformance/netcode_rollback.zan` 覆盖：校验和的确定性与字节敏感
性与空输入、快照缓冲的存取与帧号校验与覆盖后旧帧不可用与越界取回失败、
输入帧的权威值只接受一次与确认状态与预测标记、回滚会话的预测推进与
远端输入到达后回滚重放与重放结果与权威一致与预测错计数与快照被覆盖时
回滚失败并明说与提前到达的输入不算预测错、分叉检测的校验和不符计数、
延迟补偿的位置历史存取与回溯帧钳制与回溯取位置。

## 放置

游戏包约定：`packages/Zan.Game.Netcode/src/Game/Netcode/*.zan`。
与 `packages/Zan.Game` 的 `Game.Net`（lockstep + 回放）是**并列**关系：
lockstep 适合 RTS 这类"全体同频"的玩法，rollback 适合格斗 / 射击 /
多人 ARPG 的"本地要跟手"。两者不互相依赖。
