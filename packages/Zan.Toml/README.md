# Zan.Toml

TOML v1.0 for Zan：递归下降解析器 + 写出器，解析结果落在
`System.Json.JsonValue` 上（对象/数组/字符串/int/double），纯 Zan 实现，
无原生依赖。命名空间 `System.Toml`。

## 快速上手

```zan
using System.Json;
using System.Toml;

// 解析（非法抛 TomlException，带行列号）
JsonValue conf = Toml.Parse(tomlText);
JsonValue conf2 = Toml.Load("app.toml");          // 读文件

// 导航（与 System.Json 同一套访问器；可空返回先判空再取值）
JsonValue portV = conf.PathGet("server.port");
long port = 8080;
if (portV != null) { port = portV.AsLong(8080); }
JsonValue fruits = conf.Get("fruit");               // [[fruit]] 落成数组
if (fruits != null && fruits.Count() > 0) {
    JsonValue f0 = fruits.At(0);
    JsonValue cv = f0.PathGet("physical.color");
    if (cv != null) { Console.WriteLine(cv.AsString("")); }
}

// 写出
string text = Toml.ToToml(conf);
Toml.Save(conf, "out.toml");
```

## 设计要点

- **值模型即 JsonValue**：解析产物与写出的输入都是 `JsonValue`，和
  `System.Json` 无缝互转（`JsonValue.Parse(toml文本里的json段)` /
  `Toml.ToToml(json值)`）。没有独立的 TOML 文档类，也没有日期类型。
- **日期时间按原文存字符串**：`1979-05-27`、`07:32:00`、
  `1979-05-27 07:32:00Z` 落成 `NewStr`，写出时按字符串加引号。需要
  日期语义的调用方自己解析——JsonValue 没有日期形态，不假装有。
- **写出语义级往返**：Parse → ToToml → Parse 得到语义相同的树，再写
  幂等（t2 == t1）。不保证字节级还原：对象值键统一写成 `[表头]` 区块
  （同层键序因此可能变化——TOML 表本无序）、数字统一成最短形态
  （`1.25e3` → `1250`）、日期统一成字符串。数组套对象写成行内表
  （`[{ k = "v" }, 7]`）。
- **TOML 无 null**：JsonValue 树里含 null 时 `ToToml` 抛
  `TomlException`。
- **重复表头即错**：普通 `[table]` 定义两次报错；`[[array]]` 允许
  重复追加。查重键带数组元素序号——不同元素下的同名子表
  （`[[fruit]]` 各自的 `[fruit.physical]`）互不冲突。
- **整数全 64 位**：十进制/`0x`/`0o`/`0b`/下划线分隔全支持，溢出
  int64 报错；`-9223372036854775808` 特判放行。浮点经
  `JsonValue.DoubleOf`（正确舍入）。
- **四种字符串全支持**：基本串（含 `\uXXXX`/`\UXXXXXXXX` 转义）、
  多行基本串（行尾反斜杠续行）、字面串、多行字面串。转义未知报错。
- **深度上限 512**：读侧递归解析、写侧递归序列化，两侧一起限，
  防栈溢出。

## 已知取舍

- 行内表写成多行是合法的（宽容模式）；TOML 规范禁止的形态
  （如数组内混注释头）不逐一校验。
- 键比较按字节原文（UTF-8），不做 Unicode 规范化。
- `inf`/`nan` 解析为 double 的对应值；写出按最短浮点形态（可能变成
  字符串语义丢失），业务里避免写 inf/nan。
