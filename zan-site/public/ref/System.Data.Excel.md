# System.Data.Excel

> 源码: `packages/Zan.Data/src/System/Data/Excel/Xlsx.zan`


## Xlsx (class)

- static string Esc(string s)

- static string EscAttr(string s)

- static string ColRef(int c)

- static string RealToText(double v)

- static string Shortest(double a, int maxDec)

- static string FixedReal(double a, int decimals)

- static double ParseFixed(string s)

- static double Pow10(int n)

- static int WideLen(string s)


## XlsxBook (class)

- List<XlsxSheet> sheets;

- XlsxBook()

- static XlsxBook New()

- XlsxSheet AddSheet(string name)

- static XlsxBook FromText(List <List<string>> rows, bool firstRowIsHeader)

- byte[]SaveToBytes()

- void Save(string path)

- static int maxRowsPerSheet=1048576;

- bool SaveStreaming(string path, List<XlsxRowSource> sources)

- static string PartName(List<XlsxSheet> sheets, List<XlsxSheet> parts, string baseName)

- static bool StreamSheetPart(XlsxSheet s, XlsxRowSource src, int rowStart, int rows, ZipWriter zw, int partNo, int total)

- static ZipEntry Part(string name, string xml)

- static int IndexOfSheet(List<XlsxSheet> sheets, string name)

- static string ContentTypes(int sheetCount)

- static string RootRels()

- static string CoreProps()

- static string AppProps()

- static string WorkbookXml(List<XlsxSheet> sheets)

- static string WorkbookRels(int sheetCount)

- static string StylesXml()

- static string SheetXml(XlsxSheet s)

- static void AppendCellXml(StringBuilder sb, XlsxCell cell, int row1, int col, bool bold)

- static int CellWidth(XlsxCell cell)

- static int ColWidth(XlsxSheet s, int col)


## XlsxCell (class)

- int kind;

- string text;

- int days;

- bool flag;

- XlsxCell()

- static XlsxCell Str(string v)

- static XlsxCell Num(double v)

- static XlsxCell Bool(bool v)

- static XlsxCell Date(int unixDays)


## XlsxRowSource (class)

- virtual int RowCount()

- virtual bool FillRow(int index, List<XlsxCell> row)

- virtual void OnProgress(int done, int total)

- virtual bool Cancelled()


## XlsxSheet (class)

- public string name;

- public bool headerBold;

- public bool freezeHeader;

- public bool autoWidth;

- List <List<XlsxCell>> rows;

- int maxCols;

- XlsxSheet(string name)

- void AddRow(List<XlsxCell> cells)

- void SetCell(int row, int col, XlsxCell v)

- int RowCount()
