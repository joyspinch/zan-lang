# Game.Tools

> 源码: `packages/Zan.Game/src/Game/Tools/AssetPipeline.zan`


## AssetPipeline (class)

- static void CalculateOptimalAtlasSize(int frameWidth, int frameHeight, int totalFrames, out int atlasWidth, out int atlasHeight, out int cols, out int rows)

- static string GenerateAtlasManifestJson(string atlasName, int atlasWidth, int atlasHeight, int frameWidth, int frameHeight, int cols, int rows, int totalFrames)

- static string Generate5DirMappingJson(string characterModelName)

- static string GeneratePaperdollManifestJson(string charClass)


## FrameRect (class)

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
