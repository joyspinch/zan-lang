# System.Drawing.Printing

> 源码: `stdlib/System/Drawing/Printing/Printing.zan`


## CupsRawPrinterBackend (class)

- string printer;

- string document;

- string spoolPath;

- bool failed;

- bool Open(string printerName)

- bool StartDoc(string documentName)

- bool StartPage()

- int Write(byte[]data)

- bool EndPage()

- bool EndDoc()

- void Abort()

- bool Close()

- static string Quote(string s)


## PrinterSettings (class)

只读访问当前用户已安装的打印机。Windows 上走
winspool（EnumPrintersW/GetDefaultPrinterW），Linux/macOS 上走 CUPS
的 lpstat。

- static List<string> InstalledPrinters()
  - 枚举本地打印机及打印机连接。

- static string DefaultPrinterName()
  - 返回默认打印机名称；未配置时返回空字符串。

- [DllImport("winspool", EntryPoint="EnumPrintersW")]static extern int EnumPrintersW(int flags, nint name, int level, nint buffer, int bufferBytes, nint neededBytes, nint returnedCount);

- [DllImport("winspool", EntryPoint="GetDefaultPrinterW")]static extern int GetDefaultPrinterW(nint buffer, nint chars);

- static List<string> WinInstalledPrinters()


## RawPrinter (class)

将已格式化的字节流直接发送到打印队列（Windows 走
winspool 的 RAW 数据类型，Linux/macOS 走 CUPS 的 `lp -o raw`）。

- static bool Send(string printerName, string documentName, byte[]data)
  - 发送一个 RAW 文档。字节原样传递给所选
    打印机驱动。任何后台打印操作失败时返回 false。

- internal static bool SendWithBackend(string printerName, string documentName, byte[]data, IRawPrinterBackend backend)
  - 针对注入的后端运行 RAW 后台打印协议。


## WinRawPrinterBackend (class)

- nint handle;

- bool Open(string printerName)

- bool StartDoc(string documentName)

- bool StartPage()

- int Write(byte[]data)

- bool EndPage()

- bool EndDoc()

- void Abort()

- bool Close()

- [DllImport("winspool", EntryPoint="OpenPrinterW")]static extern int OpenPrinterW(nint printerName, nint handle, nint defaults);

- [DllImport("winspool", EntryPoint="StartDocPrinterW")]static extern int StartDocPrinterW(nint printer, int level, nint docInfo);

- [DllImport("winspool", EntryPoint="StartPagePrinter")]static extern int StartPagePrinter(nint printer);

- [DllImport("winspool", EntryPoint="WritePrinter")]static extern int WritePrinter(nint printer, byte[]data, int count, nint written);

- [DllImport("winspool", EntryPoint="EndPagePrinter")]static extern int EndPagePrinter(nint printer);

- [DllImport("winspool", EntryPoint="EndDocPrinter")]static extern int EndDocPrinter(nint printer);

- [DllImport("winspool", EntryPoint="AbortPrinter")]static extern int AbortPrinter(nint printer);

- [DllImport("winspool", EntryPoint="ClosePrinter")]static extern int ClosePrinter(nint printer);


## IRawPrinterBackend (interface)

RAW 打印状态机使用的后台打印程序操作。

- bool Open(string printerName);

- bool StartDoc(string documentName);

- bool StartPage();

- int Write(byte[]data);

- bool EndPage();

- bool EndDoc();

- void Abort();

- bool Close();
