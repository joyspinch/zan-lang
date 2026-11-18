# 3D First-Person Explorer (第一人称漫游与交互模板)

基于 Zan.Game.Render3D 工业级 3D 渲染管线驱动的第一人称漫游与 3D 空间拾取母版。

## 特性

- **3D 渲染与光照**：
  - 基于 `Primitives3D` 自动生成立体地面与建筑立方体网格。
  - 基于 `RenderPlan` 实现自动视锥裁剪（Frustum Culling）与 GPU 批次渲染（`Renderer3D`）。
  - 动态方向日光照与环境光混合。
- **第一人称控制器 (`FpsController3D`)**：
  - WASD + Space/Shift 平滑前后左右行走与升降。
  - 鼠标平滑转动视向，严格 ±89° 俯仰锁定杜绝万向节翻滚。
- **3D 空间实时射线求交 (`Raycast3D`)**：
  - 屏幕正中准星实时发射 3D 世界射线，与场景中的实体包围盒（AABB）求交。
  - 瞄准目标时准星自动高亮并显示锁定距离。

## 运行与编译

在当前主仓根目录运行以下 PowerShell 命令，直接编译模板原始源码，不复制模板树：

```powershell
Set-Location D:/project/zan-lang
& ./scripts/build_gate.ps1 -Name build -Script {
    build/zanc.exe templates/game/fps-3d/src/main.zan templates/game/fps-3d/src --auto-stdlib --package-project . --publish --subsystem windows -o build/fps3d.exe
}
build/fps3d.exe
```

`main.zan` 必须排在首位，随后 `src` 目录递归纳入 `FpsGame.zan`、`FpsHud.zan`、`FpsCrosshair.zan` 等全部源码，已列入口不会重复纳入。`--package-project .` 使用当前主仓的 `packages/`；当前模板没有需要额外内嵌的素材目录。生成的原生驱动文件应与程序保持在同一目录。
