# Game.Render

> 源码: `stdlib/Game/Render/BitmapFont.zan`, `stdlib/Game/Render/Sprite.zan`


## BitmapFont (class)

供游戏 HUD 与诊断使用的定长单元位图字体图集。默认
图集布局从 ASCII 32 开始，按列从左到右排列。

- SdlTexture texture;

- int glyphWidth;

- int glyphHeight;

- int columns;

- int firstCharacter;

- int spacing;

- static BitmapFont Load(SdlRenderer renderer, string path, int glyphWidth, int glyphHeight, int columns)

- bool IsValid()

- void SetFirstCharacter(int code)

- void SetSpacing(int spacing)

- int Measure(string text, int scale)

- void Draw(SdlRenderer renderer, string text, int x, int y, int scale, int red, int green, int blue, int alpha)

- void DrawCentered(SdlRenderer renderer, string text, int centerX, int y, int scale, int red, int green, int blue, int alpha)

- void Close()


## Color (class)

通道范围为 0-255 的 RGBA 颜色。

- int r;

- int g;

- int b;

- int a;

- static Color Rgba(int r, int g, int b, int a)

- static Color Rgb(int r, int g, int b)

- static Color White()

- static Color Black()

- int R()

- int G()

- int B()

- int A()


## Sprite (class)

带纹理的四边形，含位置、尺寸与着色，通过
`SdlGpu` 上下文绘制。默认整张纹理；调用
`SetSource` 可绘制图集子矩形。

- SdlGpuTexture texture;

- int x;

- int y;

- int width;

- int height;

- int srcX;

- int srcY;

- int srcW;

- int srcH;

- Color tint;

- Sprite(SdlGpuTexture texture, int x, int y, int width, int height)

- void SetPosition(int x, int y)

- void SetSize(int width, int height)

- void SetTint(Color tint)

- void SetSource(int srcX, int srcY, int srcW, int srcH)

- int X()

- int Y()

- void Draw(SdlGpu gpu)
  - 将该精灵追加到当前帧批次。
