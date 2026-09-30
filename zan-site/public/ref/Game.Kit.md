# Game.Kit

> 源码: `packages/Zan.Game/src/Game/Kit/Assets.zan`, `packages/Zan.Game/src/Game/Kit/CanvasPrims.zan`, `packages/Zan.Game/src/Game/Kit/Packed.zan`, `packages/Zan.Game/src/Game/Kit/SpriteBatch.zan`, `packages/Zan.Game/src/Game/Kit/Support.zan`


## Assets (class)

- static string exeDir;

- static bool exeDirReady;

- public static string ExeDir()

- static string Parent(string path)

- static string Find(string rel)

- public static string FindCommon(string name)


## CDraw (class)

- static int originX;

- static int originY;

- static void Origin(int x, int y)
  - 设置本帧舞台绘制的画布原点；模板 Render 开头调用一次。

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
  - 桌面窗口跟随：内容区 [contentTopPx, 画布底) 等比铺满逻辑
    designW×designH——缩放取短轴，长轴延展成更大的逻辑空间（不
    缩放不裁剪），故无黑边。帧首整块铺 marginColor 打底（延展边、
    换面首帧不至于露垃圾），模板随后按 stageW/stageH 满画。指针
    坐标用 UnmapX/UnmapY 换回。GuiHost 每帧调用。

- static int Argb(int a, int r, int g, int b)

- static void FillRect(Canvas c, int x, int y, int w, int h, int color)

- static void FillVGrad(Canvas c, int x, int y, int w, int h, int colorTop, int colorBottom)

- static void Clear(Canvas c, int w, int h, int color)

- static List<string> blitSrcPaths;

- static List<string> blitSrcKeys;

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
  - Read one 64-char hex share from a file (typically an
    --embed'd copy, so File.ReadAllBytes finds it without a real file on
    disk). Trims whitespace/newlines.

- static bool SpaceAt(string s, int i)


## PackedAssets (class)

- ResourcePack pack;

- PackedAssets()

- public static PackedAssets Open(string packPath, string shareAHex, string shareBHex)
  - Open a .zrp and authenticate its index.
    Both shares are 64-char hex strings (32 bytes each); the real key is
    their XOR, reassembled here and never stored as one literal.
    Throws IOException when the pack can't be read/authenticated:
    "pack: io/magic"    (1) not a .zrp or truncated
    "pack: auth"        (2) wrong master key (index failed GCM)
    "pack: signature"   (3) index signature mismatch (repacked/tampered)

- public static PackedAssets OpenKeyed(string packPath, string masterKey)
  - Open with an already-assembled master key (32 bytes as a
    byte buffer). Same error contract as `Open`.

- public static PackedAssets OpenVerified(string packPath, string shareAHex, string shareBHex, string nHex, string eHex)
  - Open a SIGNED .zrp (packed with --sign-n/--sign-d) and
    authenticate AND signature-check its index before serving anything.
    Games that ship signed packs should use this: a stolen master key
    alone is then not enough to swap entries — the pack must be re-signed
    with the private key. The verify key (RSA modulus/exponent hex) is
    public material and may live in the binary — it's meant to be seen.
    <paramref name="nHex"/>/<paramref name="eHex"/> = public modulus and
    exponent (hex). Error contract is `Open`'s, plus:
    "pack: signature"   (3) index signature missing/mismatched
    (an unsigned pack is rejected too — signature is required here).

- public static PackedAssets OpenKeyedVerified(string packPath, string masterKey, string nHex, string eHex)
  - OpenVerified with an already-assembled master key (32 bytes
    as a byte buffer). Same error contract as `OpenVerified`.

- public byte[]Read(string name)
  - Raw entry bytes. Throws IOException when the name is not in
    the pack or the payload fails GCM authentication (tampered).

- public AudioClip LoadWav(string name)
  - Parse a WAV entry straight from the decrypted bytes -- the
    PCM is copied out inside the call, nothing touches the disk. Invalid
    handle when the bytes aren't a WAV.

- public bool Contains(string name)
  - Entry existence probe (no decryption).

- public int Count()
  - Number of entries in the pack.

- public int SizeOf(string name)
  - Byte size of one entry (no decryption), -1 when unknown.


## Rng (class)

- long state;

- static Rng Seed(long s)

- long NextLong()

- int Next()

- int Range(int lo, int hi)

- double NextDouble01()


## SpriteBatch (class)

2D 游戏批量图元与精灵渲染上下文。
封装批量绘制指令流、全局颜色/Alpha 混合与视口映射，提供绘图统计以指导性能调优。

- Canvas canvas;

- bool inBatch;

- int drawCalls;

- int itemCount;

- int globalAlpha;

- SpriteBatch()

- int DrawCalls()
  - 当前已提交的批次绘制调用数（DrawCalls）。

- int ItemCount()
  - 本帧批量绘制的项目（图元/精灵）总数。

- void SetGlobalAlpha(int alpha)
  - 设置全局 Alpha 透明度乘数（0~255）。

- void Begin(Canvas c)
  - 开始一个精灵批处理阶段。

- void End()
  - 结束批处理阶段。

- void DrawRect(int x, int y, int w, int h, int color)
  - 批量绘制实心矩形。

- void DrawBall(int cx, int cy, int rad, int r, int g, int b, int a)
  - 批量绘制实心圆盘。

- void DrawGlow(int cx, int cy, int rad, int r, int g, int b, int a)
  - 批量绘制柔和径向光晕。


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
