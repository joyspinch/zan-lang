# Game.Graphics

> 源码: `packages/Zan.Game/src/Game/Graphics/Mesh3DProjection.zan`, `packages/Zan.Game/src/Game/Graphics/PaperdollRenderer.zan`, `packages/Zan.Game/src/Game/Graphics/SpriteAnimation.zan`, `packages/Zan.Game/src/Game/Graphics/SpriteSheet.zan`


## Animation3DTrack (class)

- string clipName;

- double time;

- double duration;

- double speed;

- double weight;

- bool loop;

- Animation3DTrack()

- static Animation3DTrack Create(string clipName, double duration, double speed, bool loop)

- string ClipName { get }

- double Time{ get set}

- double Duration { get }

- double Weight{ get set}

- bool Loop { get }

- void Advance(double dt)


## AnimationState (class)

- string name;

- int startRow;

- int framesPerDir;

- double fps;

- bool loop;

- AnimationState(string name, int startRow, int framesPerDir, double fps, bool loop)

- static AnimationState Create(string name, int startRow, int framesPerDir, double fps, bool loop)

- string Name { get }

- int StartRow { get }

- int FramesPerDir { get }

- double Fps { get }

- bool Loop { get }


## BoneSocket (class)

- string name;

- double localX;

- double localY;

- double localZ;

- BoneSocket(string name, double lx, double ly, double lz)

- static BoneSocket Create(string name, double lx, double ly, double lz)

- string Name { get }

- double LocalX { get }

- double LocalY { get }

- double LocalZ { get }

- void ResolveWorldOffset(double rotYRad, out double outX, out double outY, out double outZ)


## DirectionalAnimator (class)

- SpriteSheet sheet;

- AnimationState[]states;

- int stateCount;

- int currentStateIndex;

- int currentDir8;

- bool is5DirMode;

- double frameTimer;

- int currentFrameInDir;

- bool isFinished;

- DirectionalAnimator(SpriteSheet sheet, int maxStates)

- static DirectionalAnimator Create(SpriteSheet sheet, int maxStates)

- SpriteSheet Sheet { get }

- int Direction{ get set}

- bool Is5DirMode{ get set}

- bool IsFinished { get }

- int CurrentFrameInDir { get }

- string CurrentStateName { get }

- void AddState(string name, int startRow, int framesPerDir, double fps, bool loop)

- void Play(string stateName, int dir8, bool forceRestart)

- void Update(double dt)

- static void ResolveDir5(int dir8, out int outRowOffset, out bool outFlipX)

- int GetCurrentGlobalFrame()

- bool IsCurrentFlipped()

- void GetCurrentUV(out float u0, out float v0, out float u1, out float v1)


## Mesh3DProjection (class)

- double pixelsPerUnit;

- double currentYaw;

- double targetYaw;

- double rotationSpeed;

- Animation3DTrack currentTrack;

- Animation3DTrack fadeTrack;

- double crossFadeDuration;

- double crossFadeElapsed;

- bool isFading;

- BoneSocket[]sockets;

- int socketCount;

- Mesh3DProjection(double ppu, int maxSockets)

- static Mesh3DProjection Create(double ppu, int maxSockets)

- double PPU { get }

- double CurrentYaw{ get set}

- double TargetYaw{ get set}

- double RotationSpeed{ get set}

- Animation3DTrack CurrentTrack { get }

- Animation3DTrack FadeTrack { get }

- bool IsFading { get }

- void World2Dto3D(double x2d, double y2d, double height2d, out double x3d, out double y3d, out double z3d)

- void Project3DtoScreen(double x3d, double y3d, double z3d, out double screenX, out double screenY)

- void SetLookDirection(double dx, double dy)

- void RegisterSocket(string name, double lx, double ly, double lz)

- bool GetSocketWorldPosition(string name, double charX3d, double charY3d, double charZ3d, out double outX, out double outY, out double outZ)

- void CrossFade(string newClipName, double duration, double speed, bool loop, double fadeTime)

- void Update(double dt)


## PaperdollLayer (class)

- string name;

- SpriteSheet sheet;

- bool is5DirMode;

- bool visible;

- double offsetX;

- double offsetY;

- int baseOrder;

- int[]orderPerDir;

- PaperdollLayer(string name, SpriteSheet sheet, int baseOrder, int[]orderPerDir, bool is5DirMode)

- static PaperdollLayer Create(string name, SpriteSheet sheet, int baseOrder, int[]orderPerDir, bool is5DirMode)

- string Name { get }

- SpriteSheet Sheet{ get set}

- bool Visible{ get set}

- bool Is5DirMode{ get set}

- double OffsetX{ get set}

- double OffsetY{ get set}

- int BaseOrder{ get set}

- int GetOrder(int dir8)


## PaperdollRenderer (class)

- PaperdollLayer[]layers;

- int layerCount;

- DirectionalAnimator masterAnimator;

- int[]sortedLayerIndices;

- int[]sortKeys;

- PaperdollRenderer(SpriteSheet baseSheet, int maxLayers)

- static PaperdollRenderer Create(SpriteSheet baseSheet, int maxLayers)

- DirectionalAnimator MasterAnimator { get }

- int LayerCount { get }

- void AddLayer(string name, SpriteSheet sheet, int baseOrder, int[]orderPerDir, bool is5DirMode)

- PaperdollLayer GetLayer(string name)

- void SetLayerVisible(string name, bool visible)

- void SwapLayerSheet(string name, SpriteSheet newSheet)

- void Play(string stateName, int dir8, bool forceRestart)

- void Update(double dt)

- int SortLayersByDirection()

- int GetSortedLayerIndex(int sortedRank)

- void GetLayerCurrentUV(int layerIndex, out float u0, out float v0, out float u1, out float v1, out bool flipped)


## SpriteSheet (class)

- string textureKey;

- int texWidth;

- int texHeight;

- int frameWidth;

- int frameHeight;

- int cols;

- int rows;

- int totalFrames;

- SpriteSheet(string key, int texW, int texH, int frameW, int frameH)

- static SpriteSheet Create(string key, int texW, int texH, int frameW, int frameH)

- string TextureKey { get }

- int FrameWidth { get }

- int FrameHeight { get }

- int TotalFrames { get }

- int Cols { get }

- int Rows { get }

- void GetFrameUV(int frameIndex, out float u0, out float v0, out float u1, out float v1)

- void GetFrameRect(int frameIndex, out int srcX, out int srcY, out int srcW, out int srcH)
