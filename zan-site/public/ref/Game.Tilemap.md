# Game.Tilemap

> 源码: `packages/Zan.Game/src/Game/Tilemap/Tilemap.zan`


## TileLayer (class)

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

- double WorldHeight()

- int GetTile(int col, int row)

- void SetTile(int col, int row, int tileId)

- int WorldToCol(double worldX)

- int WorldToRow(double worldY)

- double ColToWorldX(int col)

- double RowToWorldY(int row)

- List<Rect2> ExtractColliders(int solidTileId)

- List<Rect2> ExtractCollidersMatching(List<int> solidTileIds)

- List<Rect2> ExtractCollidersRange(int minTileId, int maxTileId)

- List<Rect2> ExtractCollidersOptimized2D(int solidTileId)

- List<Rect2> ExtractCollidersMatchingOptimized2D(List<int> solidTileIds)

- List<Rect2> ExtractCollidersRangeOptimized2D(int minTileId, int maxTileId)

- static bool ContainsId(List<int> list, int target)

- static List<Rect2> MergeBoxesVertically(List<Rect2> boxes, int tileSize)

- void LoadCsv(string csv)
