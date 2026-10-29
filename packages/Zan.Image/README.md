# Zan.Image

独立高性能图像解码与光栅化运行时套件（无需窗口系统或图形渲染器环境）。

`Zan.Image` 封装了跨平台的底层图像解码引擎（stb_image / nanosvg 等），支持常见光栅格式解码、SVG 矢量图按指定比例光栅化以及像素内存高速缓存，像素格式统一为直通 Alpha 的 32 位 ARGB32。

命名空间为 `System.Drawing.Imaging`，通过 `using System.Drawing.Imaging;` 引入使用。

---

## 支持格式

- **光栅图片格式**：PNG、JPEG、BMP、GIF、TGA、PNM、PSD、WebP 及 ICO 格式子图。
- **矢量图片格式**：SVG（支持按目标宽高按比例光栅化并写入缓存）。
- **内存图片**：支持直接从内存字符串或 `byte[]` 字节流加载，key 约定使用 `"mem:"` 前缀。

---

## 核心接口：`ImageCodec`

| 方法 | 说明 |
| :--- | :--- |
| `ImageCodec.Width(key)` | 获取文件或已注册 key 的像素宽度；首次访问文件时自动解码，失败返回 0 |
| `ImageCodec.Height(key)` | 获取文件或已注册 key 的像素高度，失败返回 0 |
| `ImageCodec.LoadMem(key, data, len)` | 将内存中的字符串或 `byte[]` 字节解码并注册到指定 key |
| `ImageCodec.LoadSvg(key, svg, rasterW, rasterH)` | 将 SVG 矢量文本按目标宽高比例光栅化并存入 key |
| `ImageCodec.Evict(key)` | 主动淘汰并释放指定 key 的像素内存与解码缓存 |
| `ImageCodec.View(key)` | 获取底层 native 图像描述符句柄（仅供图形引擎同步转发） |
| `ImageCodec.MemReport()` | 返回当前解码缓存条目数及像素内存占用的诊断文本 |

---

## 快速上手

```zan
using System;
using System.Drawing.Imaging;

class Program {
    static void Main() {
        // 1. 查询文件图片尺寸（首次调用自动解码并缓存）
        int w = ImageCodec.Width("assets/logo.png");
        int h = ImageCodec.Height("assets/logo.png");
        Console.WriteLine("Image size: " + Convert.ToString(w) + "x" + Convert.ToString(h));

        // 2. 从内存字节流加载
        byte[] bytes = System.IO.File.ReadAllBytes("assets/avatar.jpg");
        int loadW = ImageCodec.LoadMem("mem:user_avatar", bytes, bytes.Length);

        // 3. 矢量 SVG 光栅化为 64x64 图标
        string svgText = "<svg width=\"24\" height=\"24\">...</svg>";
        ImageCodec.LoadSvg("mem:icon_save", svgText, 64, 64);

        // 4. 打印缓存占用报告
        Console.WriteLine(ImageCodec.MemReport());

        // 5. 不再使用时释放缓存
        ImageCodec.Evict("mem:user_avatar");
    }
}
```

---

## 原生驱动支持

内置跨平台预编译静态驱动（`libzan_image`），开箱即用支持：
- **Windows** (`win-x64`, `win-arm64`)
- **Linux** (`linux-x64`, `linux-arm64`)
- **macOS** (`macos-x64`, `macos-arm64`)
- **Android** (`android-x64`, `android-arm64`)
- **OpenHarmony / 鸿蒙** (`ohos-x64`, `ohos-arm64`)
