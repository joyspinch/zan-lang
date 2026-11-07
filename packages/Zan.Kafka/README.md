# Zan.Kafka

Pure Zan asynchronous Apache Kafka wire protocol client.

## Features
- **Zero External Dependencies**: Pure native Zan network implementation over asynchronous sockets.
- **Hardware-Accelerated CRC32C**: Hardware instruction-accelerated CRC-32C message integrity calculation.
- **RecordBatch v2 Standards**: Full support for Kafka 0.11+ RecordBatch v2 binary framing, Varint/Zigzag encoding, and structured message headers.
- **Connection Multiplexing**: Correlation-ID driven request/response multiplexing over asynchronous TCP sockets.
- **High-Performance Producer**: Partition targeting, publisher acks (0, 1, -1), batch delivery, and metadata topology resolution.
- **Streaming Consumer**: Non-blocking `PollAsync`, high watermark detection, and automatic offset tracking.

## Quick Start

```zan
using System;
using Zan.Kafka;

class Program {
    static async void Main() {
        KafkaConnection conn = await KafkaConnection.ConnectAsync("127.0.0.1", 9092);
        if (conn == null) { return; }

        KafkaProducer producer = new KafkaProducer(conn);
        ProduceResult res = await producer.SendAsync("orders.events", 0, "order-101", "{\"status\":\"paid\"}");
        if (res.IsSuccess()) {
            Console.WriteLine("Produced to offset: " + res.offset);
        }

        KafkaConsumer consumer = new KafkaConsumer(conn);
        List<KafkaRecord> records = await consumer.PollAsync("orders.events", 0, 1000);
        for (int i = 0; i < records.Count; i = i + 1) {
            Console.WriteLine("Received: " + records[i].value);
        }
    }
}
```
