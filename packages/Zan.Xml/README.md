# Zan.Xml

XML DOM for Zan：递归下降解析器 + 树导航 + 紧凑/缩进序列化，纯 Zan 实现，
无原生依赖。命名空间 `System.Xml`，对标 .NET 的 `System.Xml` 但只保留
DOM 与读写——XPath / XSLT / Schema 校验 / DTD 校验不做。

## 快速上手

```zan
using System.Xml;

// 解析
XmlDocument doc = XmlDocument.Parse(xmlText);     // 非法抛 XmlException（带行列号）
XmlDocument doc2 = XmlDocument.Load("conf.xml"); // 读文件

// 导航（全部返回节点或 null，判空后使用）
XmlNode root = doc.RootElement();
XmlNode item = root.Element("item");             // 第一个子元素
List<XmlNode> items = root.Elements("item");     // 全部同名子元素
string text = item.InnerText();                  // 递归拼接文本/CDATA
string v = item.Attr("id");                      // 属性，无则 null

// 构建
XmlNode el = XmlNode.NewElement("file");
el.SetAttr("name", "a");
el.AddText("hello");
root.AddChild(el);

// 序列化
string compact = doc.ToXml();        // 紧凑，保留注释/CDATA/PI
string pretty = doc.ToPrettyXml();   // 2 空格缩进
doc.Save("out.xml");                 // 紧凑形式写文件
```

## 设计要点

- **命名空间不解析**：没有 xmlns 作用域表，名字按原文保留（含前缀）。
  用不带前缀的名字查找时回退"忽略前缀只比本地名"——
  `Element("href")` 能命中 `<D:href>`。WebDAV、WeChat 这类前缀随
  服务器变化的报文靠这一条规则即可；带前缀的查询只做精确匹配。
- **往返无损（语义级）**：Parse 与 ToXml/ToPrettyXml 互为逆操作，写出的
  文本再次 Parse 得到语义相同的树。不保证字节级还原：实体统一成最短形
  （`&#65;` → `A`）、自闭合空元素统一成 `<a/>`、`\r` 归一成 `\n`。
- **保留节点种类**：声明、注释、PI、DOCTYPE（原文截取，含内部子集）、
  CDATA（内容含 `]]>` 时写侧按 `]]]]><![CDATA[>` 拆分）全部保留，
  序列化时原样回吐。
- **转义封闭**：解析支持五种命名实体 + `&#DDD;`/`&#xHHH;` 数字字符引用
  （含中文，经 `Encoding.Utf8FromCodePoint` 落成 UTF-8）；写侧转义
  `& < >` 与 `\r`（文本）、另加 `" \n \t`（属性值）。未知实体报错。
- **深度上限 512**：与 `System.Json` 对称——读侧递归解析、写侧递归
  序列化，两侧一起限，防栈溢出。
- **错误即抛**：`XmlException` 带行列号（`... at line 3, column 10`）。
  没有 lenient 模式；需要容错的调用方自行 try/catch。

## 已知取舍

- DOCTYPE 不做 DTD 校验，实体表（`<!ENTITY>`）不展开。
- 非_ASCII 字节整体视作名字字符，中文元素/属性名可用；XML 规范里
  更宽的 Unicode 名字集未逐一校验。
- 注释正文里的 `--`、CDATA 之外的 `]]>` 等非法形态不校验，原样保留。

## 消费方

- `Zan.Sdk.Wechat` 的 `XmlUtil`：微信报文目前用标签扫描（结构固定），
  其注释明确"需要完整 DOM 时换 System 层解析器"——即本包。
- `Zan.Net` 的 `WebDavClient`（multistatus 解析）、`Zan.Data` 的
  Xlsx 读取（若做）是下一个受益方。
