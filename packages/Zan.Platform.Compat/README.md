# Zan.Platform.Compat - 早期平台 API 历史兼容包

> **成熟度：冻结** —— 历史兼容转发层：只保证旧代码编译通过，不加新能力、
> 不配独立测试；新代码请直接用标准库 `System.OperatingSystem`/`Environment`。

`Zan.Platform.Compat` 是为兼容 Zan 早期历史代码而提供的 API 转发层。

在 Zan 0.2 早期版本中，系统环境与操作系统判断曾位于 `Platform.Runtime` 命名空间。新版本已全面对齐现代标准，将跨平台能力统一收敛至标准库的 `System.OperatingSystem` 与 `System.Environment`。

为了保证旧项目在不修改代码的情况下平滑升级，可以引入本包。

---

## 包含内容

命名空间：`Platform`

```zan
using Platform;

// 历史 API 示例：
bool isWin = Runtime.IsWindows();
bool isLinux = Runtime.IsLinux();
bool isMac = Runtime.IsMacOS();
string plat = Runtime.GetPlatform(); // "windows" | "linux" | "macos" | "wasi"
```

---

## 迁移建议

对于新开发的 Zan 项目，**推荐直接使用标准库中的现代 API**，无需依赖本包：

```zan
using System;

// 推荐的标准写法：
bool isWin = OperatingSystem.IsWindows();
bool isLinux = OperatingSystem.IsLinux();
bool isMac = OperatingSystem.IsMacOS();
bool isWasi = OperatingSystem.IsWasi();

// 环境变量与路径：
string appDir = Environment.CurrentDirectory;
string userHome = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
```
