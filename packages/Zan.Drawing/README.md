# Zan.Drawing

Zan 2D 绘图基元、像素处理、图像解码与打印服务包。

## 命名空间与能力概览

- `System.Drawing`：
  - 基础几何结构体：`Point`、`Size`、`Rectangle`、`Color`
  - 托管 32 位 ARGB 像素缓冲：`ImageBuffer`（支持裁切、缩放、贴图、颜色变换、盒式模糊等）
  - 底层硬件 SIMD 像素原语：`PixelOps`（`BlendOver`、`SwapRB`、`FillRect`、`ResampleBilinearRow`）
  - RAW 打印系统：`System.Drawing.Printing.RawPrinter`、`PrinterSettings`（Windows `winspool` / POSIX CUPS）
- `System.Drawing.Imaging`：
  - 独立图像解码与光栅化缓存：`ImageCodec`（基于纯 C `zan_image` 原生驱动，支持 PNG / JPEG / BMP / GIF / TGA / PNM / PSD / WebP / ICO / SVG 解码与内存管理）
