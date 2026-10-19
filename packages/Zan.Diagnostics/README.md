# Zan.Diagnostics

进程控制与服务端可观测性（纯 Zan，无原生依赖）。`using System.Diagnostics;`
按需拉入，命名空间保留零破坏。

## 内容（stdlib/System/Diagnostics 整目录迁入）

| 文件 | 内容 |
|---|---|
| `Log.zan` | 日志门面：进程级滚动文件日志 + 进程级落地（历史 API 保持不变） |
| `LogHub.zan` | 结构化日志注册表：落地端集合 + 作用域栈（**实例级**，使用者持有） |
| `Logger.zan` | 分类日志器：绑定到 `LogHub`，按分类带结构化字段写日志 |
| `LogSink.zan` | 落地端契约（基类）：console / file / memory 之外可自建 |
| `LogEvent.zan` | 结构化事件模型（级别/分类/字段/作用域/链路），值对象语义 |
| `LogFormatter.zan` | 事件渲染：TEXT 与 JSON（JSON 走 `JsonValue`，不手拼） |
| `LogPaths.zan` | 日志文件路径展开与按大小滚动（唯一实现） |
| `Sinks/ConsoleLogSink.zan` | 控制台落地端 |
| `Sinks/FileLogSink.zan` | 滚动文件落地端 |
| `Sinks/MemoryLogSink.zan` | 内存环形缓冲落地端（自测断言/进程内日志面板） |
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

## 结构化日志

历史 API（`Log.Configure` / `Log.Info` / `Log.SetFormat` …）语义不变；新增的是
**事件 → 落地端**这条链路：

```zan
// 注册表（实例级）：使用者持有，生命周期跟着使用者走
LogHub hub = LogHub.WithProcessSinks();   // 进程级 console + 滚动文件
MemoryLogSink mem = new MemoryLogSink(256);
hub.AddSink(mem);                          // 再加自己的落地端

// 分类 + 结构化字段
Logger log = hub.Logger("app");
log.Info("request done", Logger.Fields("path", "/api/x", "ms", "12"));

hub.BeginScope("req-1");                   // 作用域随事件落地
// ...
hub.EndScope();

hub.Shutdown();                            // 退出前收尾
```

- 事件模型 `LogEvent` 是**值对象**：`WithField` / `WithScope` 返回派生副本，
  原事件不被改，多个落地端共用同一事件不会互相污染。
- 作用域由 `LogHub` 持有（`BeginScope` / `EndScope`），随事件落地。
- 级别过滤：全局 `Log.minLevel` + 每个 `Logger` 可单独覆盖（`MinLevel`）。
- JSON 输出走 `JsonValue` 序列化：控制字符、U+2028 等会被正确转义，
  手拼转义表会漏，产出非法 JSON。
- 落地端写失败必须自己吞掉（日志是诊断旁路，不拖垮业务）。
- **注册表是实例级而非静态**：包级 static 持有的 rc 集合在进程退出时
  不会被释放（编译器只释放主编译单元的静态字段），集合里的落地端、
  事件、字段字典会整条留到进程结束，`--check-leaks` 直接报泄漏。
  同理，`stdlib` 的 `Activity.Current` 也需要 `Stop()` 显式清空。

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
