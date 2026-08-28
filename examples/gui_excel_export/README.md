# DataTable 导出（Excel / CSV）

`Gui.Component.DataTable` 的文件导出能力演示：表格数据一键落盘为
**.xlsx**（真 Excel 工作簿）或 **.csv**，保存路径走 `FilePicker`
保存对话框。

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

## 分层

| 层 | 位置 | 职责 |
| --- | --- | --- |
| 数据层 | `System.Data.Excel`（`XlsxBook`/`XlsxSheet`/`XlsxCell`） | 纯 Zan 的 XLSX 写出器：inlineStr 字符串、多工作表、`Zip` 打包，无 GUI 依赖。服务端/控制台生成报表直接用它（`SaveToBytes()` 可直接作 HTTP 下载响应）。 |
| GUI 层 | `Gui.Component.DataTable`（`DataTable.Export.zan`） | `DataTable.ExportToXlsx(st, cols, src, path, selectionOnly)` / `ExportToCsv(...)`：行选择语义与剪贴板复制一致，XLSX 按列类型映射单元格。 |

## 相关

- `tests/conformance/xlsx_write.zan` — 写出器的结构断言与字节可复现性。
- `docs/STDLIB.md` — stdlib 目录总览。
