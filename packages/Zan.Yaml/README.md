# Zan.Yaml

YAML core 子集 for Zan：解析器 + 写出器，纯 Zan 实现，无原生依赖。
命名空间 `System.Yaml`，值树直接用 `System.Json.JsonValue`——解析结果
可以立刻喂给任何已用 JSON 的代码，序列化天然保持键序。

## 支持范围（core schema / block + flow）

```yaml
# 块映射/嵌套/注释
server:
  host: local      # 行内注释
  port: 8080
# 块序列 + "- k: v" 压缩嵌套
items:
  - name: a
    qty: 1
  - x
# 流式集合（可跨行）
pair: [1, 2]
opts: {debug: true, level: 3}
# 四种标量
plain: text
single: 'it''s'
double: "line1\nline2\u4e2d"
literal: |
  原样保留
  换行
folded: >
  单换行
  折成空格
# 锚点与别名
base: &b
  x: 1
copy: *b
```

- **类型推断（core schema）**：`null`/`~`/空 → null；`true/false`
  （含大小写变体）→ bool；十进制/`0x`/`0o` → 整数；带 `.`/`e` → 浮点；
  `.inf`/`-.inf`/`.nan` → ±Infinity/NaN；其余 → 字符串。
  超出 long 的整数与 32 位浮点保持字符串（语言只有 int/long/double，
  见 README 末尾取舍）。
- **块标量 chomping**：`|`（clip，默认，留一个换行）、`|-`（strip，
  去尾换行）、`|+`（keep，保留全部尾空行），折叠 `>` 同理；支持显式
  缩进指示数字（`|2`）。`key: |` 的父缩进取键所在行，`- |` 取横杠
  所在行。
- **锚点/别名**：`&name` 在值位置（标量或集合整体），`*name` 引用，
  同一树多处共享同一 JsonValue。未知别名报错。
- **写出**：2 空格缩进块风格；空集合写 `{}`/`[]`；子映射/子序列在
  `- ` 后用压缩形式；整值浮点补 `.0` 保型；`.inf`/`.nan` 原样写回；
  字符串若按 plain 写出会被重新解析成别的类型/结构（含空串、首尾
  空格、指示符开头、`": "`、`" #"`、控制字符）则自动加双引号并转义。

## 快速上手

```zan
using System.Yaml;
using System.Json;

JsonValue conf = Yaml.Parse(yamlText);     // 非法抛 YamlException（带行号）
JsonValue file = Yaml.Load("conf.yaml");   // 读文件

string host = file.Get("server").Get("host").AsString("");
int port = file.Get("server").Get("port").AsInt(0);   // int 值直接取

string out2 = Yaml.ToYaml(conf);           // 写出（键序不变）
Yaml.Save(conf, "copy.yaml");
```

## 明确不支持（报错或按普通文本，皆可预期）

- **多文档**：第二个 `---` 直接抛错（单文档 API，避免歧义）。
- **标签**（`!!str` 等）、**合并键**（`<<:`）、**复杂键**（`? ...`）、
  **指令**（`%YAML` 跳过不执行）、**引号内多行折叠**。
- **UTF-16 代理对不合并**：`\uD83D\uDE00` 每个 `\uXXXX` 独立编码，
  代理区码点落 U+FFFD——与 `System.Json` 的 `\u` 解码同一行为
  （`Encoding.Utf8FromCodePoint` 的契约）。 emoji 请直接以 UTF-8
  原文写入。
- plain 标量单行——跨行续行请用 `>` 折叠或引号（`"a\nb"`）。

## 已知取舍

- 整数统一按 long 解析：YAML 的任意精度整数超出 long 时保留原文
  字符串（写侧原样回吐，不丢数据，类型上是 string）。
- 无 `float`（32 位）类型：`1.5e10f` 这类后缀不是合法 core 标量，
  落字符串。
- 行内制表符缩进直接报错（YAML 规范禁止）；行内容里的 tab 原样保留。
- 流式集合跨行拼接时行号按起始行计，深层流式报错的行号是近似的。
