# Zan.Bson

BSON（Binary JSON，MongoDB 的存储/交换格式）的纯 Zan 实现：
`System.Json.JsonValue` 树与线上字节的 schemaless 互转。命名空间
`System.Bson`，零外部依赖，线上格式为小端（区别于 MessagePack 的
大端）。姊妹包：`Zan.MsgPack`、`Zan.Cbor`（值模型与形态约定对齐）。

## 特性

- **类型覆盖**：double / string / document / array / binary / ObjectId /
  bool / datetime / null / int32 / int64。
- **规范整型选择**：整型形态数字按值域自动选 int32（±2³¹ 内）或
  int64；浮点形态走 double——int/float 形态往返保持。
- **读侧还原约定**：binary 按 UTF-8 尽力解码（值模型无二进制，非法
  序列收敛 U+FFFD）；ObjectId → 24 位小写十六进制；datetime（UTC 毫秒）
  → ISO 8601 UTC 字符串。regex/code/timestamp/decimal128/minkey/maxkey
  等值模型外的类型抛 `BsonException`。
- **严格长度校验**：文档声明总长必须与实际消耗一致（含嵌套），字符串
  必须带终止 NUL，越界即报错——带偏移量的错误消息。
- **数组语义**：按 MongoDB 惯例按元素顺序还原（键名 "0","1",… 不参与
  语义）；写出时自动生成数字键。
- **多文档流**：`Unpack` 读首个文档后忽略尾部字节，可从流里取第一个。

## API 一览

| 入口 | 用途 |
|------|------|
| `Bson.Pack(doc)` | JsonValue 文档 → BSON 字节（根必须是对象） |
| `Bson.Unpack(bytes)` / `Unpack(bytes, off, count)` | 字节（切片）→ JsonValue |
| `Bson.Save(doc, path)` / `Bson.Load(path)` | 文件对称读写 |

## 示例

```zan
using System;
using System.Bson;
using System.Json;

class Demo {
    static void Main() {
        JsonValue doc = JsonValue.Parse("{\"id\":1234,\"tags\":[\"a\"]}");
        byte[] bin = Bson.Pack(doc);
        JsonValue back = Bson.Unpack(bin);
    }
}
```

更多可运行的例子见 `examples/formats/bson_basics.zan`。
