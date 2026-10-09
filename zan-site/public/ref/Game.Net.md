# Game.Net

> 源码: `packages/Zan.Game/src/Game/Net/LockstepManager.zan`, `packages/Zan.Game/src/Game/Net/ReplaySystem.zan`


## FrameBucket (class)

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

- int Update(double dt, LogicTickFn onExecuteLogicTick)


## PlayerCommand (class)

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

- int frameIndex;

- List<PlayerCommand> commands;

- ReplayFrameRecord(int frameIdx)

- static ReplayFrameRecord Create(int frameIdx)

- int FrameIndex { get }

- List<PlayerCommand> Commands { get }

- void AddCommand(PlayerCommand cmd)


## ReplayHeader (class)

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


## ReplayRecorder (class)

- ReplayHeader header;

- List<ReplayFrameRecord> recordedFrames;

- bool isRecording;

- ReplayRecorder(long seed, int players, string map)

- static ReplayRecorder Create(long seed, int players, string map)

- bool IsRecording { get }

- int FrameCount { get }

- ReplayHeader Header { get }

- void RecordFrame(int frameIdx, FrameBucket bucket)

- void Stop()

- List<ReplayFrameRecord> GetFrames()


## void (delegate)

`delegate void LogicTickFn(int logicFrame, FrameBucket bucket);`


## void (delegate)

`delegate void ReplayFrameFn(int frameIndex, List<PlayerCommand> commands);`
