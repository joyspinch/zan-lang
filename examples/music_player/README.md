# 网易云音乐 - 沉浸播放实机示例 (HTML + CSS + Zan Code-behind)

本示例完整演示了 Zan 语言现代桌面界面的核心开发范式：**HTML 骨架 + CSS 视觉/动效 + Zan 原生 Code-behind 强类型逻辑**。

页面内**零 `<script>` 标签**，视觉效果与业务逻辑完全解耦，彻底告别在后端/业务代码中手写绝对坐标与微调像素的历史。

---

## 架构三位一体

1. **`src/App.html`（结构与视觉）**：
   - **DOM 语义树**：使用 `<zan-flex>`、`<div>`、`<span>`、`<button>` 等标准控件标签。
   - **经典三段式停靠**：
     - 顶栏 `dock="top"`（高 48px）；
     - 控制条 `dock="bottom"`（高 72px）；
     - 舞台区 `dock="fill"`（自动吃满客户区视口空间）。
   - **现代 CSS 视觉原语**：
     - **渐变背景**：深暗紫渐变 `linear-gradient(135deg, #26212b 0%, #1a171e 60%, #121015 100%)`。
     - **唱片同心圆材质**：`border-radius: 180px` 与 `radial-gradient` 唱片环纹。
     - **内置原生 CSS 动画**：`animation: spin 24s linear infinite;`，由 Zan 原生帧循环在底层无损驱动 60/120Hz 旋转，无需 Zan 业务逻辑手写角度插值。
     - **吸底磨砂毛玻璃**：`background: rgba(18, 15, 21, 0.95)` 半透明质感。

2. **`src/App.zan`（原生 Code-behind 逻辑）**：
   - 极简原生代码，仅 40 余行。
   - 通过 `HtmlHandlers` 将事件（如 `@click="OnTogglePlay"`）编译期安全绑定至强类型函数。
   - 负责数据状态管理、音频引擎调用与原生生命周期管理。

---

## 运行与验证

### 1. 命令行编译并启动
```bash
build\zanc.exe examples/music_player/src/App.html examples/music_player/src/App.zan --auto-stdlib -o _scratch/music.exe
_scratch\music.exe
```

### 2. 无头排版与全景观测（无弹窗秒级验真）
在 CI、终端或 AI 辅助环境中，可直接配合 `zan_gui_snapshot` 或无头驱动输出控件树与命中区：
```bash
$env:ZAN_UI_SCRIPT = "wait 300; dump hitregions regions.json; dump tree tree.json; quit"
$env:ZAN_UI_OUT = "_scratch/music_snapshot"
_scratch\music.exe
```
生成的 `tree.json` 会精确返回各节点的停靠模式、排版尺寸与层级结构，杜绝错位与遮蔽。
