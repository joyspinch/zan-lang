Game.Data — 数值表加载
=====================

策划用 Excel/CSV 改数值、游戏读表——这套加载与校验在任何"表格驱动"的
玩法里都成立，所以放在包里；**表里有哪些列、每列什么含义、取值范围多大，
是游戏自己的事**，那部分留在项目里（用 `TableSchema` 描述）。

| 文件 | 职责 |
|------|------|
| `DataRow.zan` | 一行：列名 → 原始文本；`Text` / `Num` / `Int` / `Long` / `Has` / `RequireText` / `RequireInt` |
| `DataTable.zan` | 一张表：读文件、解析 CSV、id 索引、`Find` / `Get` / `Names` |
| `TableSchema.zan` | 列规格串与校验：列齐全、类型合法、值在范围内 |
| `TableColumn.zan` | 一列的规格（Zan 不支持嵌套类，实体类型一个文件一个类） |
| `DataValues.zan` | 严格数值解析：整数/浮点的非法写法一律抛异常 |

口径
----
- **表里的一切都是文本**，取值靠 `Text`/`Num`/`Int` 显式转换。解析层不做
  隐式类型推断——"0" 是整数还是浮点由表结构决定，不由字符串长得像决定。
- **`Text` 宽松、`RequireText` 严格**：缺列时 `Text` 返回空串（当"没填"处理），
  `RequireText` 抛异常。关键列（id、倍率、掉落权重）用严格版本——写错列名
  要立刻暴露，不能悄悄变成 0。
- **严格数值解析**：`Convert.ToDouble("abc")` 在 Zan 里静默返回 0，所以
  `DataValues` 自己扫一遍字符：空串、非法字符、溢出都抛异常，带行列定位。
- **加载期就顶错**：表头重复列/空列名、每行列数不一致、id 为空或重复、
  表结构与规格不符——都在加载期抛异常，不留到运行时才发现数值不对。

用法
----
```zan
using Game.Data;

// 1. 读表（首行表头，UTF-8）
DataTable items = DataTable.Load("assets/data/items.csv");

// 2. 描述表结构并校验
TableSchema schema = TableSchema.Parse("id:int:1:999,name,hp:int:1:9999,rate:num:0:1");
schema.ValidateHeader(items.Header(), "items.csv");
for (int i = 0; i < items.Count(); i = i + 1) {
    schema.ValidateRow(items.RowAt(i), "items.csv");
}

// 3. 取值
DataRow sword = items.Get("1");
int hp = sword.Int("hp");
double rate = sword.Num("rate");
```

嵌入资源
--------
`DataTable.Load` 走 `File.ReadAllText`，**内嵌副本兜底**——单文件发布时
表在镜像里也能读（`File.ReadAllText` 找不到盘上文件时会读内嵌副本）。
`DataTable.Parse` 则直接吃文本，方便加密/压缩后再解析。
