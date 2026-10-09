# Zan.Diagnostics

进程控制与服务端可观测性（纯 Zan，无原生依赖）。`using System.Diagnostics;`
按需拉入，命名空间保留零破坏。

## 内容（stdlib/System/Diagnostics 整目录迁入）

| 文件 | 内容 |
|---|---|
| `Log.zan` | 结构化日志（Log/LogLevel，落盘与内存环形缓冲） |
| `ServerMetrics.zan` | 请求/SQL/慢路径指标与错误汇点（WebApp 监控数据源） |
| `Process.zan` | 子进程派生、输出捕获及系统关联程序打开（Process/ProcessResult） |
| `ProcessControl.zan` | 进程树控制（挂起/恢复/终止） |
| `ProcessList.zan` | 系统进程枚举（ProcessEntry） |
| `ProcessHost.zan` | 常驻子进程宿主（保活与重启） |
| `Privileges.zan` | 特权检查（管理员/提权探测） |
| `ServiceProcess.zan` | Windows 服务集成（SCM 状态码快照：查询/状态/PID） |

## 系统关联程序

`Process.OpenUrl(uri)` 和 `Process.OpenPath(path)` 分别打开绝对 URI、现有文件或目录；
相对路径按当前工作目录解析，参数不经 shell 解释。Windows 使用 ShellExecuteW，
Linux 使用 PATH 中的 xdg-open，macOS 使用 /usr/bin/open。返回 true 表示处理器已启动，
不代表资源加载成功；缺少处理器或输入无效返回 false。无桌面的 Linux 环境需要自行
提供关联程序；WASI、移动平台目前返回 false。GUI 的 `App.OpenInSystemBrowser` 复用此入口。

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
