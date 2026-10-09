# System.Drawing

> 源码: `packages/Zan.Desktop/src/System/Drawing/ImageBuffer.zan`, `packages/Zan.Desktop/src/System/Drawing/PixelOps.zan`, `packages/Zan.Desktop/src/System/Drawing/Primitives.zan`


## Color (class)

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


## ImageBuffer (class)

- public int Width;

- public int Height;

- public int Stride;

- public nint Pixels;

- public ImageBuffer(int width, int height)

- public void Dispose()

- ~ImageBuffer()

- public void Clear(int color)

- public void FillRect(int x, int y, int w, int h, int color)

- public ImageBuffer Crop(int x, int y, int w, int h)

- public ImageBuffer Resize(int newWidth, int newHeight)

- public void Blit(ImageBuffer src, int dx, int dy, bool alphaBlend)

- public void SwapRB()

- public void AdjustBrightness(int delta)

- public void Blend50(ImageBuffer other)

- public void Darken(ImageBuffer other)

- public void Lighten(ImageBuffer other)

- public void Invert()

- public void Grayscale()

- public void BoxBlur(int radius)


## PixelOps (class)

- [DllImport("crt")]static extern void BlendOver(nint dst, nint src, int count);

- [DllImport("crt")]static extern void SwapRB(nint dst, nint src, int count);

- [DllImport("crt")]static extern void FillRect(nint dst, int dstStride, int x, int y, int w, int h, int color);

- [DllImport("crt")]static extern void ResampleBilinearRow(nint dst, nint src0, nint src1, nint xIndices, nint xWeights, int weightY, int width);


## Point (class)

- public int x;

- public int y;

- public Point(int x, int y)


## Rectangle (class)

- public int x;

- public int y;

- public int width;

- public int height;

- public Rectangle(int x, int y, int w, int h)

- int Right()

- int Bottom()

- bool Contains(int px, int py)


## Size (class)

- public int width;

- public int height;

- public Size(int w, int h)
