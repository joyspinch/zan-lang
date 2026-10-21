# Zan.IO

扩展 I/O：路径、文本流、压缩族与文件监视（纯 Zan，无原生依赖）。
`using System.IO;` / `using System.IO.Compression;` / `using System.IO.Watch;`
按需拉入，命名空间保留零破坏。

## 内容（System/IO 拆半：核心 8 文件留守 stdlib，其余迁入）

| 文件 | 内容 |
|---|---|
| `Path.zan` | 路径组合/解析与扩展（判扩展名、规范化、Glob匹配） |
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
| `Compression/Zstd/*.zan` | zstd（Zstandard）解码器（RFC 8878，纯解码） |

## zstd（Zstandard）解码

`System.IO.Compression.Zstd` 是 **zstd 的解码器**（RFC 8878），纯 Zan 实现，
与 libzstd 产出的帧互通（conformance `zstd_decode` 用 `zstandard` 0.25.0 的
真实帧对拍）。压缩侧不在本包，请用外部 zstd 工具——压缩器的取舍空间太大，
"半截实现"最容易在互通时暴露成静默的错误结果。

```zan
using System.IO.Compression;

byte[] plain = Zstd.Decompress(compressed);          // 失败抛 ZstdException

byte[] data;
ZstdError err;
if (!Zstd.TryDecompress(compressed, out data, out err)) {
    Console.WriteLine("zstd 解压失败: " + err.ToString());
}

Zstd.IsZstd(bytes);                                   // 只看魔数
Zstd.DecompressFile("in.zst");                        // 文件 -> 字节
Zstd.DecompressFileTo("in.zst", "out.bin");           // 文件 -> 文件
```

**覆盖范围**：帧头（单段帧 / 窗口描述符帧 / 无内容长度 / 带内容校验和）、
原始 / RLE / 压缩三种块、原始 / RLE / 压缩 / treeless 四种字面量段、
Huffman 字面量（1 流与 4 流）、FSE 与 Huffman 熵编码、序列的重复偏移、
序列表的 predefined / RLE / FSE / repeat 四种模式、多帧拼接、可跳过帧、
XXH64 内容校验和。

**错误可区分**：内部一律抛 `ZstdException`（带 `ZstdError` 错误码枚举：
`NotZstd` / `Truncated` / `BadFrameHeader` / `DictionaryRequired` /
`BadBlock` / `BadLiterals` / `BadHuffman` / `BadFse` / `BadSequences` /
`BadOffset` / `ChecksumMismatch` / `SizeMismatch` / `Internal`），
`TryDecompress` 给不抛异常的那条路。

**已知边界**：

- **不支持字典**：帧头声明了非零 Dictionary ID 时抛 `DictionaryRequired`
  （明确报错，不静默按无字典解）。
- **只解码不编码**：本包不提供 zstd 压缩。
- 空输入返回空结果（没有帧就没有输出），不算错误。

**线程安全**：`Zstd` 的方法全是无状态静态方法，可并发调用；单次解码的
中间状态都在本次调用的局部对象里。

## 跨平台路径与用户目录

`Path.GetFullPath(path)` 按当前工作目录解析完整路径，目标可以尚不存在；
`Path.IsPathRooted(path)` 判断是否有根。Windows 使用 Unicode 系统接口，
POSIX 按 `/` 处理路径并保留文件名中的反斜杠。`Path.Combine` 支持空路径和
有根的第二参数，`GetTempPath()` 返回带末尾分隔符的临时目录。

`KnownFolders` 保留统一入口：Linux 应用目录遵循绝对路径形式的 XDG 环境变量，
桌面、文档和下载目录读取 `user-dirs.dirs`；macOS 的应用数据目录为
`~/Library/Application Support`。Windows 应用数据目录读取环境变量，用户目录
仍使用主目录下的默认位置，尚不读取系统已知文件夹重定向。

## 留守 stdlib（生成器子编译闭包，`--no-packages` 可达）

`File` / `Directory` / `FileInfo` / `FileInfoEx` / `FileStream` / `Stream` /
`SeekOrigin` / `ByteBuffer`——已核验留守 8 文件对迁出类型**零真实依赖**
（Stream 的 MemoryStream/StreamReader 字样全是 doc 注释，ByteBuffer.Crc32 是
自有方法调 NativeMemory.Crc32，FileStream.Path() 是自有属性）。

## 消费者

- ZanIDE（MarketplaceInstall/Package 的 Tar、编辑器 IO）、`Zan.Data`（Xlsx 的 Zip）、
  `Zan.Gui`（QrCode）、`Zan.Game`
- conformance：io/compress 域用例从仓库根编译即可（包发现自动拉入）
