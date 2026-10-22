# Zan.Text

文本处理（纯 Zan，无原生依赖）。`using System.Text;` /
`using System.Text.RegularExpressions;` / `using System.Text.Html;` 按需拉入，
命名空间保留零破坏。

## 内容（stdlib/System/Text 迁入，Encoding 留 stdlib）

| 文件 | 内容 |
|---|---|
| `Markdown.zan` | Markdown → HTML 渲染 |
| `Pinyin.zan` | 汉字转拼音（`data/pinyin.txt` 数据表） |
| `Bm25Index.zan` | BM25 全文检索 |
| `Fuzzy.zan` | 模糊匹配 |
| `Template.zan` | 文本模板（占位符展开） |
| `TextTable.zan` | 等宽文本表格 |
| `RegularExpressions/` | 正则引擎（Regex/Match/RegexProgram，`System.Text.RegularExpressions`） |
| `Html/` | HTML 容错解析 + CSS 选择器查询 + HTML 消毒器（`System.Text.Html`） |
| `CodePage/Gbk.zan` | CP936（GBK）编解码（`System.Text.CodePage`），映射表在 `CodePage/GbkTable.zan` |

## 不在包内

- `Encoding.zan` 留 stdlib `System/Text/`——生成器子编译闭包需要
  （GenIndex/ZanGen/JsonValue 消费），且 UTF-8/Base64 是基础能力。

## 数据文件

- `src/System/Text/data/pinyin.txt`：Pinyin 运行时数据。查找顺序：env
  `ZAN_PINYIN_DATA` → exe 同目录 → zan_embed（`text/pinyin.txt`，zanc 对携带
  Pinyin 的程序自动嵌入，与 Gui skins 同机制）→ 源码检出向上六层找
  `packages/Zan.Text/src/System/Text/data/pinyin.txt`。

## 编码表

- `src/System/Text/CodePage/GbkTable.zan`：CP936 的双字节映射表（24130 个
  槽位，每个一个码点，未映射的用 U+FFFD 占位）。它是**数据**：编解码算法在
  `Gbk.zan` 里原生实现，不依赖平台的代码页 API，Windows 与 POSIX 行为一致。
  映射取自 Unicode 官方 CP936 表，与 Python 的 `'gbk'` 编码器逐槽位一致
  （21791 个有效槽位，双向一一对应）。
