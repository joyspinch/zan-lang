# Zan.Csv

RFC 4180 CSV 的纯 Zan 实现（原先分散在 `Zan.Text` 的
`System.Text.Csv` 与 `Zan.DataFrame` 的 `System.Data.Csv` 已合并至此，
两包改为消费本包）。命名空间 `System.Csv`，零外部依赖。

## 特性

- **引号字段**：以 `"` 起始的字段可包含分隔符与换行，`"` 转义为两个
  `""`；字段中途出现的引号按字面字符收下（宽松解析）。
- **跨平台记录分隔符**：CRLF、LF、单独 CR 都能解析；末尾换行不产生
  空记录；无换行收尾的最后一条记录正常结束。
- **UTF-8 BOM 剥除**：文件头 `EF BB BF`（Windows 记事本产物）自动剥离。
- **自定义分隔符**：`ParseSep`/`SerializeSep`/`RowToTextSep`/`EscapeSep`
  支持 TSV 等单字符分隔符。
- **值模型极简**：一切都是 `string`（`List<string>` 行、`List<List<string>>`
  表），类型推断是上层（如 Zan.DataFrame）的事。

## API 一览

| 入口 | 用途 |
|------|------|
| `Csv.Parse(text)` | 解析为 `CsvTable`（首记录约定为表头） |
| `Csv.ParseSep(text, sep)` | 自定义分隔符解析（TSV：`"\t"`） |
| `t.Header()` / `t.Rows()` / `t.RowCount()` | 表头 / 数据行 / 行数 |
| `t.Column(name)` | 按表头名取整列（不存在返回 null） |
| `Csv.ParseRow(line)` | 解析单行记录 |
| `Csv.Serialize(rows)` / `SerializeSep(rows, sep)` | 行列表 → CSV 文本（`\n` 结尾） |
| `Csv.RowToText(fields)` / `RowToTextSep(fields, sep)` | 单行 → 文本（不含换行） |
| `Csv.Escape(field)` / `EscapeSep(field, sep)` | 单字段按需加引号 |

## 示例

```zan
using System;
using System.Csv;

class Demo {
    static void Main() {
        CsvTable t = Csv.Parse("name,tags\r\nada,\"x,y\"\r\n");
        Console.WriteLine(t.Header().At(0));          // name
        Console.WriteLine(t.RowCount());              // 1
        Console.WriteLine(t.Column("tags").At(0));    // x,y

        List<List<string>> rows = new List<List<string>>();
        List<string> r = new List<string>();
        r.Add("a"); r.Add("say \"hi\"");
        rows.Add(r);
        string text = Csv.Serialize(rows);            // a,"say ""hi"""\n
    }
}
```

更多可运行的例子见 `examples/formats/csv_basics.zan`。
