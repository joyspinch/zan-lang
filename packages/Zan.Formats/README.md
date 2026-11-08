# Zan.Formats

Zan 官方统一多格式序列化套件，包含二进制与文本数据交换格式实现：

- **Formats.Bson**：MongoDB BSON 二进制 JSON 编码与解码
- **Formats.Cbor**：RFC 8949 简明二进制对象表示（CBOR）
- **Formats.MsgPack**：MessagePack 高效二进制序列化
- **Formats.Protobuf**：Protocol Buffers 基础 Wire 格式与 Varint 读写
- **Formats.Toml**：TOML 配置解析器
- **Formats.Yaml**：YAML 数据格式解析器
- **Formats.Csv**：RFC 4180 高性能 CSV 读写引擎

## 快速使用

各格式均保持独立的专业命名空间，开箱即用：

```zan
using Formats.Bson;
using Formats.Cbor;
using Formats.MsgPack;
using Formats.Toml;
using Formats.Yaml;
using Formats.Csv;
```
