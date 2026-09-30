# Zan.Globalization

键式多语言查找与农历历法（纯 Zan，无原生依赖）。
`using System.Globalization;` 按需拉入，命名空间保留零破坏。

## 内容（stdlib/System/Globalization 整目录迁入）

| 文件 | 内容 |
|---|---|
| `Lang.zan` | Lang：键式多语言查找（Apple .strings 同构，源码保留英文原文） |
| `Lunar.zan` | Lunar：公历→农历、干支（年/月/日）、生肖 |

## 消费者

- `Zan.Gui`（Designer/FilePicker 的文案）、`Zan.Gui.CodeEditor`、
  `Zan.Gui.DataTable`（DataTable.Lang）、`Zan.Mvc`（Framework/Lang）
- `examples/gui_gallery`、conformance `lunar.zan`、`lang.zan`
  （从仓库根编译即可，包发现自动拉入）
