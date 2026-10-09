# Game.Kit

> 源码: `packages/Zan.Game/src/Game/Kit/Assets.zan`, `packages/Zan.Game/src/Game/Kit/CanvasPrims.zan`, `packages/Zan.Game/src/Game/Kit/Packed.zan`, `packages/Zan.Game/src/Game/Kit/SpriteBatch.zan`, `packages/Zan.Game/src/Game/Kit/Support.zan`


## Assets (class)

- static string exeDir;

- static bool exeDirReady;

- public static string ExeDir()

- static string Parent(string path)

- static string Find(string rel)

- public static string FindCommon(string name)


## BlitSourceEntry (class)

- public string path;

- public string key;

- public BlitSourceEntry(string path, string key)


## CDraw (class)

- static int originX;

- static int originY;

- static void Origin(int x, int y)

- static int vpS=65536;

- static int vpX;

- static int vpY;

- static int stageW;

- static int stageH;

- static int MapX(int v)

- static int MapY(int v)

- static int MapW(int v)

- static int UnmapX(int v)

- static int UnmapY(int v)

- static void StageViewport(Canvas c, int contentTopPx, int designW, int designH, int marginColor)

- static int Argb(int a, int r, int g, int b)

- static void FillRect(Canvas c, int x, int y, int w, int h, int color)

- static void FillVGrad(Canvas c, int x, int y, int w, int h, int colorTop, int colorBottom)

- static void Clear(Canvas c, int w, int h, int color)

- static List<BlitSourceEntry> blitSources;

- static string BlitSource(Canvas c, string path)

- static void BlitImage(Canvas c, string path, int dx, int dy, int dw, int dh)

- static void RoundRectFill(Canvas c, int x, int y, int w, int h, int rad, int cr, int cg, int cb, int ca)

- static void RingRect(Canvas c, int x, int y, int w, int h, int t, int cr, int cg, int cb, int ca)

- static void FillCircle(Canvas c, int cx, int cy, int rad, int cr, int cg, int cb, int ca)

- static void Line(Canvas c, int x0, int y0, int x1, int y1, int t, int cr, int cg, int cb, int ca)

- static void CircleStroke(Canvas c, int cx, int cy, int rad, int t, int cr, int cg, int cb, int ca)

- static bool PointIn(int px, int py, int x, int y, int w, int h)


## CFx (class)

- static void ShadowRect(Canvas c, int x, int y, int w, int h, int rad, int blur, int cr, int cg, int cb, int ca)

- static void GradRect(Canvas c, int x, int y, int w, int h, int rad, int dir, int c0, int cv, int c1)

- static void Ring(Canvas c, int x, int y, int w, int h, int rad, int t, int cr, int cg, int cb, int ca)

- static void RingCircle(Canvas c, int cx, int cy, int rad, int t, int cr, int cg, int cb, int ca)

- static void Card(Canvas c, int x, int y, int w, int h, int rad, int fr, int fg, int fb, int f2r, int f2g, int f2b, int tr, int tg, int tb, int ta, int t, int dir)

- static void Pill(Canvas c, int x, int y, int w, int h, int dir, int fr, int fg, int fb, int f2r, int f2g, int f2b, int tr, int tg, int tb, int ta)

- static bool GoldButton(Canvas c, int mx, int my, int x, int y, int w, int h, string label, int scale, int kind)

- static void Vignette(Canvas c, int w, int h, int a)

- static void Rule(Canvas c, int cx, int cy, int halfW, int cr, int cg, int cb, int ca)


## CKitUi (class)

- static void Backdrop(Canvas c, string bgPath, int w, int h)

- static bool Button(Canvas c, int mx, int my, int x, int y, int w, int h, string label, int scale, int kind)


## CSprite (class)

- static void Glow(Canvas c, int cx, int cy, int rad, int cr, int cg, int cb, int ca)

- static void Ball(Canvas c, int cx, int cy, int rad, int cr, int cg, int cb, int ca)


## CText (class)

- static int FontPx(int scale)

- static int LineHeight(int scale)

- static int DevPx(int scale)

- static int Width(string text, int scale)

- static void Draw(Canvas c, string text, int x, int y, int scale, int cr, int cg, int cb, int ca)

- static void DrawCentered(Canvas c, string text, int centerX, int y, int scale, int cr, int cg, int cb, int ca)

- static void DrawShadow(Canvas c, string text, int x, int y, int scale, int cr, int cg, int cb, int ca)

- static void DrawCenteredShadow(Canvas c, string text, int centerX, int y, int scale, int cr, int cg, int cb, int ca)


## KeyShares (class)

- public static string File(string path)

- static bool SpaceAt(string s, int i)


## PackedAssets (class)

- ResourcePack pack;

- PackedAssets()

- public static PackedAssets Open(string packPath, string shareAHex, string shareBHex)

- public static PackedAssets OpenKeyed(string packPath, string masterKey)

- public static PackedAssets OpenVerified(string packPath, string shareAHex, string shareBHex, string nHex, string eHex)

- public static PackedAssets OpenKeyedVerified(string packPath, string masterKey, string nHex, string eHex)

- public byte[]Read(string name)

- public AudioClip LoadWav(string name)

- public bool Contains(string name)

- public int Count()

- public int SizeOf(string name)


## Rng (class)

- long state;

- static Rng Seed(long s)

- long NextLong()

- int Next()

- int Range(int lo, int hi)

- double NextDouble01()


## SpriteBatch (class)

- const int CmdRect=0;

- const int CmdBall=1;

- const int CmdGlow=2;

- int capacity;

- int count;

- int culledCount;

- int[]cmdType;

- int[]cmdX;

- int[]cmdY;

- int[]cmdW;

- int[]cmdH;

- int[]cmdR;

- int[]cmdG;

- int[]cmdB;

- int[]cmdA;

- Canvas canvas;

- bool inBatch;

- int drawCalls;

- int globalAlpha;

- nint bufRects;

- nint bufBalls;

- nint bufGlows;

- SpriteBatch()

- SpriteBatch(int cap)

- void allocCommands()

- static SpriteBatch Create(int capacity)

- int DrawCalls()

- int ItemCount()

- int CulledCount()

- void SetGlobalAlpha(int alpha)

- void Begin(Canvas c)

- void End()

- void ensurePackBuffers()

- void DrawRect(int x, int y, int w, int h, int color)

- void DrawBall(int cx, int cy, int rad, int r, int g, int b, int a)

- void DrawGlow(int cx, int cy, int rad, int r, int g, int b, int a)


## Talker (class)

- List<string> praise;

- List<string> taunt;

- List<string> neutral;

- List<string> hype;

- string current;

- int kind;

- long until;

- Rng rng;

- static Talker Create(Rng rng)

- void Add(int bank, string line)

- List<string> Bank(int bank)

- void Say(int bank, int nowMs, int holdMs)

- void SayExact(int bank, string line, int nowMs, int holdMs)

- string Current(int nowMs)

- int Kind()


## Utf8 (class)

- static int Decode(string s, int[]pos)
