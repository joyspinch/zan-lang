# System.Net.Ntp

> 源码: `stdlib/System/Net/Ntp/NtpClient.zan`


## NtpClient (class)

SNTP 客户端（RFC 4330）——通过 UDP 123 端口获取墙上时钟时间，
这是没有电池供电 RTC 的设备获知日期的常见方式。

用法：
long unix = await NtpClient.QueryAsync("pool.ntp.org", 123, 3000);
if (unix > 0) { /* seconds since 1970-01-01 UTC */ }

- static long EPOCH_DELTA=2208988800;

- static async long QueryAsync(string host, int port, int timeoutMs)
  - 发送一次 SNTP 请求并返回 Unix 秒（UTC），
    超时或应答格式错误时返回 -1。<paramref name="timeoutMs"/> 限定
    等待应答数据报的时间。
