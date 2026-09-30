# System.Drawing

> 源码: `stdlib/System/Drawing/ImageBuffer.zan`, `stdlib/System/Drawing/PixelOps.zan`, `stdlib/System/Drawing/Primitives.zan`


## Color (class)

表示一个包含 RGBA 分量的颜色。分量取 0..255；
整型颜色约定见 `Chart.Rgb`（0xFFRRGGBB）。

- int r;

- int g;

- int b;

- int a;

- static Color FromRGB(int r, int g, int b)
  - 从 RGB 分量构造不透明颜色（alpha 固定 255）。分量取 0..255。

- static Color FromARGB(int a, int r, int g, int b)
  - 从 ARGB 分量构造颜色（a=0 全透明，a=255 不透明）。

- int ToColorRef()
  - 打包为 Windows COLORREF（0x00BBGGRR），供原生 API 直接使用。

- static Color Red()
  - 不透明红。

- static Color Green()
  - 不透明绿。

- static Color Blue()
  - 不透明蓝。

- static Color White()
  - 不透明白。

- static Color Black()
  - 不透明黑。

- static Color Gray()
  - 不透明灰（128,128,128）。

- static Color Yellow()
  - 不透明黄。

- static Color Cyan()
  - 不透明青。

- static Color Magenta()
  - 不透明品红。

- static Color Orange()
  - 不透明橙（255,165,0）。


## ImageBuffer (class)

纯 Zan 自举的托管 32 位 ARGB 图像缓冲区。
封装内存管理、边界安全与高性能图像处理流水线（Crop、Resize、Blit、Fill）。
所有热路径计算全部下沉至底层物理原语（NativeMemory.Copy2D 与 PixelOps），
杜绝 C 业务胶水依赖，具备工业级运行效率。

- public int Width;

- public int Height;

- public int Stride;

- public nint Pixels;

- public ImageBuffer(int width, int height)

- public void Dispose()

- public void Clear(int color)
  - 以指定 32 位 ARGB 颜色清空图像。

- public void FillRect(int x, int y, int w, int h, int color)
  - 在指定矩形区域填充 32 位 ARGB 颜色（自动边界裁剪）。

- public ImageBuffer Crop(int x, int y, int w, int h)
  - 裁剪子区域并返回新的 ImageBuffer（由 Copy2D 物理加速，零冗余遍历）。

- public ImageBuffer Resize(int newWidth, int newHeight)
  - 双线性重采样拉伸到目标尺寸（由 ResampleBilinearRow 向量化物理加速）。

- public void Blit(ImageBuffer src, int dx, int dy, bool alphaBlend)
  - 将另一个 ImageBuffer 绘制到当前图像（支持直接覆盖与 Porter-Duff Over 混合）。

- public void SwapRB()
  - 将当前图像的通道在 RGBA 与 BGRA 之间翻转。

- public void AdjustBrightness(int delta)
  - 调整整幅图像的明暗度（delta 为 -255..255）。基于 AVX2 饱和加减硬件指令，杜绝溢出反转。

- public void Blend50(ImageBuffer other)
  - 50% 均值混合（基于 AVX2 vpavgb 32 字节并行硬件指令）。

- public void Darken(ImageBuffer other)
  - 变暗混合（Min 滤镜，基于 AVX2 vpminub 32 字节并行硬件指令）。

- public void Lighten(ImageBuffer other)
  - 变亮混合（Max 滤镜，基于 AVX2 vpmaxub 32 字节并行硬件指令）。

- public void Invert()
  - 颜色反相（基于 AVX2 32 字节并行 XOR 硬件流水线）。

- public void Grayscale()
  - 灰度化（基于定点数加权通道融合 Y = (B*29 + G*150 + R*77) >> 8）。保留 Alpha 通道不变。

- public void BoxBlur(int radius)
  - 高性能双向可分离均值盒式模糊（Box Blur，可用于毛玻璃特效与平滑）。
    radius: 模糊半径（像素），必须 >= 1。
    采用定点数倒数乘法加速，彻底消除每像素除法开销。


## PixelOps (class)

底层高性能像素操作物理原语（硬件加速与向量化）。
由编译器 irgen 直接降低为 LLVM 向量化内部循环与指令，
供上层 ImageBuffer、GUI 渲染器与游戏引擎自举调用。

- [DllImport("crt")]static extern void BlendOver(nint dst, nint src, int count);
  - 将 count 个 32 位 ARGB 像素使用标准 Porter-Duff Over 算法合成到 dst。

- [DllImport("crt")]static extern void SwapRB(nint dst, nint src, int count);
  - 交换 count 个 32 位像素的 R 和 B 通道（RGBA ↔ BGRA）。

- [DllImport("crt")]static extern void FillRect(nint dst, int dstStride, int x, int y, int w, int h, int color);
  - 快速填充指定矩形区域的 32 位像素颜色。

- [DllImport("crt")]static extern void ResampleBilinearRow(nint dst, nint src0, nint src1, nint xIndices, nint xWeights, int weightY, int width);
  - 双线性重采样单行：根据 X 轴源索引、X 轴权重数组和当前行 Y 权重，
    对 src0 和 src1 两行像素进行水平加垂直双线性插值，输出 width 个像素到 dst。
    xWeights 与 weightY 范围为 0..256（定点数）。


## Point (class)

表示一个二维点。x 向右、y 向下（屏幕坐标）。

- public int x;
  - X 坐标（向右）。

- public int y;
  - Y 坐标（向下）。

- public Point(int x, int y)
  - 构造点。


## Rectangle (class)

表示一个矩形：左上角 (x,y) 与宽高；Right/Bottom 为开区间边界。

- public int x;
  - 左上角 X。

- public int y;
  - 左上角 Y。

- public int width;
  - 宽度。

- public int height;
  - 高度。

- public Rectangle(int x, int y, int w, int h)
  - 构造矩形（左上角 + 宽高）。

- int Right()
  - 右边界（x + width，开区间）。

- int Bottom()
  - 下边界（y + height，开区间）。

- bool Contains(int px, int py)
  - 点 (px,py) 是否落在矩形内（左闭右开：含左/上边，不含右/下边）。


## Size (class)

表示一个二维尺寸（宽 × 高，正值为常规方向）。

- public int width;
  - 宽度。

- public int height;
  - 高度。

- public Size(int w, int h)
  - 构造尺寸。
