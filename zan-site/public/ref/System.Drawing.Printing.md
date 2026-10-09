# System.Drawing.Printing

> 源码: `packages/Zan.Desktop/src/System/Drawing/Printing/Printing.zan`


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

- static List<string> InstalledPrinters()

- static string DefaultPrinterName()

- [DllImport("winspool", EntryPoint="EnumPrintersW")]static extern int EnumPrintersW(int flags, nint name, int level, nint buffer, int bufferBytes, nint neededBytes, nint returnedCount);

- [DllImport("winspool", EntryPoint="GetDefaultPrinterW")]static extern int GetDefaultPrinterW(nint buffer, nint chars);

- static List<string> WinInstalledPrinters()


## RawPrinter (class)

- static bool Send(string printerName, string documentName, byte[]data)

- internal static bool SendWithBackend(string printerName, string documentName, byte[]data, IRawPrinterBackend backend)


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

- ~WinRawPrinterBackend()

- [DllImport("winspool", EntryPoint="OpenPrinterW")]static extern int OpenPrinterW(nint printerName, nint handle, nint defaults);

- [DllImport("winspool", EntryPoint="StartDocPrinterW")]static extern int StartDocPrinterW(nint printer, int level, nint docInfo);

- [DllImport("winspool", EntryPoint="StartPagePrinter")]static extern int StartPagePrinter(nint printer);

- [DllImport("winspool", EntryPoint="WritePrinter")]static extern int WritePrinter(nint printer, byte[]data, int count, nint written);

- [DllImport("winspool", EntryPoint="EndPagePrinter")]static extern int EndPagePrinter(nint printer);

- [DllImport("winspool", EntryPoint="EndDocPrinter")]static extern int EndDocPrinter(nint printer);

- [DllImport("winspool", EntryPoint="AbortPrinter")]static extern int AbortPrinter(nint printer);

- [DllImport("winspool", EntryPoint="ClosePrinter")]static extern int ClosePrinter(nint printer);


## IRawPrinterBackend (interface)

- bool Open(string printerName);

- bool StartDoc(string documentName);

- bool StartPage();

- int Write(byte[]data);

- bool EndPage();

- bool EndDoc();

- void Abort();

- bool Close();
