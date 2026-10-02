# Zan.AppUpdate 通用版本更新与防篡改自愈包

`Zan.AppUpdate` 是为 Zan 语言桌面应用、游戏与服务端打造的通用版本管理、发布打包、客户端更新调度与文件完整性自愈框架。

---

## 核心特性

1. **版本发布打包（ReleasePackager）**
   - 递归扫描指定发布目录（支持无限层级子目录），自动收集全部文件。
   - 对每个文件提取大小并计算 32 位 MD5 校验和。
   - 一键生成标准规范的 `update.manifest.json` 版本清单文件。
   - 基于标准库 `Zip` 高效压缩打包为 `.zip` 发布包，同时可将清单打包入 Zip 或独立导出。
   - 计算 Zip 包自身 MD5 与总大小，确保发布包整体防篡改与完整性。

2. **版本进度实时更新与监控（UpdateProgress）**
   - 精细化状态机阶段：`Checking`（检查版本）、`Downloading`（下载）、`Verifying`（校验完整性）、`Extracting`（解压安装）、`SelfHealing`（防篡改自愈）、`Completed`（完成）、`Failed`（失败）。
   - 携带实时百分比（0 - 100%）、已传输/处理字节数、总字节数、当前处理文件名与状态提示语。
   - 纯委托回调机制（`UpdateProgressCallback`），方便控制台 CLI、GUI 界面进度条或游戏 HUD 零摩擦接入。
   - 内置开箱即用的 `ConsoleProgressRenderer` 控制台字符进度条。

3. **MD5 防篡改比对与精准自愈（TamperDetector）**
   - 本地全文件 MD5 完整性巡检：启动时或更新后对所有关键程序/资源文件进行指纹比对。
   - 自动识别三种异常状态：`Missing`（文件丢失/被误删）、`Tampered`（文件被篡改/损坏/哈希不匹配）、`Ok`（正常）。
   - **自动精准还原自愈**：从官方 `.zip` 压缩包中按需提取被篡改或丢失的文件，覆盖写入本地目标路径并自动进行二次 MD5 复检，确保程序快速恢复至官方纯净状态，抵御恶意文件篡改与资源损坏。

4. **安全升级与版本覆盖（AppUpdater）**
   - 支持语义化版本判断与增量/全量更新调度。
   - 支持解压前对整包 Zip 进行 MD5 预校验，杜绝网络传输损毁或劫持。
   - 支持升级前旧版本自动备份（`_backup/v_<timestamp>`），出现意外可快速回退。

---

## 架构组件清单

| 类名 / 文件 | 职责说明 |
| :--- | :--- |
| `UpdateFileEntry` | 单个文件的路径、大小、MD5 指纹与必需标志，支持 JSON 互转 |
| `UpdateManifest` | 包含版本号、发布时间、更新日志、包 MD5 与文件列表的清单模型 |
| `ReleasePackager` | 服务端/发布端：扫描目录、计算哈希、生成 Manifest 并 Zip 压缩 |
| `UpdateProgressInfo` / `UpdatePhase` | 进度数据结构、阶段枚举与状态文案转换 |
| `TamperDetector` | 客户端防篡改比对器与基于 Zip 的受损文件精准还原自愈引擎 |
| `AppUpdater` | 客户端一键更新管理器，串联版本检查、解压安装与防篡改闭环 |

---

## 快速上手

### 1. 服务端 / 发布端：制作版本发布包

```zan
using System;
using Zan.AppUpdate;

class Publisher {
    static void Main() {
        string appDir = "dist/myapp";
        string outZip = "releases/myapp_1.2.0.zip";
        string outManifest = "releases/myapp_1.2.0.manifest.json";

        UpdateProgressCallback onProgress = info => {
            Console.WriteLine(info.percent + "% - " + info.message);
        };

        UpdateManifest manifest = ReleasePackager.Pack(
            appDir,
            outZip,
            outManifest,
            "MyApp",
            "1.2.0",
            "1. 支持暗黑主题\n2. 修复已知性能问题",
            "https://updates.example.com/myapp_1.2.0.zip",
            onProgress
        );

        Console.WriteLine("打包完成，Zip MD5: " + manifest.packageMd5);
    }
}
```

### 2. 客户端：版本更新与解压安装

