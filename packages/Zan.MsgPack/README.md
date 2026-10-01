# Zan.MsgPack

MessagePack for Zan：`System.Json.JsonValue` ↔ 线上字节的 schemaless
互转，纯 Zan 实现（IEEE-754 位构造、大端装配），无原生依赖。命名空间
`System.MsgPack`。

## 快速上手

```zan
using System.Json;
using System.MsgPack;

// 编码：JsonValue → bytes
JsonValue conf = JsonValue.Parse("{\"port\":8080,\"tags\":[\"a\",\"b\"]}");
byte[] bytes = MsgPack.Pack(conf);

// 解码：bytes → JsonValue
JsonValue back = MsgPack.Unpack(bytes);

// 文件
JsonValue fromDisk = MsgPack.Load("cache.msgpack");
MsgPack.Save(conf, "cache.msgpack");
```

## 设计要点

- **值模型即 JsonValue**：与 `System.Json`、`Zan.Toml` 同一棵值树，
  三种格式之间转接零中间层（`Toml.Parse(...)` → `MsgPack.Pack(...)`
  直接可走）。
- **整数按规范最短形态**：0..127 与 -32..-1 单字节 fixint，向上依次
  uint8/16/32/64（正）与 int8/16/32/64（负）；str 走 fixstr/str8/16/32；
  容器走 fixarray/fixmap → array16/32 → map16/32。
- **int/float 形态保持**：编码按 `JsonValue.IsInt()` 分流——NewInt 与
  整数字面量走整数家族，NewDouble 即使数值恰为整数（7.0）也走
  float64；往返后形态不变（IsInt 仍为真/假）。
- **读侧宽容、写侧规范**：bin 家族（c4-c6）按 UTF-8 尽力解码成字符串
  （非法序列收敛 U+FFFD）；float32（0xCA）解码成 double；uint64 高位
  溢出 int64 时转 double（53 位有效位之外精度丢失，文档化取舍）。
- **timestamp extension（type -1）→ ISO 8601 UTC 字符串**：32/64/96
  位三种形态都认，走 `DateTime.FromUnixSeconds`；其他 extension 类型
  报 `MsgPackException`（无 schema 无法安全还原）。
- **IEEE-754 纯 Zan 位构造**：语言没有 reinterpret。编码侧归一化
  构造（±∞/NaN 收口），解码侧二进制定点逐位累加 + 2 的幂（各项精确）
  ——与 `Zan.Data` 的 MySqlWire/TdsMessage 同一算法形状，次正规、
  ±∞、NaN、-0.0 解码全部还原。
- **深度上限 512**：Pack/Unpack 递归两侧一起限，防栈溢出；与
  `System.Json` 对称。
- **错误即抛**：截断、保留码 0xC1、未知 ext、map 键非字符串都抛
  `MsgPackException`（带字节偏移）。

## 已知取舍

- 写出不产 float32（语言无 32 位浮点；读侧兼容）。
- 写出不产 timestamp/bin/ext（JsonValue 没有对应形态：时间按字符串
  语义存取，二进制请走 `Zan.Protobuf` 的 bytes 字段）。
- map 键限定字符串（JsonValue 对象键即字符串；非字符串键报错）。
- uint64 高位（≥2^63）解码为 double。
