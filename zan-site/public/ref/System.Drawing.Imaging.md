# System.Drawing.Imaging

> 源码: `packages/Zan.Image/src/System/Drawing/Imaging/ImageCodec.zan`


## ImageCodec (class)

- static int Width(string key)

- static int Height(string key)

- static int LoadMem(string key, string data, int len)

- static int LoadMem(string key, byte[]data, int len)

- static int LoadSvg(string key, string svg, int rasterW, int rasterH)

- static int LoadSvg(string key, byte[]svg, int len, int rasterW, int rasterH)

- static void Evict(string key)

- static nint View(string key)

- static string MemReport()

- [DllImport("zan_image")]static extern int zan_image_width(string key);

- [DllImport("zan_image")]static extern int zan_image_height(string key);

- [DllImport("zan_image")]static extern int zan_image_load_mem(string key, string data, int len);

- [DllImport("zan_image")]static extern int zan_image_load_mem_bytes(string key, byte[]data, int len);

- [DllImport("zan_image")]static extern int zan_image_load_svg(string key, string text, int len, int rasterW, int rasterH);

- [DllImport("zan_image", EntryPoint="zan_image_load_svg")]static extern int zan_image_load_svg_bytes(string key, byte[]text, int len, int rasterW, int rasterH);

- [DllImport("zan_image")]static extern void zan_image_evict(string key);

- [DllImport("zan_image")]static extern nint zan_image_get(string key);

- [DllImport("zan_image")]static extern string zan_image_mem_report();
