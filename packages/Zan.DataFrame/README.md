# Zan.DataFrame

纯 Zan 表格数据核心：RFC 4180 CSV 解析/写出 + 列式 DataFrame
（类型推断、过滤、稳定排序、分组聚合、哈希内连接）。零原生依赖、
零反射——`using System.Data;` 按需拉入，命名空间与 Zan.Data 共存。

## 内容（namespace → 内容）

- `System.Data` — `Csv`（Parse/ParseRow/Escape/RowToText）、
  `CsvTable`、`DataFrame`（列存 + 查询原语）、`DataCol`、`RowGroup`、
  `ColKind`/`Agg` 枚举

## 快速上手

```zan
using System.Data;

DataFrame sales = DataFrame.FromCsv(File.ReadAllText("sales.csv"));
DataFrame big = sales.Filter((int row) => sales.GetFloat(row, "qty") > 2.0);
big = big.SortBy("price", true);
List<RowGroup> gs = big.GroupBy("city");
for (int i = 0; i < gs.Count; i = i + 1) {
    double s = big.Sum(gs[i].Rows(), "qty");
}
DataFrame joined = big.Join(pop, "city", "city");   // 内连接哈希实现
string text = big.ToCsv();                          // 往返无损
```

## 设计要点

- **列存**：每列一个类型化列表（`long`/`double`/`string`），CSV
  装载时按整列内容推断（全整数 → Int，全数值 → Float，否则 Str；
  空列推断为 Str）。
- **跨类型访问自动转换**：Int 列 `GetStr` 得十进制文本，Float 列
  `GetInt` 截断；double ↔ 文本走最短往返格式，`Parse(ToCsv())` 等价。
- **CSV 方言**：RFC 4180——引号字段可含逗号/换行，`""` 转义，
  CRLF/LF/CR 记录分隔，开头 UTF-8 BOM 剥除；字段两侧空白属字段
  本身不裁剪。写出与解析严格互逆。
- **稳定排序**：归并实现，相等键保持原相对顺序。
- **Join**：内连接哈希实现；右表键列不进结果，其余右表列与左表
  撞名时加 `_1` 后缀。
- **GroupBy**：返回的 `RowGroup` 持原表行号，配 `Sum/Avg/Min/Max/
  Count`（`Aggregate`）使用；组按键首次出现顺序排列。

## 消费者

- `packages/Zan.Desktop` 的 `TaskScheduler.ParseCsv`（schtasks
  CSV 输出行解析，已收编为本包 `Csv.ParseRow`）
- 需要表格数据解析的任何程序（报表、日志归并、数据导出）

## 已知边界

- 无 Parquet/Excel 等二进制格式（Xlsx 见 `Zan.Data`）；无宽表
  （wide/reshape）操作；聚合以 double 返回（Int 列求和内部走
  long 累加避免精度损失）。
- 数值形态识别是严格的（可选符号 + 数字 [+ 小数部分] [+ 指数]），
  带 BOM/空白/千分位的字段按文本列处理。
