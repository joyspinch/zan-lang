# Zan.Browser.Firefox

Zan 平台进程内嵌入式火狐（Gecko）浏览器与 AI 智能体自动化原生驱动包。

## 特性

- **零外部独立进程**: 直接基于 `xul.dll` / `gecko_embed.dll` 原生 C-ABI 动态装载与就地运行，不拉起任何外部 `firefox.exe`。
- **微秒级 AI 智能体操作 (FirefoxAgent)**:
  - `Snapshot()`: 语义 DOM 提取与 Token 降噪压缩，自动分配 `[e1]`, `[e2]` 索引。
  - `Fill(ref, text)`: 原生原型链与事件派发，支持 React/Vue 受控组件。
  - `Click(ref)`: 真实原生指针事件链（PointerDown -> MouseDown -> PointerUp -> MouseUp -> Click）。
  - `RunBatch(code)`: Code-Native 复合事务原位执行，微秒级求值。
- **确定性硬件防关联指纹 (FingerprintProfile)**:
  - 核心数、显卡 WebGL/Vendor、屏幕分辨率、时区、语言按需配置与多店铺隔离。
- **持久化沙箱与店铺隔离**:
  - `userContextId = 0`: 全局默认会话，持久化 Cookie 与账号登录态；
  - `userContextId > 0`: 隔离店铺容器，多账号互不串号。

## 快速上手

```csharp
using Zan.Browser.Firefox;

// 1. 初始化内核
FirefoxBootstrap.Init();

// 2. 创建智能体标签页并导航
var tab = FirefoxAgentSession.CreateTab("https://www.example.com");

// 3. 提取语义快照并驱动大模型交互
PageSnapshot snap = tab.Snapshot();
Console.WriteLine(snap.ToPromptText());

// 4. 表单填写与点击
tab.Fill("e1", "admin");
tab.Click("e3");
```
