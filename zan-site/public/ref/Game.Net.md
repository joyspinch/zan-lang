# Game.Net

> 源码: `packages/Zan.Game/src/Game/Net/LockstepManager.zan`, `packages/Zan.Game/src/Game/Net/ReplaySystem.zan`


## FrameBucket (class)

单个逻辑帧命令桶（包含该帧所有玩家的操作指令合集）。

- int frameIndex;

- PlayerCommand[]commands;

- int commandCount;

- FrameBucket(int frameIndex, int maxCommands)

- static FrameBucket Create(int frameIndex, int maxCommands)

- int FrameIndex { get }

- int CommandCount { get }

- void AddCommand(PlayerCommand cmd)

- PlayerCommand GetCommand(int index)


## LockstepManager (class)

工业级定步帧同步管理器（Lockstep Synchronizer）。
专为 RTS（红警/魔兽/帝国）、多人 ARPG 攻沙、格斗与联机塔防打造：
1. 固定频率逻辑帧驱动（默认 20Hz / 50ms 一帧）；
2. 命令桶缓存与按帧顺序确定性执行；
3. 网络断线与延迟积压快速追帧（Fast-Forward Catch-up）；
4. 逻辑分叉校验（Checksum Hash）。

- int currentLogicFrame;

- int serverConfirmedFrame;

- double tickIntervalSec;

- double accumulator;

- FrameBucket[]frameHistory;

- int maxHistory;

- bool isCatchingUp;

- LockstepManager(double tickRateHz, int historyCap)

- static LockstepManager Create(double tickRateHz, int historyCap)

- int CurrentLogicFrame { get }

- int ServerConfirmedFrame { get }

- double TickInterval { get }

- bool IsCatchingUp { get }

- void ReceiveServerFrame(int frameIdx, PlayerCommand[]cmds, int count)
  - 接收到服务器广播的指定逻辑帧命令包。

- int Update(double dt, LogicTickFn onExecuteLogicTick)
  - 推进时间并尝试消耗帧。
    回调 delegate: void OnExecuteFrame(int frameIndex, FrameBucket bucket)


## PlayerCommand (class)

单个玩家在特定逻辑帧输入的指令。

- int playerId;

- int cmdType;

- double targetX;

- double targetY;

- int targetId;

- int extraParam;

- PlayerCommand(int pid, int type, double tx, double ty, int tid, int extra)

- static PlayerCommand Create(int pid, int type, double tx, double ty, int tid, int extra)

- int PlayerId { get }

- int CmdType { get }

- double TargetX { get }

- double TargetY { get }

- int TargetId { get }

- int ExtraParam { get }


## ReplayFrameRecord (class)

单帧录像数据（帧序号 + 该帧全部玩家指令）。

- int frameIndex;

- List<PlayerCommand> commands;

- ReplayFrameRecord(int frameIdx)

- static ReplayFrameRecord Create(int frameIdx)

- int FrameIndex { get }

- List<PlayerCommand> Commands { get }

- void AddCommand(PlayerCommand cmd)


## ReplayHeader (class)

录像元数据头信息。

- int magic;

- int version;

- long randomSeed;

- int playerCount;

- string mapName;

- int totalFrames;

- ReplayHeader(long seed, int players, string map)

- static ReplayHeader Create(long seed, int players, string map)

- long RandomSeed { get }

- int PlayerCount { get }

- string MapName { get }

- int TotalFrames{ get set}


## ReplayPlayer (class)

录像回放播放器（ReplayPlayer）：
支持暂停、恢复、快进倍速播放（1x, 2x, 4x, 8x）与步进帧同步。

- ReplayHeader header;

- List<ReplayFrameRecord> frames;

- int currentPlayIndex;

- bool isPlaying;

- bool isPaused;

- double speedMultiplier;

- double accumulator;

- double frameIntervalSec;

- ReplayPlayer(ReplayHeader header, List<ReplayFrameRecord> frames, double tickRateHz)

- static ReplayPlayer Create(ReplayHeader header, List<ReplayFrameRecord> frames, double tickRateHz)

- bool IsPlaying { get }

- bool IsPaused { get }

- bool IsFinished { get }

- double SpeedMultiplier{ get set}

- int CurrentFrameIndex { get }

- int TotalFrames { get }

- void Play()

- void Pause()

- void Resume()

- int Update(double dt, ReplayFrameFn onExecuteFrame)
  - 推进回放时间轴，按倍速逐帧触发执行回调。


## ReplayRecorder (class)

战局录制器（ReplayRecorder）：
零侵入挂载在 Lockstep 循环中，将每帧玩家指令原子记录。

- ReplayHeader header;

- List<ReplayFrameRecord> recordedFrames;

- bool isRecording;

- ReplayRecorder(long seed, int players, string map)

- static ReplayRecorder Create(long seed, int players, string map)

- bool IsRecording { get }

- int FrameCount { get }

- ReplayHeader Header { get }

- void RecordFrame(int frameIdx, FrameBucket bucket)
  - 记录当前逻辑帧的命令桶。

- void Stop()
  - 结束录制。

- List<ReplayFrameRecord> GetFrames()


## void (delegate)

帧同步逻辑帧执行回调：逻辑帧号、该帧指令桶。

`delegate void LogicTickFn(int logicFrame, FrameBucket bucket);`


## void (delegate)

录像回放帧执行回调：帧号、该帧玩家指令集。

`delegate void ReplayFrameFn(int frameIndex, List<PlayerCommand> commands);`
