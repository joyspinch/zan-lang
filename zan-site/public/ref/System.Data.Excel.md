# System.Data.Excel

> 源码: `packages/Zan.Data/src/System/Data/Excel/Xlsx.zan`


## Xlsx (class)

Xlsx 模块的共享工具：XML 转义、列号转字母、
double 的精确文本化与显示宽度估算。

- static string Esc(string s)
  - XML 文本转义：& < >，并剥掉 XML 1.0 不允许的
    控制字符（保留制表符、换行、回车）。

- static string EscAttr(string s)
  - 属性值转义：在 `Esc` 基础上再转义双引号（单趟流式切片）。

- static string ColRef(int c)
  - 0 → A、25 → Z、26 → AA……（Excel 列引用）。

- static string RealToText(double v)
  - double → 十进制文本：从 0 位小数起逐位尝试，取首个能按
    相对容差往返的表示；|v| ≥ 1e15 或非零 |v| < 1e-9 时写科学
    计数法。绝不用 Convert.ToString(double)——它只有约 6 位
    有效数字，123.45 会被写成 123.45（碰巧）但 123.4567 变 123.457。

- static string Shortest(double a, int maxDec)
  - 0..maxDec 位小数逐个尝试，返回首个按相对容差 1e-15
    往返的定点表示；都不行则返回最长表示兜底。

- static string FixedReal(double a, int decimals)
  - a >= 0：渲染为恰好 `decimals` 位小数（四舍五入）。

- static double ParseFixed(string s)
  - 严格十进制解析（我自己生成的文本，格式可控）。

- static double Pow10(int n)
  - 以 double 表示的 10^n。

- static int WideLen(string s)
  - 显示宽度估算：CJK/全角按 2，其他按 1（代理对按 2）。
    Excel 的列宽单位约等于一个 ASCII 字符，这只求观感不较真。


## XlsxBook (class)

极简 XLSX（Office Open XML SpreadsheetML）写出器，纯 Zan 实现：
每张工作表一个 worksheet XML，连同 workbook/styles/rels 一起用
`Zip` 打包。字符串写 inlineStr（免共享字符串表），
支持文本/数字/布尔/日期单元格、表头加粗、冻结首行与列宽自适应；
不写时间戳，同一本书每次生成的字节完全一致。
与 Excel、WPS、LibreOffice、openpyxl 互通。

XlsxBook book = XlsxBook.New();
XlsxSheet sh = book.AddSheet("报表");
List<XlsxCell> head = new List<XlsxCell>();
head.Add(XlsxCell.Str("品名"));
head.Add(XlsxCell.Str("单价"));
sh.AddRow(head);
sh.SetCell(1, 0, XlsxCell.Str("椅子"));
sh.SetCell(1, 1, XlsxCell.Num(123.45));
sh.SetCell(1, 2, XlsxCell.Date(20458));   // 2026-01-05
book.Save("out.xlsx");                    // 或 SaveToBytes() 给 HTTP 下载

- List<XlsxSheet> sheets;

- XlsxBook()
  - 私有构造；统一经 `New`/`FromText` 创建。

- static XlsxBook New()
  - 新建空工作簿。

- XlsxSheet AddSheet(string name)
  - 添加工作表并返回它。名字会清洗 Excel 的非法字符
    （: \ / ? * [ ] 与首尾撇号）、截到 31 字符，并与已有表名去重。

- static XlsxBook FromText(List <List<string>> rows, bool firstRowIsHeader)
  - 便捷构建：全文本表格。`firstRowIsHeader` 为 false 时关闭
    表头加粗与冻结；需要数字/日期/布尔请用 AddSheet + XlsxCell。

- byte[]SaveToBytes()
  - 生成完整 xlsx 字节（zip 包）。输出只由数据决定，可复现。

- void Save(string path)
  - 写入 .xlsx 文件。路径打不开时抛 `IOException`。

- static int maxRowsPerSheet=1048576;
  - 单张工作表的最大行数（xlsx 格式上限 1048576）。流式写
    出超出时自动续接分表；测试/演示可调小以驱动分表路径。

- bool SaveStreaming(string path, List<XlsxRowSource> sources)
  - 流式写出：工作表内容经 <paramref name="sources"/>（与 sheets
    一一对应，缺位的用空源）逐行供给，边生成边压缩落盘，内存
    占用与总行数无关。单表超过 `maxRowsPerSheet` 时
    自动续接分表（原表名后缀 2、3……去重；表头/冻结窗格留在
    第一分表）。数据源的 Cancelled() 返回 true 时中止写盘、
    删除半成品并返回 false；路径打不开时抛 IOException。
    输出同样不含时间戳，可复现。

- static string PartName(List<XlsxSheet> sheets, List<XlsxSheet> parts, string baseName)
  - 分表命名：`baseName` 撞上现有表或已写分表时后缀 2、3……。

- static bool StreamSheetPart(XlsxSheet s, XlsxRowSource src, int rowStart, int rows, ZipWriter zw, int partNo, int total)
  - 写出一个分表的 worksheet 部件：表头加粗/冻结窗格只在分表
    覆盖绝对第 0 行时生效；列宽对区间前 200 行做预扫（FillRow
    必须可重入）；行体攒到 32KB 冲一次压缩流。返回 false =
    数据源取消/失败。

