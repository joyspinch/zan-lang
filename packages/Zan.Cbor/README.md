# Zan.Cbor

CBOR（RFC 8949，RFC 7049 的继任）for Zan：`System.Json.JsonValue` ↔
线上字节的 schemaless 互转，纯 Zan 实现，无原生依赖。命名空间
`System.Cbor`。

## 快速上手

```zan
using System.Json;
using System.Cbor;

JsonValue conf = JsonValue.Parse("{\"port\":8080,\"tags\":[\"a\",\"b\"]}");
byte[] bytes = Cbor.Encode(conf);
JsonValue back = Cbor.Decode(bytes);

JsonValue fromDisk = Cbor.Load("cache.cbor");
Cbor.Save(conf, "cache.cbor");
```

## 设计要点

- **写侧规范最短、读侧全形态**：写侧全部确定长度（definite），
  argument 按 RFC 的最短形态（≤23 内联 → 24/25/26/27 定宽），负整数
  走 major type 1（`n = ~v`，long.MinValue 也不溢出）；读侧额外接受
  indefinite 长度（文本/字节串分片拼接、数组/映射读到 break）、
  半精度 0xF9 / 单精度 0xFA / tag / 保留码校验。
- **tag 处理对齐 RFC 互操作建议**：tag 0（RFC3339 文本）原样透传，
  tag 1（纪元秒，int 或 float）→ ISO 8601 UTC 字符串；其余 tag 剥掉
  取内层继续解码——不认识的 tag 不该让数据不可读。
- **byte string → 字符串**：JsonValue 没有字节形态，major 2 按 UTF-8
  尽力解码（非法序列收敛 U+FFFD，内嵌 NUL 保留）。
- **IEEE-754 纯 Zan 位构造**：写侧归一化构造（±∞/NaN 收口），读侧
  半精度（全部值在 double 中精确）/单精度/双精度逐位累加还原
  （次正规、±∞、NaN、-0.0 全收）。与 `Zan.Data` 的 MySqlWire/
  TdsMessage 同一算法形状。
- **uint64 高位溢出 int64 → double**（53 位之外精度丢失，文档化）。
- **深度上限 512**，map 键必须是文本字符串，错误即抛 `CborException`
  （带字节偏移）。

## 已知取舍

- 写侧不产 indefinite / 半精度 / 最短浮点（固定 0xFB 64 位）——
  想要规范最短浮点（如 1.5 → `f93e00`）需要最短表示搜索，读侧已兼容
  这类输入；写侧不作（文档化取舍，二进制体积非紧要场景）。
- 写侧不产 tag（JsonValue 无 tag 语义）；时间按字符串语义存取。
- map 键限定字符串。