```zan
using System;
using Zan.AppUpdate;

class UpdaterDemo {
    static void Main() {
        AppUpdater updater = new AppUpdater();

        // 绑定通用控制台进度条
        updater.onProgress = ConsoleProgressRenderer.Render;

        // 校验整包 MD5 并解压更新（自动备份旧版本）
        bool ok = updater.ApplyPackage("downloads/myapp_1.2.0.zip", "C:/Program Files/MyApp", null, true);
        if (ok) {
            Console.WriteLine("更新成功！");
        }
    }
}
```

### 3. 客户端：防篡改自检与文件自动还原

```zan
using System;
using Zan.AppUpdate;

class IntegrityCheckDemo {
    static void Main() {
        string appDir = "C:/Program Files/MyApp";
        string manifestPath = appDir + "/manifest.json";
        string recoveryZip = "cache/myapp_latest.zip";

        // 1. 加载官方发布清单
        UpdateManifest manifest = UpdateManifest.LoadFromFile(manifestPath);

        // 2. 检查本地文件 MD5 防篡改状态
        IntegrityReport report = TamperDetector.Check(appDir, manifest, ConsoleProgressRenderer.Render);
        Console.WriteLine(report.Summary());

        // 3. 若发现文件被篡改或丢失，自动从 Zip 恢复纯净版本
        if (!report.isClean) {
            Console.WriteLine("发现文件异常，开始执行自动自愈还原...");
            int restored = TamperDetector.RestoreFromZip(appDir, report, recoveryZip, ConsoleProgressRenderer.Render);
            Console.WriteLine("成功自愈文件数: " + restored);

            // 4. 二次复核
            IntegrityReport checkAgain = TamperDetector.Check(appDir, manifest, null);
            if (checkAgain.isClean) {
                Console.WriteLine("自愈完毕，程序文件已恢复官方纯净状态！");
            }
        }
    }
}
```

---

### 4. 发行方签名（防更新链劫持，生产必配）

整包 MD5 只保证**完整性**不保证**真实性**——能同时替换清单与 Zip 的通道攻击者可以
两者一起换。清单签名把信任锚定在发行方私钥上：

```zan
using System;
using System.Security.Cryptography;
using Zan.AppUpdate;

// 发布端：打包后用 RSA 私钥签清单（密钥为 hex 大端字符串，可由 PEM 导出）
UpdateManifest m = ReleasePackager.Pack(...);
ReleasePackager.SignManifest(m, nHex, dHex);   // 签名写入 m.signature
m.SaveToFile(manifestPath);                    // 签名后重新保存清单

// 客户端：启动时配置发行方公钥（hex 模数、hex 指数）
AppUpdater.SetTrustedSigner(nHex, "010001");
// 之后所有 UpdateTo 强制验签：清单无签名或验签失败直接中止（fail-closed）。
// 不配置公钥则维持"仅整包 MD5"的兼容模式——仅供开发环境使用。
```

安全基线（本包内置，无需配置）：

- **zip-slip 防护**：更新包/恢复包内条目路径经 `UpdateFileEntry.SafeRelPath`
  检查，含 `..` 段、盘符或绝对路径的条目一律拒绝并中止整包（fail-closed），
  与 stdlib `Tar.SafeName` 同规则。
- **原子覆盖**：所有覆盖写先落同目录随机 token 临时文件再 rename 替换
  （`MOVEFILE_REPLACE_EXISTING`），断电/崩溃不会截断活文件，只可能留下
  可清理的 `.tmp`。
- **真备份**：`backupOld=true` 时，每个将被覆盖的旧文件先复制进
  `_backup/<原相对路径>`，回滚可用（`_backup` 不参与打包扫描）。

---

## 清单文件（Manifest）规范样例

```json
{
  "appName": "MyApp",
  "version": "1.2.0",
  "releaseDate": "2026-09-21 12:00:00",
  "packageUrl": "https://updates.example.com/myapp_1.2.0.zip",
  "packageSize": 2564810,
  "packageMd5": "e61278c46e75fde5cff4711de4bbdaf8",
  "changelog": "更新日志描述...",
  "files": [
    {
      "path": "bin/myapp.exe",
      "size": 184520,
      "md5": "bb1a652836b87b59a11c656b5e39f155",
      "required": true
    },
    {
      "path": "config.json",
      "size": 128,
      "md5": "bf8c257d57bbf841fb7f2173bbf71623",
      "required": true
    }
  ]
}
```
