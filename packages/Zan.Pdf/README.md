# System.Pdf

`System.Pdf` 是 **PDF 生成器**：页面 + 画线 / 矩形 / 文本 + 表格 + PDF 1.4 写出器。

它补的是 QuestPDF（14,205★）那一档的**生成**侧：把数据排成页、写出去。
纯 Zan 实现，无原生依赖，不嵌字体。

## 设计取舍

- **只用标准 14 字体，不嵌字体**：Helvetica / Times / Courier × 常规/粗体是
  PDF 阅读器自带的，**不用带字体文件**——文档小、没有授权问题、任何阅读器
  都能开。代价是只有这几种西文字体，且宽度用内置的固定值（对标准 14 字体
  而言就是真实度量）。要中文必须嵌 TrueType 子集（另一件事，见已知边界）。
- **坐标用左上角原点、y 向下**：排版引擎的惯用坐标（和 HTML 画布一样）。
  翻成 PDF 的左下角原点只在一处做（写内容流时 `H - y`），其余地方都按
  页面坐标想。
- **文字的 y 是行顶，不是基线**：按行顶对齐更符合排版直觉。
- **不自动分配列宽**：列宽显式给（`AddColumn("名称", 200)`）——自动分配要么
  按内容比例（长文本的列被挤成一条），要么等分（表格布局引擎要的是"我说
  算"）。
- **编不出来的字符当场报错**：标准 14 字体没有 CJK 字形。遇到编不出来的
  字符**当场抛异常**，不写出一份显示成方框、或干脆打不开的文档。
- **数字不用 `Convert.ToString`**：不同 locale 的小数点与千分位不同，写进
  PDF 会让阅读器解析出错误的坐标。包内自写定点格式化（最多三位小数）。
- **只生成不解析**：本包不是 PDF 阅读器。要读 PDF 里的文本与表格是另一件事
  （需要内容流解析与字体映射）。
- **非线程安全**：文档是逐页构建的状态。

## 能力

| 能力 | 入口 |
|---|---|
| 文档 | `PdfDocument`：`AddPage` / `PageCount` / `Page` / `Build` / `Save` + 标题 / 作者 / 主题 / 关键词元数据 |
| 页面 | `PdfPage`：`DrawLine` / `DrawRect` / `DrawRectOutline` / `DrawRectBoth` / `DrawText` / `DrawTextInBox` / `MeasureText` / `MeasureTextHeight` |
| 页面尺寸 | `PdfPageSize`：`A4` / `Letter` / `Legal` / `Custom` |
| 字体 | `PdfFont`：`Helvetica` / `HelveticaBold` / `Times` / `TimesBold` / `Courier` + `Measure` / `AscentAt` / `LineHeight` |
| 颜色 | `PdfColor`：`Black` / `White` / `Gray` / `FromRgb` / `FromHex` / `Mix` |
| 表格 | `PdfTable`：`AddColumn` / `AddRow` / `WithPadding` / `WithFontSize` / `WithFont` / `WithBorder` / `MeasureHeight` / `Draw` |
| 对齐 | `PdfTextAlign`：`Left` / `Center` / `Right` |
| 错误 | `PdfException` + `PdfError`（`InvalidArgument` / `UnencodableCharacter` / `InvalidState`） |

## 用法

```zan
using System;
using System.Collections.Generic;
using System.Pdf;

PdfDocument doc = new PdfDocument();
doc.Title = "Quarterly Report";
doc.Author = "Zan";

PdfPage page = doc.AddPage();                 // A4
page.DrawText(50, 60, "Quarterly Report", PdfFont.HelveticaBold(), 18, PdfColor.Black);
page.DrawLine(50, 120, 400, 120, PdfColor.FromRgb(200, 0, 0), 1.5);
page.DrawRect(50, 140, 160, 60, PdfColor.FromRgb(235, 235, 245));

PdfTable table = new PdfTable();
table.AddColumn("Item", 180).AddColumn("Qty", 60, PdfTextAlign.Right);
table.AddRow(new List<string> { "Widget", "3" });
table.Draw(page, 50, 230);                    // 返回占用的高度

doc.Save("report.pdf");
```

对齐与测量：

```zan
page.DrawTextInBox(50, 50, 400, "centered", PdfTextAlign.Center, PdfFont.Times(), 14, PdfColor.Black);
double w = page.MeasureText("Hello", PdfFont.Helvetica(), 12);   // 27.336 点
```

## 验证

`tests/conformance/pdf_document.zan` 覆盖：文件头 / 文件尾、startxref 指向的
确实是 xref 表、**每条 xref 记录指向的确实是 `<对象号> 0 obj`**、对象号与
页树一致；Catalog / Pages / Page / Font 对象、MediaBox、文本操作符序列、
颜色操作符、坐标翻转；括号与反斜杠的 PDF 字面量转义；标准 14 字体的宽度 /
上伸 / 行高；表格的列宽累加、总高、绘制高度与测量一致、单元格数校验；
错误路径（无页文档、编不出来的字符、非法页尺寸、越界取页）。

外部验证：生成的样例文档用 **pypdf** 与 **pymupdf** 两个独立实现解析通过
（页数、元数据、文本抽取、表格单元格、右对齐与居中的坐标全部正确），并渲染
出位图人工复核过版面。

## 已知边界

- **没有中文 / CJK**：标准 14 字体没有 CJK 字形，编不出来的字符当场报错。
  要中文得嵌 TrueType 子集 + 编码表（另一件事）。
- **不解析 PDF**：只生成，不读。
- **没有图片**：不写 XObject 图像。
- **没有压缩流**：内容流不压缩（用 `System.IO.Compression.Deflate` 压缩是
  后续可加的）。
- **没有加密 / 签名 / 附件 / 书签 / 元数据 XMP**。
- **表格没有合并单元格、没有跨页表头重复**：跨页要调用方自己分页。
- **不支持 Type0 复合字体与自定义编码**：只 WinAnsiEncoding。

## 放置

包约定：`packages/Zan.Pdf/src/System/Pdf/*.zan`。
文档类能力（PDF / Office / 图片）走独立包并单独版本化，不进 stdlib。
