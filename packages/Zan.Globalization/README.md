# Zan.Globalization

键式多语言查找与农历历法（纯 Zan，无原生依赖）。
`using System.Globalization;` 按需拉入，命名空间保留零破坏。

## 内容（stdlib/System/Globalization 整目录迁入）

| 文件 | 内容 |
|---|---|
| `Lang.zan` | Lang：键式多语言查找（Apple .strings 同构，源码保留英文原文） |
| `Lunar.zan` | Lunar：公历→农历、干支（年/月/日）、生肖 |
| `DateParser.zan` | DateParser：宽松日期解析（`2024-01-02`、`2024/1/2`、`Jan 2, 2024`、`2024年1月2日`、时区、相对词）+ 模式串格式化与精确解析 |

## 消费者

- `Zan.Gui`（Designer/FilePicker 的文案）、`Zan.Gui.CodeEditor`、
  `Zan.Gui.DataTable`（DataTable.Lang）、`Zan.Mvc`（Framework/Lang）
- `examples/gui_gallery`、conformance `lunar.zan`、`lang.zan`
  （从仓库根编译即可，包发现自动拉入）
