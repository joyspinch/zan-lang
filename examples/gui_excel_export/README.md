# DataTable 导出（Excel / CSV）

`Gui.Component.DataTable` 的文件导出能力演示：表格数据一键落盘为
**.xlsx**（真 Excel 工作簿）或 **.csv**，保存路径走 `FilePicker`
保存对话框。导出在**后台线程流式写盘**：底部进度条实时显示、
随时可取消；内存占用与行数无关——百万行 × 70 列也不会涨内存
（超过 1,048,576 行自动分表）。

```bash
build/zanc examples/gui_excel_export/gui_excel_export.zan --auto-stdlib -o build/gui_excel_export.exe
build/gui_excel_export.exe
```

## 演示内容

- **导出 Excel (.xlsx)**：整个（过滤/排序后）视图。按列声明写出
  类型化真值——`Numeric`/`Money` 列写数字（Excel 里可直接求和）、
  `Date` 列写 Excel 日期（yyyy-mm-dd 显示格式）、`Bool` 列写布尔；
  首行加粗、冻结首行、列宽按内容自适应。
- **导出 CSV (.csv)**：整个视图，内容与 Ctrl+C 复制一致（显示文本，
  RFC 4180 转义）；UTF-8 带 BOM，Excel 双击打开中文不乱码。
- **导出选中为 Excel**：`selectionOnly = true`——有单元格范围导
  范围（行×列），否则导勾选行；两者皆无时返回 `false`（状态栏提示）。
- **异步 + 进度 + 取消**：走 `ExportToXlsxAsync`/`ExportToCsvAsync`，
  传一个 `DataTableExportProgress` 子类即可；回调在后台线程上发生，
  示例用 `App.Post` 切回 UI 线程刷进度条（这是唯一的跨线程规则）。
  行集在启动那刻冻结成快照，之后表格再排序/过滤不影响本次导出。

## 分层

| 层 | 位置 | 职责 |
| --- | --- | --- |
| 数据层 | `System.Data.Excel`（`XlsxBook`/`XlsxRowSource`/`XlsxCell`） | 纯 Zan 的 XLSX 写出器：inlineStr 字符串、多工作表、流式 `ZipWriter` 打包（分块 deflate，边生成边落盘），无 GUI 依赖。服务端/控制台生成报表直接用它：整表在内存用 `SaveToBytes()`，大表用 `SaveStreaming(path, sources)` 逐行供给。 |
| GUI 层 | `Gui.Component.DataTable`（`DataTable.Export.zan`） | `ExportToXlsx/ExportToCsv`（同步，流式落盘）与 `ExportToXlsxAsync/ExportToCsvAsync`（后台线程 + 进度/取消）：行选择语义与剪贴板复制一致，XLSX 按列类型映射单元格。 |

## 相关

- `tests/conformance/xlsx_write.zan` — 内存路径的结构断言与字节可复现性。
- `tests/conformance/xlsx_stream.zan` — 流式写出：分块 deflate、分表、取消。
- `docs/STDLIB.md` — stdlib 目录总览。
