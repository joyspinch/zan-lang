# Zan.IO

扩展 I/O：路径、文本流、压缩族与文件监视（纯 Zan，无原生依赖）。
`using System.IO;` / `using System.IO.Compression;` / `using System.IO.Watch;`
按需拉入，命名空间保留零破坏。

## 内容（System/IO 拆半：核心 8 文件留守 stdlib，其余迁入）

| 文件 | 内容 |
|---|---|
| `Path.zan` / `PathEx.zan` | 路径组合/解析与扩展（判扩展名、规范化） |
| `MemoryStream.zan` | 内存流（Stream 公共基类的内存实现） |
| `StreamReader.zan` / `StreamWriter.zan` | 文本读写流 |
| `MemoryMappedFile.zan` | 内存映射文件 |
| `FileAccess.zan` / `FileMode.zan` | 打开语义枚举 |
| `IniFile.zan` | INI 解析/写回（IniFile/IniSection/IniEntry） |
| `KnownFolders.zan` | 系统 known folders（桌面/下载/AppData…） |
| `Shortcut.zan` | Windows .lnk 解析（Shortcut/ShortcutInfo） |
| `DirectoryTree.zan` | 目录树枚举与快照（FileSnapshot/PathFilter） |
| `Watch/DirectoryWatcher.zan` | 文件变更监视（DirectoryWatcher/FileChange） |
| `Compression/Crc32.zan` | CRC-32 |
| `Compression/Deflate.zan` | Deflate（DeflateChunker） |
| `Compression/GZip.zan` | GZip |
| `Compression/Zip.zan` | Zip 读写（Zip/ZipEntry/ZipWriter） |
| `Compression/BZip2.zan` | BZip2（BZip2Decoder） |
| `Compression/Tar.zan` | Tar 打包/解包（保留 unix mode 位） |

## 留守 stdlib（生成器子编译闭包，`--no-packages` 可达）

`File` / `Directory` / `FileInfo` / `FileInfoEx` / `FileStream` / `Stream` /
`SeekOrigin` / `ByteBuffer`——已核验留守 8 文件对迁出类型**零真实依赖**
（Stream 的 MemoryStream/StreamReader 字样全是 doc 注释，ByteBuffer.Crc32 是
自有方法调 NativeMemory.Crc32，FileStream.Path() 是自有属性）。

## 消费者

- ZanIDE（MarketplaceInstall/Package 的 Tar、编辑器 IO）、`Zan.Data`（Xlsx 的 Zip）、
  `Zan.Gui`（QrCode）、`Zan.Game`
- conformance：io/compress 域用例从仓库根编译即可（包发现自动拉入）
