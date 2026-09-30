# System.Drawing.Printing

> 源码: `packages/Zan.Desktop/src/System/Drawing/Printing/Printing.zan`


## CupsRawPrinterBackend (class)

CUPS 没有逐页的 spooler API：把 RAW 字节流收集到临时文件，
EndDoc 时用 `lp -o raw` 整份提交（-o raw 让 CUPS 跳过过滤链，
字节原样送给打印机，与 Windows 的 RAW 数据类型一致）。

- string printer;

- string document;

- string spoolPath;

- bool failed;

- bool Open(string printerName)
  - 记住打印机名；系统里有 lp 命令才算打开成功。

- bool StartDoc(string documentName)
  - 创建按 PID 命名的临时 spool 文件；创建失败返回 false。

- bool StartPage()
  - spool 文件就绪即成功（CUPS 无逐页提交的概念）。

- int Write(byte[]data)
  - 追加到 spool 文件，返回写入字节数；失败返回 -1 并标记文档失败。

- bool EndPage()
  - 本页是否无失败。

- bool EndDoc()
  - 用 `lp -d <printer> -t <doc> -o raw` 提交整份 spool 文件。

- void Abort()
  - 标记文档失败：EndDoc 将拒绝提交。

- bool Close()
  - 删除 spool 临时文件。

- static string Quote(string s)
  - 单引号包裹 shell 参数，内部单引号按 POSIX 方式（'\''）转义。


## PrinterSettings (class)

只读访问当前用户已安装的打印机。Windows 上走
winspool（EnumPrintersW/GetDefaultPrinterW），Linux/macOS 上走 CUPS
的 lpstat。不含网络发现，仅本地与已连接打印机。

- static List<string> InstalledPrinters()
  - 枚举本地打印机及打印机连接。列表可能为空
    （无打印机或 lpstat 不可用），不为 null。

- static string DefaultPrinterName()
  - 返回默认打印机名称；未配置时返回空字符串。

- [DllImport("winspool", EntryPoint="EnumPrintersW")]static extern int EnumPrintersW(int flags, nint name, int level, nint buffer, int bufferBytes, nint neededBytes, nint returnedCount);

- [DllImport("winspool", EntryPoint="GetDefaultPrinterW")]static extern int GetDefaultPrinterW(nint buffer, nint chars);

- static List<string> WinInstalledPrinters()
  - EnumPrintersW（PRINTER_INFO_4）两段式枚举：先探缓冲区大小再取数据。


## RawPrinter (class)

将已格式化的字节流直接发送到打印队列（Windows 走
winspool 的 RAW 数据类型，Linux/macOS 走 CUPS 的 `lp -o raw`）。
适用于打印机原生语言数据（如 ESC/POS、PCL）；不排版、不分页，
内容必须由调用方按目标打印机格式生成。

- static bool Send(string printerName, string documentName, byte[]data)
  - 发送一个 RAW 文档。字节原样传递给所选
    打印机驱动。documentName 为空时用默认名。
    任何后台打印操作失败时返回 false。

- internal static bool SendWithBackend(string printerName, string documentName, byte[]data, IRawPrinterBackend backend)
  - 针对注入的后端运行 RAW 后台打印协议。


## WinRawPrinterBackend (class)

winspool RAW 后端：协议各步一一对应 OpenPrinterW/
StartDocPrinterW/WritePrinter 等 spooler 调用。

- nint handle;

- bool Open(string printerName)
  - OpenPrinterW 打开打印机；调用成功且句柄非 0 才返回 true。

- bool StartDoc(string documentName)
  - StartDocPrinterW 以 RAW 数据类型开始作业（作业号非 0 为成功）。

- bool StartPage()
  - StartPagePrinter。

- int Write(byte[]data)
  - WritePrinter 写入 data，返回实际字节数；失败返回 -1。

- bool EndPage()
  - EndPagePrinter。

- bool EndDoc()
  - EndDocPrinter 提交作业。

- void Abort()
  - AbortPrinter 丢弃当前作业。

- bool Close()
  - ClosePrinter 关闭打印机句柄。

- [DllImport("winspool", EntryPoint="OpenPrinterW")]static extern int OpenPrinterW(nint printerName, nint handle, nint defaults);

- [DllImport("winspool", EntryPoint="StartDocPrinterW")]static extern int StartDocPrinterW(nint printer, int level, nint docInfo);

- [DllImport("winspool", EntryPoint="StartPagePrinter")]static extern int StartPagePrinter(nint printer);

- [DllImport("winspool", EntryPoint="WritePrinter")]static extern int WritePrinter(nint printer, byte[]data, int count, nint written);

- [DllImport("winspool", EntryPoint="EndPagePrinter")]static extern int EndPagePrinter(nint printer);

- [DllImport("winspool", EntryPoint="EndDocPrinter")]static extern int EndDocPrinter(nint printer);

- [DllImport("winspool", EntryPoint="AbortPrinter")]static extern int AbortPrinter(nint printer);

- [DllImport("winspool", EntryPoint="ClosePrinter")]static extern int ClosePrinter(nint printer);


## IRawPrinterBackend (interface)

RAW 打印状态机使用的后台打印程序操作。Open/StartDoc/
StartPage/Write/EndPage/EndDoc/Close 按此顺序调用；失败路径用
Abort 替代 EndDoc。一个文档一页一份数据的简化协议。

- bool Open(string printerName);
  - 打开与打印机的连接；失败返回 false。

- bool StartDoc(string documentName);
  - 开始一个 RAW 文档；失败返回 false。

- bool StartPage();
  - 开始一页；失败返回 false。

- int Write(byte[]data);
  - 写入一页数据，返回实际写入的字节数；失败返回 -1。

- bool EndPage();
  - 结束当前页；失败返回 false。

- bool EndDoc();
  - 正常结束文档并提交打印；失败返回 false。

- void Abort();
  - 放弃当前文档（StartDoc 成功而后续失败时替代 EndDoc）。

- bool Close();
  - 关闭连接；关闭失败时整体结果按失败计。
