# Zan.DataFrame

纯 Zan 表格数据核心：列式 DataFrame（类型推断、过滤、稳定排序、
分组聚合、哈希内连接），CSV 读写消费 `Zan.Csv`（`System.Csv`）。
零原生依赖、零反射——`using System.Data;` 按需拉入，命名空间与
Zan.Data 共存。

## 内容（namespace → 内容）

- `System.Data` — `DataFrame`（列存 + 查询原语）、`DataCol`、
  `RowGroup`、`ColKind`/`Agg` 枚举（CSV 的 `Csv`/`CsvTable` 已迁至
  `packages/Zan.Csv` 的 `System.Csv`）
- `System.Data` — `Stats`（描述统计：均值/中位数/分位/方差/标准差/
  偏度/峰度/相关系数）、`DataFrameStats`（按列名取统计的入口）
- `System.Data` — `Reshape`（选列/算列/去重/拼接/宽转长/长转宽/转置）、
  `Pivot`（透视表/交叉表）、`Merge`（内/左/右/外/交叉连接 + 多键）、
  `Window`（滑动窗口/指数加权/差分/有效性掩码）、
  `Resample`（秒/分/时/日/周/月重采样）、`Freq` 枚举
- `System.Data` — `Missing`（缺失值：`IsNaN`/`NotNaN`/`Mask`/`ValidMask`/
  `NaNCount`/`ValidCount`、`DropNa`、`FillNa`/`FillNaByColumn`/
  `FillNaStat`）——列存的"缺失"由独立的布尔掩码表达，详见下文

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
- **CSV 方言**（经 `Zan.Csv`）：RFC 4180——引号字段可含逗号/换行，
  `""` 转义，CRLF/LF/CR 记录分隔，开头 UTF-8 BOM 剥除；字段两侧
  空白属字段本身不裁剪。写出与解析严格互逆。
- **稳定排序**：归并实现，相等键保持原相对顺序。
- **Join**：内连接哈希实现；右表键列不进结果，其余右表列与左表
  撞名时加 `_1` 后缀。
- **GroupBy**：返回的 `RowGroup` 持原表行号，配 `Sum/Avg/Min/Max/
  Count`（`Aggregate`）使用；组按键首次出现顺序排列。
- **描述统计**（`Stats`）：吃 `List<double>`（已解析的数值列），
  不自己认列类型——列到数值的转换由 `DataFrameStats.Column` 统一做
  （Int/Float 直取，Str 按 strtod 前缀），转换点只有一处。
  边界语义：空集返回 0.0（聚合查询里空组很常见，不抛）；常量序列的
  偏度/峰度返回 0 而非 NaN（NaN 会污染下游图表）；分位数用线性插值
  （与 numpy 默认一致）。

## 快速上手（统计）

```zan
using System.Data;

DataFrame sales = DataFrame.FromCsv(File.ReadAllText("sales.csv"));
double med  = DataFrameStats.Median(sales, "price");       // 中位数
double p90  = DataFrameStats.Percentile(sales, "price", 0.9);
double std  = DataFrameStats.StdDev(sales, "price");
double corr = DataFrameStats.Correlation(sales, "qty", "price");

// 也可直接吃数值序列（不经过 DataFrame）：
List<double> xs = new List<double>();
xs.Add(2.0); xs.Add(4.0); xs.Add(9.0);
double m = Stats.Median(xs);
```

## 快速上手（表级变换）

```zan
using System.Data;

// 选列 + 算列（不改入参，返回新表）
DataFrame proj = Reshape.Select(sales, "city", "qty");
DataFrame calc = Reshape.WithColumn(sales, "qty2", ColKind.Int,
    (int row) => sales.GetFloat(row, "qty") * 2.0);

// 宽转长 → 透视 → 直接给图表
List<string> ids = new List<string>(); ids.Add("city");
List<string> vals = new List<string>(); vals.Add("qty");
DataFrame long = Reshape.Melt(sales, ids, vals);
DataFrame wide = Pivot.PivotTable(long, "city", "variable", "qty", Agg.Sum);

// 连接（多键 / 外连接）
List<string> lk = new List<string>(); lk.Add("city"); lk.Add("date");
DataFrame merged = Merge.MergeTables(left, right, lk, lk, MergeKind.Outer);

// 时序：日聚合 + 7 日移动平均
DataFrame daily = Resample.ResampleBy(series, "t", "v", Freq.Day, Agg.Sum);
DataFrame ma = Window.Rolling(daily, "v", 7, Agg.Avg);   // 结果列 v_rolling
```

## 设计要点（表级变换）

- **全部返回新表，不改入参**。行序有明确规则（各函数注释里写明），
  因为确定性的行序是 golden 对比的前提。
