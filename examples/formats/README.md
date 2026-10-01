# examples/formats — 序列化格式示例

仓库自带 JSON（`System.Json`）；这里六个示例覆盖 `packages/` 里的六个
格式包：两个文本配置格式（TOML/YAML）、一个文档格式（XML）、三个二进
制格式（CBOR/MessagePack/Protobuf wire）。文本三件套的值树统一落在
`System.Json.JsonValue` 上——解析结果直接喂给任何已用 JSON 的代码；
二进制三件套面向体量与吞吐敏感的存储/传输场景。

每个示例都是单文件、自校验的完整程序（含往返断言与一条错误路径）。

编译运行（在仓库根目录）：

```
build/zanc.exe examples/formats/toml_config.zan --auto-stdlib -o toml.exe
./toml.exe
```

| 文件 | 格式 | 包 | 演示要点 |
| --- | --- | --- | --- |
| `toml_config.zan` | TOML | `Zan.Toml` | 表/数组表、十/十六/八进制整数与下划线、点路径导航、写出往返（表无序，比对值不比文本）、重复表头报错 |
| `yaml_config.zan` | YAML | `Zan.Yaml` | 块映射/`- k: v` 压缩序列、core schema 类型（`0x`、引号保字符串）、块标量 `\|`、行内注释、文本级往返、重复键报错 |
| `xml_basics.zan` | XML | `Zan.Xml` | DOM 解析/构建/序列化、属性与 InnerText、缺失判空范式、往返、非法标签报错（带行列号） |
| `cbor_basics.zan` | CBOR | `Zan.Cbor` | RFC 8949 编解码（最短整数形态）、Save/Load 文件对称、与 JSON 体量对比、截断报错 |
| `msgpack_basics.zan` | MessagePack | `Zan.MsgPack` | 最短前缀整数形态、Save/Load、与 JSON 体量对比、保留类型 0xC1 报错 |
| `protobuf_wire.zan` | Protobuf | `Zan.Protobuf` | 手工 wire 编解码（varint/长度界定/嵌套消息）、未知字段 Skip 的向前兼容、无 .proto 代码生成的小协议场景、截断报错 |

选型速查：

- 人要手写手改的配置 → TOML（运维向）或 YAML（编排/嵌套深）
- 程序间交换文档/报文，人可能要看 → XML
- 程序间交换数据，体量优先 → CBOR（自描述、可流式）或 MessagePack
  （生态广）；两者都能和 `System.Json` 树直通
- 带字段号演进的紧凑 RPC 协议 → Protobuf wire（字段号即契约，向后/
  向前兼容靠 Skip 未知字段）
