# Zan.Resilience

服务治理与弹性容错策略包（纯 Zan，无原生依赖）：
`using System.Resilience;` 按需引入。

## 内容

| 类型 | 说明 |
|---|---|
| `CircuitBreaker` | 工业级熔断器状态机（Closed / Open / HalfOpen），支持故障阈值与自动探测自愈 |
| `TokenBucketRateLimiter` | 令牌桶限流器，支持突发流量缓冲与单调时钟平滑补桶 |
| `RetryStrategy` | 弹性重试策略，支持指数退避与随机抖动重试计算 |
| `CircuitState` | 熔断器状态枚举 |