- static ZipEntry Part(string name, string xml)
  - 以 (部件名, XML 文本) 组装一个 zip 条目。

- static int IndexOfSheet(List<XlsxSheet> sheets, string name)
  - 表名在清单中的下标，不存在时为 -1。

- static string ContentTypes(int sheetCount)
  - ---------- OPC 部件 ----------

- static string RootRels()
  - 包级关系：workbook 与两个 docProps 部件。

- static string CoreProps()
  - 核心属性（创建者）；不写时间戳以保持输出可复现。

- static string AppProps()
  - 应用属性（生成程序名）。

- static string WorkbookXml(List<XlsxSheet> sheets)
  - workbook.xml：表名与 sheetId/rId 清单。

- static string WorkbookRels(int sheetCount)
  - workbook 的关系：每张表一个 rId，最后一个是 styles。

- static string StylesXml()
  - 样式表：s=0 默认，s=1 日期（numFmt 164 = yyyy-mm-dd），
    s=2 表头加粗，s=3 日期 + 加粗。numFmts 必须在 fonts 之前
    （schema 顺序），Excel 才认。

- static string SheetXml(XlsxSheet s)
  - 单张工作表的完整 worksheet XML（内存版，见 `SaveToBytes`）。

- static void AppendCellXml(StringBuilder sb, XlsxCell cell, int row1, int col, bool bold)
  - 单个 <c> 单元格（含 r/s/t 属性与内容）追加到 sb。kind 0
    或空文本的单元格不产生输出。

- static int CellWidth(XlsxCell cell)
  - 单格的列宽贡献值（8 兜底；文本按显示宽、数字 +1、日期 10）。

- static int ColWidth(XlsxSheet s, int col)
  - 列宽估算：内容显示宽度 + 2 的余量，夹在 [10, 60]。
    只扫前 200 行——宽度是观感问题，不值得为十万行扫描。


## XlsxCell (class)

xlsx 单元格值。用静态构造器创建：`Str` 文本、
`Num` 数字、`Bool` 真值、`Date` 日期
（自 1970-01-01 的天数，与 DataTable 的日期键一致）。kind 0 的空
单元格（含 <c>Str("")</c>）在写出时跳过。

- int kind;
  - 0 空，1 文本，2 数字，3 布尔，4 日期。

- string text;
  - kind 1 的文本内容；kind 2 的十进制字面量——写入时即定形，
    避免 Convert.ToString(double) 只有约 6 位有效数字的损耗。

- int days;
  - kind 4：自 1970-01-01 起的天数（可为负）。

- bool flag;
  - kind 3 的真值。

- XlsxCell()
  - 私有构造；统一经静态工厂创建。

- static XlsxCell Str(string v)
  - 文本单元格；空字符串写出为空单元格。

- static XlsxCell Num(double v)
  - 数字单元格。写出取能精确往返的最短十进制表示。

- static XlsxCell Bool(bool v)
  - 布尔单元格（Excel 的 t="b"，显示为 TRUE/FALSE）。

- static XlsxCell Date(int unixDays)
  - 日期单元格：写出为 Excel 序列日期（1900 日期制），
    挂 yyyy-mm-dd 数字格式，Excel 里显示为日期而非数字。


## XlsxRowSource (class)

流式写出的行供给源：`XlsxBook.SaveStreaming` 按需调
`FillRow` 取行。行号从 0 起，第 0 行约定为表头（若
有）。FillRow 把第 `index` 行填进 `row`（先 Clear，行内单元格数
可参差）并返回 true；返回 false 视为数据失败、中止导出。数据源
必须可重入——列宽估算会对每个分表的前 200 行重新预扫一遍。
所有回调都在写出线程上发生；GUI 程序请用 App.Post 把进度切回
UI 线程再改控件。

- virtual int RowCount()
  - 数据总行数（含表头行）。

- virtual bool FillRow(int index, List<XlsxCell> row)
  - 填第 `index` 行。返回 false 中止导出（文件被删除）。

- virtual void OnProgress(int done, int total)
  - 已写完 `done`/`total` 行（写线程回调，约每 512 行一次）。

- virtual bool Cancelled()
  - 返回 true 中止导出：半成品被删除，SaveStreaming 返回 false。


## XlsxSheet (class)

xlsx 工作表。行是 `XlsxCell` 列表（可参差，
缺的列按空处理）；默认开启表头加粗（首行）、冻结首行与列宽
自适应，按需关掉即可。

- public string name;

- public bool headerBold;
  - 首行按表头处理：加粗。默认 true。

- public bool freezeHeader;
  - 冻结首行（Excel 的冻结窗格）。默认 true。

- public bool autoWidth;
  - 按内容估算列宽（前 200 行）。默认 true。

- List <List<XlsxCell>> rows;

- int maxCols;

- XlsxSheet(string name)
  - 私有构造；经 `XlsxBook.AddSheet` 创建。

- void AddRow(List<XlsxCell> cells)
  - 追加一行。行内单元格数可以少于其他行。

- void SetCell(int row, int col, XlsxCell v)
  - 写入指定单元格，行/列自动扩展；中间缺位补空单元格。

- int RowCount()
  - 行数。
