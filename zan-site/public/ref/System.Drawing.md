# System.Drawing

> 源码: `stdlib/System/Drawing/Graphics.zan`, `stdlib/System/Drawing/Primitives.zan`


## Bitmap (class)

通过 GDI 创建和操作位图。

- [DllImport("gdi32")]static extern int CreateCompatibleDC(int hdc);

- [DllImport("gdi32")]static extern int CreateCompatibleBitmap(int hdc, int width, int height);

- [DllImport("gdi32")]static extern int SelectObject(int hdc, int obj);

- [DllImport("gdi32")]static extern int DeleteDC(int hdc);

- [DllImport("gdi32")]static extern int DeleteObject(int obj);

- [DllImport("gdi32")]static extern int BitBlt(int hdcDest, int xDest, int yDest, int width, int height, int hdcSrc, int xSrc, int ySrc, int rop);

- static int CreateMemoryDC(int hwndDC, int width, int height)
  - 创建带兼容位图的内存 DC。

- static void CopyTo(int srcDC, int destDC, int x, int y, int width, int height)
  - 将位图内容复制到目标 DC。

- static void Destroy(int memDC)
  - 删除内存 DC。


## Color (class)

表示一个包含 RGBA 分量的颜色。

- int r;

- int g;

- int b;

- int a;

- static Color FromRGB(int r, int g, int b)

- static Color FromARGB(int a, int r, int g, int b)

- int ToColorRef()

- static Color Red()

- static Color Green()

- static Color Blue()

- static Color White()

- static Color Black()

- static Color Gray()

- static Color Yellow()

- static Color Cyan()

- static Color Magenta()

- static Color Orange()


## Font (class)

提供 GDI 文本渲染所需的字体创建。

- [DllImport("gdi32")]static extern int CreateFontA(int height, int width, int escapement, int orientation, int weight, int italic, int underline, int strikeout, int charset, int outPrecision, int clipPrecision, int quality, int pitchAndFamily, string faceName);

- [DllImport("gdi32")]static extern int SelectObject(int hdc, int obj);

- [DllImport("gdi32")]static extern int DeleteObject(int obj);

- static int Create(string name, int size)
  - 按指定名称和大小创建字体。

- static int CreateBold(string name, int size)
  - 创建粗体字体。

- static int CreateItalic(string name, int size)
  - 创建斜体字体。

- static int Select(int hdc, int font)
  - 将字体选入设备上下文。

- static void Destroy(int font)
  - 删除字体对象。


## Graphics (class)

在设备上下文上提供 GDI 绘图操作。

- [DllImport("gdi32")]static extern int CreateSolidBrush(int color);

- [DllImport("gdi32")]static extern int CreatePen(int style, int width, int color);

- [DllImport("gdi32")]static extern int SelectObject(int hdc, int obj);

- [DllImport("gdi32")]static extern int DeleteObject(nint obj);

- [DllImport("gdi32")]static extern int Rectangle(nint hdc, int left, int top, int right, int bottom);

- [DllImport("gdi32")]static extern int Ellipse(nint hdc, int left, int top, int right, int bottom);

- [DllImport("gdi32")]static extern int MoveToEx(nint hdc, int x, int y, nint lpPoint);

- [DllImport("gdi32")]static extern int LineTo(nint hdc, int x, int y);

- [DllImport("gdi32")]static extern int SetPixel(nint hdc, int x, int y, int color);

- [DllImport("gdi32")]static extern int GetPixel(nint hdc, int x, int y);

- [DllImport("gdi32")]static extern int SetBkMode(nint hdc, int mode);

- [DllImport("gdi32")]static extern int SetTextColor(nint hdc, int color);

- [DllImport("gdi32")]static extern int TextOutA(nint hdc, int x, int y, string text, int len);

- [DllImport("gdi32")]static extern int SetBkColor(nint hdc, int color);

- [DllImport("gdi32")]static extern int RoundRect(nint hdc, int left, int top, int right, int bottom, int rx, int ry);

- [DllImport("gdi32")]static extern int Polygon(nint hdc, string points, int count);

- [DllImport("gdi32")]static extern int Polyline(nint hdc, string points, int count);

- [DllImport("gdi32")]static extern int Arc(nint hdc, int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4);

- [DllImport("gdi32")]static extern int Pie(nint hdc, int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4);

- [DllImport("gdi32")]static extern int CreateFont(int height, int width, int escapement, int orientation, int weight, int italic, int underline, int strikeout, int charset, int outPrecision, int clipPrecision, int quality, int pitchAndFamily, string faceName);

- [DllImport("user32")]static extern int FillRect(int hdc, string rect, int brush);

- [DllImport("user32")]static extern int GetDC(int hwnd);

- [DllImport("user32")]static extern int ReleaseDC(int hwnd, int hdc);

- static void FillRectangle(int hdc, int x, int y, int w, int h, int color)
  - 绘制实心矩形。

- static void DrawRectangle(int hdc, int x, int y, int w, int h, int color, int penWidth)
  - 绘制矩形边框。

- static void FillEllipse(int hdc, int x, int y, int w, int h, int color)
  - 绘制实心椭圆。

- static void DrawLine(int hdc, int x1, int y1, int x2, int y2, int color, int penWidth)
  - 在两点之间绘制一条直线。

- static void DrawText(int hdc, string text, int x, int y, int color)
  - 在指定位置绘制文本。

- static void FillRoundRect(int hdc, int x, int y, int w, int h, int rx, int ry, int color)
  - 绘制实心圆角矩形。

- static int FromHwnd(int hwnd)
  - 获取窗口的设备上下文。

- static void Release(int hwnd, int hdc)
  - 释放设备上下文。


## Point (class)

表示一个二维点。

- public int x;

- public int y;

- public Point(int x, int y)


## Rectangle (class)

表示一个矩形。

- public int x;

- public int y;

- public int width;

- public int height;

- public Rectangle(int x, int y, int w, int h)

- int Right()

- int Bottom()

- bool Contains(int px, int py)


## Size (class)

表示一个二维尺寸。

- public int width;

- public int height;

- public Size(int w, int h)