- **窗口结果写进新列**（Float 型），不覆盖输入列——输入列可能是
  Int 型，覆盖会把小数截断（窗口均值 16.67 被抹成 16）。默认列名
  `<列名>_rolling` / `<列名>_ewm` / `<列名>_diff`。
- **"算不出的行"不写 NaN**。Zan 没有 NaN 字面量（`0.0/0.0` 能造出
  NaN，但它穿过格式化与图表链路会渲染成平台相关的文本，还会污染
  下游平均），所以本包用"不写值 + 有效性掩码"表达。`minPeriods` 默认 1
  （窗口不满也出值），显式给 >1 时不足样本的行不写值，要判哪些行真算得出用
  `RollingValid` 拿有效性掩码。
- **重采样的空桶不产生行**（不是补 0）——补 0 会让"这段时间没数据"
  和"这段时间销量为 0"混为一谈。桶边界左闭右开，周桶起点是周一
  00:00（以 Unix 纪元对齐，时区无关）。
- **外连接的键从右表补齐**：右表键列不进结果（与 `Join` 一致），
  但右表独有行的左表键列取右表的值。
- **枚举是顶层类型**：`Freq`/`Agg`/`ColKind`/`MergeKind` 都在
  `namespace System.Data`——`Resample.Freq.Second` 这样的嵌套枚举
  编译器解析不了。

## 消费者

- `packages/Zan.Desktop` 的 `TaskScheduler.ParseCsv`（schtasks
  CSV 输出行解析，经 `Zan.Csv` 的 `Csv.ParseRow`）
- 需要表格数据解析的任何程序（报表、日志归并、数据导出）
- `packages/Zan.Gui.Charts` 的 `ChartBridge`（DataFrame → 图表数据）

## 已知边界

- 无 Parquet/Excel 等二进制格式（Xlsx 见 `Zan.Data`）；聚合以 double
  返回（Int 列求和内部走 long 累加避免精度损失）。
- 数值形态识别是严格的（可选符号 + 数字 [+ 小数部分] [+ 指数]），
  带 BOM/空白/千分位的字段按文本列处理。
- 无多级索引（`MultiIndex`）与插值（`interpolate`——缺失值处理见下）。
- `Aggregate` 只认固定的 5 个算子（`Agg`），没有 `groupby().agg()`
  那样的自定义聚合字典。
- `Concat` 按第一张表的列名对齐，不做重命名；`Resample` 无升采样与
  时间偏移（`asfreq`/`offset`）。

## 缺失值

列存是强类型的 `List<long>` / `List<double>` / `List<string>`——格子里
**总有值**，所以"这行没填"与"这行填了 0"分不开。本包用**独立的布尔
掩码**（`DataCol.missing`，与值列表等长）表达"缺失"，不用哨兵值。

**为什么不用 `0` 当哨兵**：`0` 与 `""` 都是合法值。拿 `0` 当缺失的
哨兵，会把"这行没填"和"这行填了 0"混为一谈——而这两者的处理方式
常常相反（前者该 drop，后者该保留）。pandas 的 `NaN` 之所以好使，
是因为 double 有专门的 NaN 位；Zan 的列存是强类型的，没有 NaN 语义。

```zan
using System.Data;

DataFrame df = DataFrame.FromCsv("a,b\r\n1,\r\n2,5\r\n");   // b 第一行是空的

Missing.NaNCount(df, "b");        // 1
Missing.IsNaN(df, 0, "b");        // true
Missing.DropNa(df, "any");        // 丢含缺失的行
Missing.FillNa(df, "0", "b");     // 填 0
Missing.FillNaStat(df, "mean", "b");  // 填均值（Int 列自动升 Float）
```

- **CSV 约定**：空字段 → Int/Float 列的**缺失**（不是 0）；Str 列的空
  字段仍是空串（Str 列没有独立的缺失态）；整列全空 → 推断为 Str。
  空字段**不参与类型推断**——否则 `1,,3` 会被推成 Str，数字列退化成
  文本列。
- **聚合跳过缺失**：求和/均值/最值只看有效格。全列无有效值时返回
  0.0（不是 NaN——NaN 会顺着下游的平均再平均，污染出一片垃圾）。要
  区分"没有有效值"与"有效值恰好是 0"，用 `Missing.ValidCount`。
- **写入即"有值"**：`SetInt`/`SetFloat`/`SetStr` 写非空值会清掉缺失
  标记；写空串到 Int/Float 列会**重新**置为缺失（与 CSV 装载同口径）。
- **升型而不是截断**：往 Int 列填均值 1.5 会截成 1，静默算错。
  `FillNaStat` 遇到带小数的填充值先把列**升成 Float**（`CastColumn`），
  再填。
- **新行/新列的格子是缺失不是 0**：`AddRow` 与 `AddColumn` 造出来的
  数值格标记为缺失——"这格没填"与"这格填了 0"是两回事。
