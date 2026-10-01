# Zan.Protobuf

Protocol Buffers 线格式（wire format）编解码 for Zan：varint / ZigZag /
长度界定 / fixed64 / fixed32 五种 wire type，嵌套消息与 packed 重复字段，
未知字段跳过（含已废弃的 group 配对），IEEE-754 double 纯 Zan 位构造，
无原生依赖。命名空间 `System.Protobuf`。

**只做 wire format**——没有 .proto schema 解析、没有代码生成、没有
反射。字段号由调用方给出，与手写 .proto 的编码约定一致：schema 是
两端的共识，运行时只需要正确地编字节。

## 快速上手

```zan
using System.Protobuf;

// 编码（对应 proto: int32 a = 1; string b = 2; Inner c = 3;）
ProtoWriter w = new ProtoWriter();
w.WriteInt32(1, 150);                       // → 08 96 01
w.WriteString(2, "testing");                // → 12 07 ...
int m = w.BeginMessage(3);                  // 流式嵌套
w.WriteInt32(1, 150);
w.EndMessage();                             // → 1a 03 08 96 01
byte[] bytes = w.ToBytes();

// 解码：Next() 走字段，未知字段 Skip()
ProtoReader r = new ProtoReader(bytes);
while (r.Next()) {
    if (r.FieldNumber() == 1 && r.WireType() == 0) {
        long a = r.ReadInt32();
    } else if (r.FieldNumber() == 3) {
        ProtoReader inner = new ProtoReader(r.ReadMessage());
        // ... 或直接 r.Skip()
    } else {
        r.Skip();
    }
}
```

## 设计要点

- **writer 是增长缓冲**：byte[] 倍增扩容整块 `NativeMemory.Copy` 搬移
  （与 `System.IO.ByteBuffer` 同形），不用逐字节循环。`BeginMessage`/
  `EndMessage` 用字段号/载荷起点双栈支持任意嵌套，End 时载荷整体右移
  补 `tag + varint 长度` 头。packed 重复字段同一机制：Begin 后连写
  `RawVarint`/`RawFixed64` 再 End。
- **varint 是 64 位 LEB128**：负 int32/enum 按 64 位二补码 10 字节
  形态编码（规范行为）；算术右移的符号扩展位用掩码清掉。
- **sint32/sint64 走 ZigZag**：小负数不付 10 字节代价；逆映射的
  64 位形态用掩码模拟逻辑右移。
- **Skip 全 wire type**：varint / fixed64 / len / fixed32 直接跳，
  SGROUP（3）向下配对到 EGROUP（4）——嵌套深度限 512，防恶意构造；
  未知 wire type 报 `ProtoException`。
- **double 纯 Zan 位构造**：语言没有 reinterpret 转换。编码侧归一化
  构造位模式（与 Zan.Data 的 `TdsMessage.DoubleBits` 同一算法，±∞/NaN
  收口——归一化循环对非有限值不终止）；解码侧二进制定点逐位累加尾数
  （每项 2^(b-52) 恰可表示、和精确）再乘 2 的幂（与 MySqlWire 的
  `ieeeToText` 同形）。次正规数、±∞、NaN、-0.0 读取全部还原。
- **错误即抛**：截断、字段号 0、变长整数超 10 字节、长度越界、
  wire type 不匹配都抛 `ProtoException`（带字节偏移）。长度比较用
  减法（`n > len - pos`）——`pos + n` 会 int 溢出绕过检查。

## 已知取舍

- **没有 32 位 float**：Zan 只有 double；`.proto` 里的 `float` 字段
  需要自转位型或改 `double`（fixed32 原始读写不受影响）。
- **编码侧不产 -0.0 / inf / NaN 的 double**（归一化构造的固有边界）；
  解码侧全部还原。互操作对端若发这些值，按位型收。
- group 写出不支持（proto3 已废弃）；读取侧保留配对跳过以兼容旧流。
- packed 与非 packed 读取不做自动互换：按 wire type 读，与主流实现
  的"读者按线上形态取"行为一致。
