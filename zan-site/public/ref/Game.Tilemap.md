# Game.Tilemap

> 源码: `packages/Zan.Game/src/Game/Tilemap/Tilemap.zan`


## TileLayer (class)

2D 瓦片图层数据结构。管理固定尺寸 (cols x rows) 的网格单元 ID。

- string name;

- int cols;

- int rows;

- int tileSize;

- List<int> tiles;

- TileLayer(string name, int cols, int rows, int tileSize)

- string Name()

- int Cols()

- int Rows()

- int TileSize()

- double WorldWidth()
  - 获取整张地图的世界坐标总宽度

- double WorldHeight()
  - 获取整张地图的世界坐标总高度

- int GetTile(int col, int row)
  - 根据网格列与行获取瓦片 ID，超出边界返回 0

- void SetTile(int col, int row, int tileId)
  - 设置网格指定列与行的瓦片 ID

- int WorldToCol(double worldX)
  - 世界坐标转网格列号

- int WorldToRow(double worldY)
  - 世界坐标转网格行号

- double ColToWorldX(int col)
  - 网格列转换为世界坐标左侧边缘 X

- double RowToWorldY(int row)
  - 网格行转换为世界坐标顶部边缘 Y

- List<Rect2> ExtractColliders(int solidTileId)
  - 从实心碰撞瓦片自动提取并合并优化碰撞矩形。
    采用水平行连续合并（Row Merging），将相邻连续的实心瓦片合并为一个长矩形，
    大幅降低物理检测计算开销与内部卡缝问题。

- void LoadCsv(string csv)
  - 解析逗号或换行分隔的 CSV 格式地图数据填充当前图层。
    格式兼容 Tiled CSV 导出与手写 ASCII 数值网格。
