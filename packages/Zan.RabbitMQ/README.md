# Zan.RabbitMQ

Pure Zan asynchronous AMQP 0-9-1 client for RabbitMQ.

## Features
- **Zero External Dependencies**: Pure native Zan network implementation over asynchronous sockets.
- **Connection Tuning**: Full SASL PLAIN authentication, automatic channel-max, frame-max, and heartbeat negotiation.
- **Channel Multiplexing**: Concurrent logical channels over a single physical TCP/TLS connection with atomic frame serialization.
- **Topologies**: Exchange declaration (direct, topic, fanout, headers), queue declaration, and binding/unbinding.
- **High-Throughput Streaming**: Chunked payload frame streaming, publisher confirms (`Confirm.Select`), and prefetch rate limiting (`Basic.Qos`).
- **Robust Delivery Pipeline**: Auto and manual acknowledgment (`Basic.Ack`, `Basic.Nack`, `Basic.Reject`).

## Quick Start

```zan
using System;
using Zan.RabbitMQ;

class Program {
    static async void Main() {
        AmqpConnection conn = await AmqpConnection.ConnectAsync(
            "127.0.0.1", 5672, "guest", "guest", "/");
        if (conn == null) { return; }

        AmqpChannel channel = await conn.CreateChannelAsync();
        await channel.ExchangeDeclareAsync("logs", "direct", true, false);
        string q = await channel.QueueDeclareAsync("app.events", true, false, false);
        await channel.QueueBindAsync(q, "logs", "event.user");

        // Publish
        BasicProperties props = new BasicProperties();
        props.contentType = "application/json";
        await channel.BasicPublishAsync("logs", "event.user", "{\"event\":\"login\"}", props);

        // Consume
        await channel.BasicConsumeAsync(q, false, async (ch, msg) => {
            Console.WriteLine("Received: " + msg.Body());
            ch.BasicAckAsync(msg.deliveryTag, false);
        });
    }
}
```
