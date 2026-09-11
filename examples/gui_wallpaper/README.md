# Zan 动态桌面壁纸（gui_wallpaper）

把一张静态图钉在**桌面图标后面**并让它动起来的完整示例：人物轻摆 +
肩部呼吸起伏（条带扭曲场）、金币飘落、星芒闪烁、微尘上浮、元宝/灯笼
呼吸暖光。Windows 专用（WorkerW 钉嵌），Linux/macOS 无 WorkerW，
程序可跑但不钉桌面（用 ZAN_WALL_NO_PIN=1 体验同一渲染）。

## 构建与运行

```bash
zanc src/main.zan --auto-stdlib -o zan_wallpaper.exe            # 调试版（带控制台日志）
zanc src/main.zan --auto-stdlib --subsystem windows -o zan_wallpaper.exe   # 常驻版（无控制台）
```

运行（图源用环境变量指，任意 PNG/JPG/WebP）：

```bash
# PowerShell
$env:ZAN_WALL_IMAGE = "C:\path\to\photo.png"
.\zan_wallpaper.exe
```

- 不设 `ZAN_WALL_IMAGE` 时取 exe 旁边的 `wallpaper.png`；也没有时画
  程序化红金底，动画照常（示例可空跑）。
- 退出：`taskkill /IM zan_wallpaper.exe /F`（壁纸窗口不进任务栏）。
- 开机自启：`Win+R` → `shell:startup` → 放一个指向 exe 的快捷方式，
  并在脚本里先设好 `ZAN_WALL_IMAGE`（或把图改名 `wallpaper.png` 放
  exe 旁边）。

## 环境变量

| 变量 | 作用 |
|---|---|
| `ZAN_WALL_IMAGE` | 图源路径；缺省 exe 旁 `wallpaper.png` |
| `ZAN_WALL_EXIT_SECONDS` | N 秒后自动退出（无人值守验证用） |
| `ZAN_WALL_NO_PIN` | `1` = 不钉桌面，普通窗口运行（调试渲染） |

## 原理与定式（踩坑出处）

1. **钉嵌**：`App.CreateDarkStage` 建无框窗口（客户区=物理像素 1:1）→
   向 Progman 发 `0x052C` 让 shell 生成图标层下的 WorkerW →
   `SetParent` 过去 → `SetWindowPos` 拉满全屏。个别系统
   `SHELLDLL_DefView` 不在 Progman 直下，退回挂 Progman。
2. **DPI 时序**：进程的 DPI 感知是 Gui 外壳启动时才提升的——
   `GetSystemMetrics` 必须在 `CreateDarkStage` **之后**读才是物理像素；
   先读后建会拿到 DPI 虚拟化尺寸。
3. **人物动起来（活照）**：单张静图没有视频模型时，用 72 条水平带
   `BlitImage` 逐条位移：摆幅 smoothstep 包络自下而上（底部钉死、
   头部最大）、相位随高度偏移成鞭梢拖曳、肩部叠加高斯窗呼吸。
   条带间重叠 1px 防缝，整幅外扩 32px 防摆动露黑边。
   真正的眨眼/微笑需要图生视频模型（本示例所用 API 端点无视频模型）。
4. **省电门控**：前台窗口面积 ≥93% 屏幕时跳过整帧绘制只轮询
   （全屏应用与最大化窗口都算——只露一条任务栏时不值得空转）。
   按"完整覆盖"判会漏掉最大化窗口（98.7% 面积），实测烧满单核；
   放行 Progman/WorkerW/自己，避免桌面空闲时误判。
5. **粒子细节**：金币/星芒避开人脸框（飘过脸像痣）；币径按屏高定标；
   xorshift 固定种子，每次启动粒子分布一致便于回归对比。

## 性能（实测 3200×2000）

- 可见渲染时约 0.6 核（30fps 壁纸档，`Sleep(20)` 限速）；
  前台被全屏/最大化窗口覆盖时 ≈0（只轮询）。
