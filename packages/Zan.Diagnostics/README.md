# Zan.Diagnostics

进程控制与服务端可观测性（纯 Zan，无原生依赖）。`using System.Diagnostics;`
按需拉入，命名空间保留零破坏。

## 内容（stdlib/System/Diagnostics 整目录迁入）

| 文件 | 内容 |
|---|---|
| `Log.zan` | 结构化日志（Log/LogLevel，落盘与内存环形缓冲） |
| `ServerMetrics.zan` | 请求/SQL/慢路径指标与错误汇点（WebApp 监控数据源） |
| `Process.zan` | 子进程派生与输出捕获（Process/ProcessResult） |
| `ProcessControl.zan` | 进程树控制（挂起/恢复/终止） |
| `ProcessList.zan` | 系统进程枚举（ProcessEntry） |
| `ProcessHost.zan` | 常驻子进程宿主（保活与重启） |
| `Privileges.zan` | 特权检查（管理员/提权探测） |
| `ServiceProcess.zan` | Windows 服务集成（SCM 状态码快照：查询/状态/PID） |

## 消费者

- `Zan.Data`（DbTrace 日志）、`Zan.Desktop`（多处进程/窗口探测）、
  `Zan.AppUpdate`（更新进度日志）
- stdlib 内 `System.Web`（WebApp/HttpContext 监控与错误落盘）、
  `System.Scripting` 已随 `Zan.Scripting` 包迁出（Lua 宿主用 Log）
- `examples/input/service_task_demo.zan`、conformance `win_serviceprocess_smoke.zan`
- ZanIDE（编辑器内派生工具进程）

## 不在包内

- `Stopwatch` 在 stdlib 根（`System/Stopwatch.zan`）——生成器子编译闭包
  需要，且 `using System;` 即得。
