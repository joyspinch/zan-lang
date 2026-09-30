# Game.Tools

> 源码: `packages/Zan.Game/src/Game/Tools/AssetPipeline.zan`


## AssetPipeline (class)

游戏素材自动化处理管线：
1. 图集栅格与紧凑布局烘焙计算（AtlasBaker）；
2. 5 方向切片映射为 8 方向元数据导出（FiveDirProcessor）；
3. 纸娃娃部位层深与挂点配置文件生成（PaperdollConfigGenerator）。

- static void CalculateOptimalAtlasSize(int frameWidth, int frameHeight, int totalFrames, out int atlasWidth, out int atlasHeight, out int cols, out int rows)
  - 计算将 N 个等宽高帧紧凑打包进 Atlas 纹理的规格（自动选取最接近的 2 的幂次方尺寸，如 512, 1024, 2048）。

- static string GenerateAtlasManifestJson(string atlasName, int atlasWidth, int atlasHeight, int frameWidth, int frameHeight, int cols, int rows, int totalFrames)
  - 生成包含每个切片 UV 坐标的标准图集描述元数据 JSON 文本。

- static string Generate5DirMappingJson(string characterModelName)
  - 生成 5 方向转 8 方向的映射对照元数据 JSON 文本。

- static string GeneratePaperdollManifestJson(string charClass)
  - 生成纸娃娃装备部件与层深遮挡矩阵配置 JSON 文本。


## FrameRect (class)

图集切片矩形元数据。

- int id;

- int x;

- int y;

- int width;

- int height;

- float u0;

- float v0;

- float u1;

- float v1;

- FrameRect(int id, int x, int y, int w, int h, float u0, float v0, float u1, float v1)

- static FrameRect Create(int id, int x, int y, int w, int h, float u0, float v0, float u1, float v1)

- int Id { get }

- int X { get }

- int Y { get }

- int Width { get }

- int Height { get }

- float U0 { get }

- float V0 { get }

- float U1 { get }

- float V1 { get }
