# System.Windows.Clipboard

> 源码: `packages/Zan.Desktop/src/System/Windows/Clipboard/Clipboard.zan`


## ClipFormat (class)

- static int Text()

- static int Bitmap()

- static int UnicodeText()

- static int Hdrop()

- static int Html()

- static int Rtf()


## Clipboard (class)

- [DllImport("user32", EntryPoint="OpenClipboard")]static extern int WinOpenClipboard(nint hwnd);

- [DllImport("user32", EntryPoint="CloseClipboard")]static extern int WinCloseClipboard();

- [DllImport("user32", EntryPoint="EmptyClipboard")]static extern int WinEmptyClipboard();

- [DllImport("user32", EntryPoint="SetClipboardData")]static extern nint WinSetClipboardData(int format, nint mem);

- [DllImport("user32", EntryPoint="GetClipboardData")]static extern nint WinGetClipboardData(int format);

- [DllImport("user32", EntryPoint="IsClipboardFormatAvailable")]static extern int WinIsClipboardFormatAvailable(int format);

- [DllImport("user32", EntryPoint="CountClipboardFormats")]static extern int WinCountClipboardFormats();

- [DllImport("user32", EntryPoint="EnumClipboardFormats")]static extern int WinEnumClipboardFormats(int last);

- [DllImport("user32", EntryPoint="RegisterClipboardFormatW")]static extern int WinRegisterClipboardFormat(nint name);

- [DllImport("user32", EntryPoint="GetClipboardFormatNameW")]static extern int WinGetClipboardFormatName(int format, nint buf, int size);

- [DllImport("kernel32", EntryPoint="GlobalAlloc")]static extern nint WinGlobalAlloc(int flags, int bytes);

- [DllImport("kernel32", EntryPoint="GlobalLock")]static extern nint WinGlobalLock(nint mem);

- [DllImport("kernel32", EntryPoint="GlobalUnlock")]static extern int WinGlobalUnlock(nint mem);

- [DllImport("kernel32", EntryPoint="GlobalFree")]static extern nint WinGlobalFree(nint mem);

- [DllImport("shell32", EntryPoint="DragQueryFileW")]static extern int WinDragQueryFileW(nint hdrop, int index, nint buf, int size);

- [DllImport("shell32", EntryPoint="DragFinish")]static extern void WinDragFinish(nint hdrop);

- static int ToolNone()

- static int ToolWayland()

- static int ToolXclip()

- static int ToolXsel()

- static int ToolPasteboard()

- static string MimeText()

- static string MimeUriList()

- static bool HaveCommand(string name)

- static int PosixTool()

- static void NoTool()

- static string TempFile(string tag)

- static List<string> MacFiles()

- static bool MacSetFiles(List<string> paths)

- static string PosixRead(int tool, string mime)

- static bool PosixWrite(int tool, string mime, string content)

- static bool HasText()

- static bool HasFiles()

- static string GetText()

- static bool SetText(string text)

- static void Clear()

- static bool SetFiles(List<string> paths)

- static List<string> GetFiles()

- static List<int> Formats()

- static int RegisterFormat(string name)

- static string FormatName(int format)

- static List<string> MimeTypes()

- static string FileUri(string path)

- static string Slashes(string path)

- static string PathFromUri(string uri)
