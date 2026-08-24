# System.Windows.Clipboard

> 源码: `stdlib/System/Windows/Clipboard/Clipboard.zan`


## ClipFormat (class)

剪贴板格式常量（winuser.h）。

- static int Text()

- static int Bitmap()

- static int UnicodeText()

- static int Hdrop()

- static int Html()

- static int Rtf()


## Clipboard (class)

剪贴板访问：Unicode 文本、文件列表（CF_HDROP / text/uri-list）、
格式探测与枚举。

if (Clipboard.HasText()) {
string t = Clipboard.GetText();
}
Clipboard.SetText("hello");
Clipboard.SetFiles(paths);          // Explorer 会将其视为真实文件拖放
List<string> files = Clipboard.GetFiles();

Windows 走 user32 剪贴板 API。Linux 上 X11 的剪贴板是“谁拥有
选区谁就要活着响应请求”的协议，进程退出内容就没了，因此
这里委托给平台自己的剪贴板工具（Wayland 用 wl-copy/wl-paste，
X11 用 xclip 或 xsel，macOS 用 pbcopy/pbpaste）；它们会自己 fork 一个
持有进程。内容走临时文件而不进命令行，因此任意文本（包括
引号、换行、shell 元字符）都不会被解释。工具不存在时抛出
PlatformNotSupportedException，而不是假装成功。

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
  - 当前平台可用的剪贴板工具；一个都没有时返回 ToolNone。
    Wayland 会话优先 wl-clipboard：X11 工具在纯 Wayland 下无法访问
    合成器的剪贴板。

- static void NoTool()

- static string TempFile(string tag)
  - 临时文件路径：剪贴板内容走文件而不进命令行。

- static string PosixRead(int tool, string mime)
  - 读取 `mime` 目标的剪贴板内容；没有该目标时返回 ""。

- static bool PosixWrite(int tool, string mime, string content)
  - 把 `content` 作为 `mime` 目标放上剪贴板。

- static bool HasText()
  - 剪贴板上有 Unicode 文本时为 true。

- static bool HasFiles()
  - 剪贴板上有文件列表（CF_HDROP）时为 true。

- static string GetText()
  - 读取 Unicode 文本；不可用时返回 ""。

- static bool SetText(string text)
  - 将 Unicode 文本放入剪贴板。成功返回 true。

- static void Clear()
  - 清空剪贴板。

- static bool SetFiles(List<string> paths)
  - 将文件列表（CF_HDROP）放入剪贴板。成功
    返回 true。

- static List<string> GetFiles()
  - 读取文件列表（CF_HDROP）；不可用时返回空。

- static List<int> Formats()
  - 当前剪贴板上所有格式的数值 id
    （包括已注册的自定义格式），按枚举顺序排列。

- static int RegisterFormat(string name)
  - 注册（或查找）自定义格式名。返回的
    id 在进程生命周期内保持稳定。

- static string FormatName(int format)
  - （已注册）格式 id 的名称，内置格式返回 ""。

- static List<string> MimeTypes()
  - 剪贴板上现有的 MIME 类型（POSIX 上格式的真名字）。
    Windows 上把已知的内置格式映射成对应的 MIME 名。

- static string FileUri(string path)
  - 本地路径 -> file:// URI（uri-list 需要百分号转义）。

- static string Slashes(string path)
  - 反斜杠换正斜杠（URI 里只有 '/'）。

- static string PathFromUri(string uri)
  - file:// URI -> 本地路径；不是 file URI 时返回 ""。
